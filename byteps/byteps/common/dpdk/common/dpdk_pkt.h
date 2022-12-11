#ifndef DPDK_PKT_H
#define DPDK_PKT_H
#include <cstdint>
#include <stdlib.h>     
#include <time.h>       
#include "common.h"

#define NUM_WORKER 2
// #define NEW_INCA
// #define SWITCH_TIMING

#define DPDK_ETH_HDR_SIZE 14
#define DPDK_HDR_FIELD_SIZE (8+4) // uint64_t key, int cmd
#define DPDK_PACKET_SIZE 1050 // 1024 data + 14 eth_hdr + 12 header field "key"
#define DPDK_DATA_SIZE (DPDK_PACKET_SIZE - DPDK_ETH_HDR_SIZE - DPDK_HDR_FIELD_SIZE)
#define MAX_ENTRIES_PER_PACKET 256
#define AGTR_SLOT_NUM 64
#define CIRC_TIME (MAX_ENTRIES_PER_PACKET / AGTR_SLOT_NUM)
// #define MAX_ENTRIES_PER_PACKET (64/2) // we will only use the "first" value of each register for now
#define DPDK_ELEMENT_NUM (MAX_ENTRIES_PER_PACKET * 4)

#define SEND_BURST_SIZE 1
#define BURST_SIZE 1
#define RX_RING 1
#define PS_RX_RING 1
// #define RX_RING_SIZE (4096 / PS_RX_RING) // when this value is too small, we might lose packet under pressure
#define RX_RING_SIZE (16384/ PS_RX_RING) // when this value is too small, we might lose packet under pressure
#define TX_RING_SIZE RX_RING_SIZE
#define CONGEST_THRES (RX_RING_SIZE / 1000 * 1000)
// #define RX_RING_SIZE 8192 // when this value is too small, we might lose packet under pressure
// #define TX_RING_SIZE 8192
// #define WORKER_RX_RING_SIZE 8192 // when this value is too small, we might lose packet under pressure
// #define WORKER_TX_RING_SIZE 8192
// #define WORKER_RX_RING_SIZE 16384 // when this value is too small, we might lose packet under pressure
// #define WORKER_TX_RING_SIZE 16384
#define WORKER_RX_RING_SIZE 32768 // when this value is too small, we might lose packet under pressure
#define WORKER_TX_RING_SIZE 32768

// #define NUM_MBUFS 32767
// #define MBUF_CACHE_SIZE 1000
#define WORKER_SEND_NUM_MBUFS 100000
#define WORKER_RECV_NUM_MBUFS 200000
// #define WORKER_SEND_NUM_MBUFS 100000
// #define WORKER_RECV_NUM_MBUFS 20000
#define NUM_MBUFS 100000
#define MBUF_CACHE_SIZE 200 // per documentation, NUM_MBUFS % MBUF_CACHE_SIZE should ideally be 0

/* MAC addresses */
// For safety reason, we have removed the real MAC address of our servers from the code

// #define MAX_AGTR_COUNT 20000
#define MAX_AGTR_COUNT 2500
#define NORM_SLOT 300

#ifdef NEW_INCA
struct p4mlhdr_index {
    uint32_t bitmap;
    uint8_t num_worker;
    uint32_t roundNum;
    // uint16_t roundNum;
    uint32_t max_info;
    uint32_t norm_info;
    uint8_t type_and_index; //[packet_type dataIndex pad]
    uint8_t recirc_times;
#ifndef SW_PS
    uint64_t ingress_global_timestamp;
    uint64_t egress_global_timestamp;
#endif
    uint16_t agtr;
    uint16_t vector[MAX_ENTRIES_PER_PACKET];
}__attribute__((packed));  // Attribute informing compiler to pack all members
#endif

struct p4mlhdr {
    uint32_t bitmap;
    uint8_t num_worker;
    uint32_t roundNum;
    // uint16_t roundNum;
    uint32_t max_info;
    uint32_t norm_info;
    uint8_t type_and_index; //[packet_type dataIndex pad]
    uint8_t recirc_times;
#ifndef SW_PS
    uint64_t ingress_global_timestamp;
    uint64_t egress_global_timestamp;
#endif
    uint16_t agtr;
    uint32_t vector[MAX_ENTRIES_PER_PACKET];
}__attribute__((packed));  // Attribute informing compiler to pack all members

#ifdef NEW_INCA
struct IndexMessage {
    struct p4mlhdr_index p4ml_hdrs;
    uint64_t key;
    int cmd; // 0: init, 1: sum, 2: norm
    int len;
#ifdef SW_PS
    bool shutdown;
#endif
    // float data[MAX_ENTRIES_PER_PACKET];
    // Name of the tensor.
    // std::string tensor_name;
}__attribute__((packed));  // Attribute informing compiler to pack all members
#endif

struct TestMessage {
    struct p4mlhdr p4ml_hdrs;
    uint64_t key;
    int cmd; // 0: init, 1: sum, 2: norm
    int len;
#ifdef SW_PS
    bool shutdown;
#endif
    // float data[MAX_ENTRIES_PER_PACKET];
    // Name of the tensor.
    // std::string tensor_name;
}__attribute__((packed));  // Attribute informing compiler to pack all members

struct Message {
    struct p4mlhdr p4ml_hdrs;
    uint64_t key;
    int cmd;
    char* data;
}__attribute__((packed));  // Attribute informing compiler to pack all members

struct rx_thread_args{
    int placeholder;
};

struct InitResponse {
    uint64_t key;
    char data[10];
};

struct agghdr {
    uint32_t bitmap;
    uint8_t num_worker;
    // uint8_t flag;
    // reserved       :  2;
    // isForceFoward  :  1;

    /* Current version
    overflow       :  1;
    PSIndex        :  2;
    dataIndex      :  1;
    ECN            :  1;
    isResend       :  1;
    isSWCollision  :  1;
    isACK          :  1;
    */

    uint16_t appID;
    uint16_t seq_num;
    uint16_t agtr;
    uint16_t agtr2;
    uint32_t vector[MAX_ENTRIES_PER_PACKET];
    uint64_t key;
    uint32_t len_tensor;
};

// This function assembled p4ml_h and p4ml_agtr_index_h
void inline assemble_norm_pkt(void* payload, uint32_t worker_id, uint8_t num_worker, uint32_t round_num, uint64_t key, float norm){
// void inline assemble_norm_pkt(void* payload, uint32_t worker_id, uint8_t num_worker, uint16_t round_num, uint64_t key, float norm){
#ifdef NEW_INCA
    p4mlhdr_index* p4ml_hdr = (p4mlhdr_index*)payload;
#else
    p4mlhdr* p4ml_hdr = (p4mlhdr*)payload;
#endif
    p4ml_hdr->bitmap = htonl(1 << worker_id);
    p4ml_hdr->num_worker = num_worker;
#ifdef SW_PS
    p4ml_hdr->agtr = htons((uint16_t)(MAX_AGTR_COUNT + key % NORM_SLOT));
#else
    p4ml_hdr->agtr = htons((uint16_t)(MAX_AGTR_COUNT * CIRC_TIME + key % NORM_SLOT));
#endif
    p4ml_hdr->roundNum = htonl(round_num);
    // p4ml_hdr->roundNum = htons(round_num);
    p4ml_hdr->type_and_index = (uint8_t)(1 << 7);
    p4ml_hdr->max_info = htonl(0);// TODO: for now, just set to zero
    memcpy(&(p4ml_hdr->norm_info), &norm, sizeof(uint32_t)); // reinterpret the bits to be uint32_t
    p4ml_hdr->norm_info = htonl(p4ml_hdr->norm_info);
    p4ml_hdr->recirc_times = (uint8_t)(CIRC_TIME-1); // norm packets don't need recirculation
#ifndef SW_PS
    p4ml_hdr->ingress_global_timestamp = (uint64_t)0;
    p4ml_hdr->egress_global_timestamp = (uint64_t)0;
#endif
}

// This function assembled p4ml_h and p4ml_agtr_index_h for DPDKPushPull
void inline assemble_dpdk_p4ml_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint32_t round_num, char* input_data, uint64_t key, uint32_t len){
// void inline assemble_dpdk_p4ml_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint16_t round_num, char* data, uint64_t key, uint32_t len){
#ifdef NEW_INCA
    p4mlhdr_index* p4ml_hdr = (p4mlhdr_index*)payload;
#else
    p4mlhdr* p4ml_hdr = (p4mlhdr*)payload;
#endif
    p4ml_hdr->bitmap = htonl(1 << worker_id);
    p4ml_hdr->num_worker = num_worker;
#ifdef SW_PS
    p4ml_hdr->agtr = htons((uint16_t)(key % MAX_AGTR_COUNT));
#else
    p4ml_hdr->agtr = htons((uint16_t)((key % MAX_AGTR_COUNT) * CIRC_TIME));
#endif
    p4ml_hdr->roundNum = htonl(round_num);
    // p4ml_hdr->roundNum = htons(round_num);
    p4ml_hdr->type_and_index = (uint8_t)0; // TODO: for now, just set to zero
    p4ml_hdr->max_info = htonl(0);// TODO: for now, just set to zero
    p4ml_hdr->norm_info = htonl(0);// TODO: for now, just set to zero
    p4ml_hdr->recirc_times = (uint8_t)0;
#ifndef SW_PS
    p4ml_hdr->ingress_global_timestamp = (uint64_t)0;
    p4ml_hdr->egress_global_timestamp = (uint64_t)0;
#endif

    uint32_t data_len = DPDK_ELEMENT_NUM;
    if (len < data_len) data_len = len;

    if(input_data){
#ifdef USE_UINT8
#ifdef NEW_INCA
        memcpy(p4ml_hdr->vector, input_data, (data_len+1)/2);
#else
        memcpy(p4ml_hdr->vector, input_data, data_len);
#endif
        // cast data pointer once here
        // float* data = (float*)input_data;
        // for (uint32_t i = 0; i < data_len; ++i) {
        //     ((uint8_t*)(p4ml_hdr->vector))[i] = (uint8_t)(data[i]);
        // }
#else
        for (uint32_t i = 0; i < MAX_ENTRIES_PER_PACKET; ++i) {
            p4ml_hdr->vector[i] = htonl((uint32_t)(((float*)data)[i]));
        }
#endif
    }
    // printf("p4mlhdr size: %d\n", sizeof(struct p4mlhdr));
}

// This function assembles a normal packet of floats
void inline assemble_normal_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint32_t round_num, char* data, uint64_t key, uint32_t len){
// void inline assemble_normal_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint16_t round_num, char* data, uint64_t key, uint32_t len){
    p4mlhdr* p4ml_hdr = (p4mlhdr*)payload;
    p4ml_hdr->bitmap = htonl(1 << worker_id);
    p4ml_hdr->num_worker = num_worker;
#ifdef SW_PS
    p4ml_hdr->agtr = htons((uint16_t)(key % MAX_AGTR_COUNT));
#else
    p4ml_hdr->agtr = htons((uint16_t)((key % MAX_AGTR_COUNT) * CIRC_TIME));
#endif
    p4ml_hdr->roundNum = htonl(round_num);
    // p4ml_hdr->roundNum = htons(round_num);
    p4ml_hdr->type_and_index = (uint8_t)0; // TODO: for now, just set to zero
    p4ml_hdr->max_info = htonl(0);// TODO: for now, just set to zero
    p4ml_hdr->norm_info = htonl(0);// TODO: for now, just set to zero
    p4ml_hdr->recirc_times = (uint8_t)0;
#ifndef SW_PS
    p4ml_hdr->ingress_global_timestamp = (uint64_t)0;
    p4ml_hdr->egress_global_timestamp = (uint64_t)0;
#endif
    uint32_t data_len = MAX_ENTRIES_PER_PACKET;
    if (len < data_len) data_len = len;
    if(data){
        for (uint32_t i = 0; i < data_len; ++i) {
            memcpy(&(p4ml_hdr->vector[i]), &(((float*)data)[i]), sizeof(uint32_t));
        }
    }
    // printf("p4mlhdr size: %d\n", sizeof(struct p4mlhdr));
}

// This function assembled p4ml_h and p4ml_agtr_index_h
void inline ps_assemble_p4ml_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint16_t agtr, uint32_t round_num, char* data_ptr){
// void inline ps_assemble_p4ml_hdrs(void* payload, uint32_t worker_id, uint8_t num_worker, uint16_t agtr, uint16_t round_num, char* data_ptr){
#ifdef USE_UINT8
    uint8_t* data = (uint8_t*)data_ptr;
#else 
    uint32_t* data = (uint32_t*)data_ptr;
#endif
    p4mlhdr* p4ml_hdr = (p4mlhdr*)payload;
    p4ml_hdr->bitmap = htonl(1 << worker_id);
    p4ml_hdr->num_worker = num_worker;
    p4ml_hdr->agtr = htons(agtr);
    p4ml_hdr->roundNum = htonl(round_num);
    // p4ml_hdr->roundNum = htons(round_num);
    p4ml_hdr->type_and_index = (uint8_t)0;
    p4ml_hdr->max_info = htonl(1);
    p4ml_hdr->norm_info = htonl(2);
    p4ml_hdr->recirc_times = (uint8_t)0;
#ifndef SW_PS
    p4ml_hdr->ingress_global_timestamp = (uint64_t)0;
    p4ml_hdr->egress_global_timestamp = (uint64_t)0;
#endif
    if(data){
#ifdef USE_UINT8
        for (uint32_t i = 0; i < MAX_ENTRIES_PER_PACKET * 4; ++i) {
            ((uint8_t*)(p4ml_hdr->vector))[i] = data[i];
        }
#else
        for (uint32_t i = 0; i < MAX_ENTRIES_PER_PACKET; ++i) {
            p4ml_hdr->vector[i] = data[i];// Here the content of data should already in network endianess
        }
#endif
    }
}

#endif // DPDK_PKT_H
