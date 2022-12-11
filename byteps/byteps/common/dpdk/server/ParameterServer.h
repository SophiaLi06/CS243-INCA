#include <cstdint>
#include <random>
#include <mutex>
#include <queue>
#include <memory>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <unordered_map>
#include <vector>
#include "../common/dpdk_pkt.h"
#include "../common/common.h"

#define MAX_TENSOR_SIZE 1024000
// // Lam: this one is useless since a PS can only handle 1app, to be mod.
// #define MAX_APP_PER_THREAD 5
// #define MAX_STORAGE_PER_APP_PER_THREAD 10
#define MAX_WORKER 16
#define TOTAL_AGTR_CNT 3000
#define PS_WORKERS 1
// #define TOTAL_AGTR_CNT 20479 // 0x4fff

// #define MAX_THREAD_PER_APP 20

#define OVERFLOW_HANDLE false

#define MULTI_CLIENT false

#define PS_TIMI

union data_t {
    int32_t *data_int;
    float *data_float;
};

struct PSJob{
    uint64_t key;
    int len;
    int cmd;
    uint16_t agtr;
    uint32_t roundNum;
    // uint16_t roundNum;
    struct rte_mbuf *send_pkt;
};

/* This is the context used by the server to store relevant info of each tensor */
struct tensor_context {
    bool isCompleted;
    data_t data;
    uint32_t len;
    uint64_t key;
    uint8_t num_worker;
    std::chrono::time_point<std::chrono::system_clock> start_time;
};

/* memory buff pools */
static struct rte_mempool *mbuf_pool[PS_WORKERS];
static struct rte_mempool *mbuf_recv_pool;

/* arrays corresponding to the registers on the switch */
uint32_t roundNumbers[TOTAL_AGTR_CNT];
// uint16_t roundNumbers[TOTAL_AGTR_CNT];
uint32_t bitmap[TOTAL_AGTR_CNT];
uint32_t norm_info[TOTAL_AGTR_CNT];
uint8_t receive_count[TOTAL_AGTR_CNT];

uint32_t data_slots[MAX_ENTRIES_PER_PACKET][TOTAL_AGTR_CNT];

/* mutex */
std::mutex _slot_roundnum_mutex[TOTAL_AGTR_CNT];

/* send pkt slots for gradient aggregators(MAX_AGTR_COUNT) and norm (NORM_SLOT) */
static struct rte_mbuf* send_pkt_ptrs[PS_WORKERS][TOTAL_AGTR_CNT];

/* mapping from tensor key to relavant context */
std::unordered_map<uint64_t, tensor_context*> context_table;

/* vector of worker mac address */
std::vector<struct rte_ether_addr> worker_addrs;

// This function assembled p4ml_h and p4ml_agtr_index_h for reply packets
void inline assemble_reply_p4ml_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint32_t round_num, 
                                     void* data, uint64_t key, uint32_t max_norm, uint16_t agtr_index){
// void inline assemble_reply_p4ml_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint16_t round_num, 
//                                      uint32_t* data, uint64_t key, uint32_t max_norm, uint16_t agtr_index){
    p4mlhdr* p4ml_hdr = (p4mlhdr*)payload;
    p4ml_hdr->bitmap = htonl(1 << worker_id); //TODO: this need to be updated to be the bitmap to send back
    p4ml_hdr->num_worker = num_worker;
    p4ml_hdr->agtr = htons(agtr_index);
    p4ml_hdr->roundNum = htonl(round_num);
    // p4ml_hdr->roundNum = htons(round_num);
    p4ml_hdr->type_and_index = (uint8_t)0; // TODO: for now, just set to zero
    p4ml_hdr->max_info = htonl(0);// TODO: for now, just set to zero
    p4ml_hdr->norm_info = htonl(max_norm);
    p4ml_hdr->recirc_times = (uint8_t)7; // this field is for Tofino switch recirculation;
                                         // here we can directly set as 7.
    if(data){
        memcpy(p4ml_hdr->vector, data, MAX_ENTRIES_PER_PACKET*4);
        // for (uint32_t i = 0; i < MAX_ENTRIES_PER_PACKET; ++i) {
        //     p4ml_hdr->vector[i] = data[i];
        // }
    }
}

void inline init_tensor(tensor_context* tensor, uint64_t key, uint32_t len, uint8_t num_workers) {
    tensor->data.data_int = new int32_t[len]();
    tensor->isCompleted = false;
    tensor->len = len;
    tensor->num_worker = num_workers;
    tensor->key = key;
}


// void inline init_tensor(tensor_context* tensor, uint32_t len) {
//     tensor->data.data_int = new int32_t[len]();
//     tensor->isCompleted = true;
//     tensor->isOccupy = new bool[MAX_TENSOR_SIZE / MAX_ENTRIES_PER_PACKET + 1]();
//     tensor->isCollision = new bool[MAX_TENSOR_SIZE / MAX_ENTRIES_PER_PACKET + 1]();
//     tensor->isFloat = new bool[MAX_TENSOR_SIZE / MAX_ENTRIES_PER_PACKET + 1]();
//     tensor->len = 0;
//     tensor->num_worker = 0;
//     tensor->key = 0xffffffffffffffff;
//     tensor->window_manager = new WindowManager[MAX_WORKER];
//     for (int i = 0; i < MAX_WORKER; i++) {
//         tensor->window_manager[i].isACKed = new bool[MAX_TENSOR_SIZE / MAX_ENTRIES_PER_PACKET + 1]();
//         tensor->window_manager[i].total_ACK = MAX_TENSOR_SIZE / MAX_ENTRIES_PER_PACKET + 1;
//     }
// }

// int inline check_tensor_available(tensor_context* tensor, agghdr* p4ml_header, int thread_id) {
//     // printf("*skey: %d, seq: %d\n", *skey, p4ml_header->seq_num);

//     // Already have completed model and not retrieve
//     if (tensor->isCompleted && p4ml_header->key != tensor->key) {
//         int total_ACK = ceil((float)p4ml_header->len_tensor / MAX_ENTRIES_PER_PACKET);
//         for (int i = 0; i < p4ml_header->num_worker; i++) 
//             tensor->window_manager[i].Reset(total_ACK);
//         // if (thread_id == 0)
//         // printf("Reset tensors[%d] LAST_ACK: %d\n", *skey, tensor->window_manager[0].last_ACK);
//         memset(tensor->data.data_int, 0, sizeof(int32_t) * p4ml_header->len_tensor);
//         memset(tensor->isOccupy, 0, sizeof(bool) * (total_ACK + 1));
//         memset(tensor->isCollision, 0, sizeof(bool) * (total_ACK + 1));
//         memset(tensor->isFloat, 0, sizeof(bool) * (total_ACK + 1));
//         tensor->num_worker = p4ml_header->num_worker; 
//         tensor->len = p4ml_header->len_tensor;
//         tensor->isCompleted = false;
//         tensor->key = p4ml_header->key;
//         // printf("Place %d available, real key = %d\n", *skey, tensors[*skey].key);
//         return 1;
//     } 
//     return 0;
// }

// void inline makeTensorReadyforFloat(agghdr *p4ml_header, tensor_context *tensor_cnt) {
//     int32_t* data = tensor_cnt->data.data_int;
//     uint16_t *p_seq = &p4ml_header->seq_num;
//     int32_t *p_model = p4ml_header->vector;
//     uint32_t offset = (*p_seq - 1) * MAX_ENTRIES_PER_PACKET;
    
//     /* Reset Data */
//     memset(data + offset, 0, sizeof(int32_t) * MAX_ENTRIES_PER_PACKET);
//     tensor_cnt->isOccupy[*p_seq] = false;
    
//     /* Reset Bitmap */
//     for (int i = 0; i < p4ml_header->num_worker; i++) {
//         tensor_cnt->window_manager[i].isACKed[p4ml_header->seq_num] = 0;
//     }
// }
