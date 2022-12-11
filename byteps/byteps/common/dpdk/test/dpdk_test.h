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
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>

#define FLOATING_POINT_INPUT true
#define DPDK_KEY_TOTAL 1200000

#define TEST_ROUND 20000
#define TEST_SIZE 10000

class DPDKManager{
public:
    DPDKManager(uint32_t host, int num_worker, int appID, int num_PS, int argc, char** argv);
    //~DPDKManager();

    static int TestLcore(void* thread_args);
    static void TestBurstySend(uint64_t pkt_num, uint64_t start_key);
    static void Send(uint64_t key, char* data, int len, int cmd, uint16_t agtr_index);
    void ShutdownSignal();

    int Shutdown();

private:
    static uint32_t host;
    static uint8_t num_worker;
    static uint8_t num_PS;
    static uint16_t appID;
    // for keys
    static uint64_t dpdkKey;
    // for aggregator index assignment
    static uint64_t aggr_count;
    // queues
    static std::queue<uint64_t> finishKeys;
    static std::queue<Job*> finishQueue;
    static std::queue<Job*>* pushPulljobQueue;
    // mutex locks
    static std::mutex _DPDKKey_mutex;
    static std::mutex _key_queue_mutex;
    static std::mutex _queuePush_mutex;
    static std::mutex _print_mutex;
    static std::mutex _cpubuff_mutex;
    static std::mutex _slot_roundnum_mutex[MAX_AGTR_COUNT+NORM_SLOT];
    // static std::mutex _slot_roundnum_mutex;
    static std::mutex _key_map_mutex;
    static std::mutex _part_finish_mutex;
    static struct rte_mempool *mbuf_pool;
    static struct rte_mempool *mbuf_recv_pool;
    static struct rte_ether_addr s_addr;
    static struct rte_ether_addr d_addr;
    static uint16_t ether_type;
    // send pkt slots for gradient aggregators(MAX_AGTR_COUNT) and norm (NORM_SLOT)
    static struct rte_mbuf* send_pkt_ptrs[WORKER_SEND_NUM_MBUFS+NORM_SLOT];
    static uint16_t slot_roundnum[MAX_AGTR_COUNT+NORM_SLOT];
    static bool slot_ready[MAX_AGTR_COUNT+NORM_SLOT];
    // functions
    static int port_init(struct rte_mempool *mbuf_recv_pool);
    // map from key to job info
    /* mapping from tensor key to relavant context and counters */
    static std::unordered_map<uint64_t, struct Job*> job_table;
    static std::unordered_map<uint64_t, uint64_t> dpdk_key_to_tensor_key;
    static std::unordered_map<uint64_t, uint64_t> tensor_key_part_total;
    static std::unordered_map<uint64_t, uint64_t> tensor_key_part_finish;
    static std::unordered_map<uint64_t, char*> cpubuff_table;
    static volatile bool should_shutdown;
    static volatile uint64_t expected_key;
    // additional stuff for testing only
    static float data[MAX_ENTRIES_PER_PACKET];

}; //DPDKManager
