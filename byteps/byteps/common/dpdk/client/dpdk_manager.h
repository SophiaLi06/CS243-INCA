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
#include "../common/dpdk_pkt.h"
#include "../common/common.h"
#include "../../common.h"
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>

#define FLOATING_POINT_INPUT true
#define DPDK_KEY_TOTAL 1200000

#define NORM_TIMING

class DPDKManager{
public:
    DPDKManager(uint32_t host, int num_worker, int appID, int num_PS, int use_compression, int argc, char** argv);
    //~DPDKManager();

    uint64_t GetNewKey();
    // struct Job* InitJob(struct Job* new_job, char* data, uint32_t len);
    //void InitPushPull(struct Job job);
    static void Send(uint64_t key, char* data, int len, int cmd, std::string tensor_name, uint16_t agtr_index);
    void DPDKPushPull(uint64_t tensor_key, char* data, int len, int cmd, std::string tensor_name);
    void ShutdownSignal();
    float CommunicateNorm(uint64_t key, float max_norm);
    // void PushPull(struct Job job, int cmd);
    // static void Push(struct Job* job, int cmd);
    int64_t PollFinishKey();
    int64_t GetFinishKey();
    // static int TestLcore(void* thread_args);
    static int RxThread(void* thread_args);
    static int TxThread(void* thread_args);
    static int BackupTxThread(void* thread_args);
    
    void Reset();
    int Shutdown();

private:
    static uint32_t host;
    static uint8_t num_worker;
    static uint8_t num_PS;
    static uint16_t appID;
    static int use_compression;
    // for keys
    static uint64_t dpdkKey;
    // for aggregator index assignment
    static uint64_t aggr_count;
    // queues
    static std::queue<uint64_t> finishKeys;
    static std::queue<Job*> finishQueue;
    static std::queue<Job> pushpulljobQueue;
    // mutex locks
    static std::mutex _DPDKKey_mutex;
    static std::mutex _key_queue_mutex;
    static std::mutex _finish_queue_mutex;
    static std::mutex _print_mutex;
    static std::mutex _cpubuff_mutex;
    static std::mutex _slot_roundnum_mutex[MAX_AGTR_COUNT+NORM_SLOT];
    static std::mutex _slot_ready_mutex[MAX_AGTR_COUNT+NORM_SLOT];
    static std::mutex _part_finish_mutex;
    static std::mutex _pushpull_queue_mutex;
    static std::mutex _in_flight_mutex;
    static std::mutex _norm_table_mutex;
    static struct rte_mempool *mbuf_pool;
    static struct rte_mempool *mbuf_recv_pool;
    static struct rte_ether_addr s_addr;
    static struct rte_ether_addr d_addr;
    static uint16_t ether_type;
    // send pkt slots for gradient aggregators(MAX_AGTR_COUNT) and norm (NORM_SLOT)
    static struct rte_mbuf* send_pkt_ptrs[WORKER_SEND_NUM_MBUFS+NORM_SLOT];
    // static struct rte_mbuf* send_pkt_ptrs[MAX_AGTR_COUNT+NORM_SLOT];
    static uint32_t slot_roundnum[MAX_AGTR_COUNT+NORM_SLOT];
    // static uint16_t slot_roundnum[MAX_AGTR_COUNT+NORM_SLOT];
    static volatile bool slot_ready[MAX_AGTR_COUNT+NORM_SLOT];
    static uint64_t slot_dpdk_key[MAX_AGTR_COUNT+NORM_SLOT];
    // functions
    static int port_init(struct rte_mempool *mbuf_recv_pool);
    // map from key to job info
    /* mapping from tensor key to relavant context and counters */
    static std::unordered_map<uint64_t, struct Job*> job_table;
    static uint64_t dpdk_key_to_tensor_key[DPDK_KEY_TOTAL];
    static char* cpubuff_table[DPDK_KEY_TOTAL];
    static int dpdk_key_to_mutex_idx[DPDK_KEY_TOTAL];
    static int curr_mutex_idx;
    static std::mutex mutex_pool[DPDK_KEY_TOTAL];
    static int tensor_key_part_total[DPDK_KEY_TOTAL];
    static int tensor_key_part_finish[DPDK_KEY_TOTAL];
    // static std::unordered_map<uint64_t, uint64_t> tensor_key_part_total;
    // static std::unordered_map<uint64_t, uint64_t> tensor_key_part_finish;
    static volatile bool should_shutdown;
    static volatile uint64_t expected_key;
    // the number of packets still in flight
    static volatile int in_flight_count;
    /* maps for norm communication */
    static volatile bool norm_ready[DPDK_KEY_TOTAL];
    static float norm_value[DPDK_KEY_TOTAL];
#ifdef NORM_TIMING
    static std::unordered_map<uint64_t, std::chrono::time_point<std::chrono::system_clock>> norm_start_time;
    static double norm_overall_time;
    static uint32_t norm_comm_count;
#endif
    static uint32_t congest_count;
    /* study packet processing time */
#ifdef SWITCH_TIMING
    static uint64_t switch_process_time;
    static uint32_t switch_timing_count;
#endif

}; //DPDKManager
