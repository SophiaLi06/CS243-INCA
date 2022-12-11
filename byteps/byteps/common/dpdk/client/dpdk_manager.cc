#include "dpdk_manager.h"

uint32_t DPDKManager::host;
uint8_t DPDKManager::num_worker;
uint8_t DPDKManager::num_PS;
uint16_t DPDKManager::appID;
int DPDKManager::use_compression;
uint64_t DPDKManager::dpdkKey;
uint64_t DPDKManager::aggr_count;

std::queue<uint64_t> DPDKManager::finishKeys;

std::mutex DPDKManager::_DPDKKey_mutex;
std::mutex DPDKManager::_key_queue_mutex;
std::mutex DPDKManager::_finish_queue_mutex;
std::mutex DPDKManager::_print_mutex;
// std::mutex DPDKManager::_cpubuff_mutex;
std::mutex DPDKManager::_slot_roundnum_mutex[MAX_AGTR_COUNT+NORM_SLOT];
std::mutex DPDKManager::_slot_ready_mutex[MAX_AGTR_COUNT+NORM_SLOT];
std::mutex DPDKManager::_part_finish_mutex;
int DPDKManager::dpdk_key_to_mutex_idx[DPDK_KEY_TOTAL];
int DPDKManager::curr_mutex_idx;
std::mutex DPDKManager::mutex_pool[DPDK_KEY_TOTAL];
std::mutex DPDKManager::_pushpull_queue_mutex;
std::mutex DPDKManager::_in_flight_mutex;
std::mutex DPDKManager::_norm_table_mutex;
std::queue<Job*> DPDKManager::finishQueue;
std::queue<Job> DPDKManager::pushpulljobQueue;
std::unordered_map<uint64_t, struct Job*> DPDKManager::job_table;
char* DPDKManager::cpubuff_table[DPDK_KEY_TOTAL];
uint64_t DPDKManager::dpdk_key_to_tensor_key[DPDK_KEY_TOTAL];
int DPDKManager::tensor_key_part_total[DPDK_KEY_TOTAL];
int DPDKManager::tensor_key_part_finish[DPDK_KEY_TOTAL];
// std::unordered_map<uint64_t, uint64_t> DPDKManager::tensor_key_part_total;
// std::unordered_map<uint64_t, uint64_t> DPDKManager::tensor_key_part_finish;
volatile bool DPDKManager::norm_ready[DPDK_KEY_TOTAL];
float DPDKManager::norm_value[DPDK_KEY_TOTAL];
/* the belows are for timing the norm communication */
#ifdef NORM_TIMING
std::unordered_map<uint64_t, std::chrono::time_point<std::chrono::system_clock>> DPDKManager::norm_start_time;
double DPDKManager::norm_overall_time;
uint32_t DPDKManager::norm_comm_count;
#endif
#ifdef SWITCH_TIMING
uint64_t DPDKManager::switch_process_time;
uint32_t DPDKManager::switch_timing_count;
#endif

struct rte_mempool* DPDKManager::mbuf_pool;
struct rte_mempool* DPDKManager::mbuf_recv_pool;
struct rte_ether_addr DPDKManager::s_addr;
struct rte_ether_addr DPDKManager::d_addr;
uint16_t DPDKManager::ether_type;
struct rte_mbuf* DPDKManager::send_pkt_ptrs[WORKER_SEND_NUM_MBUFS+NORM_SLOT];
// struct rte_mbuf* DPDKManager::send_pkt_ptrs[MAX_AGTR_COUNT+NORM_SLOT];
uint32_t DPDKManager::slot_roundnum[MAX_AGTR_COUNT+NORM_SLOT];
// uint16_t DPDKManager::slot_roundnum[MAX_AGTR_COUNT+NORM_SLOT];
volatile bool DPDKManager::slot_ready[MAX_AGTR_COUNT+NORM_SLOT];
uint64_t DPDKManager::slot_dpdk_key[MAX_AGTR_COUNT+NORM_SLOT];
volatile bool DPDKManager::should_shutdown;
volatile uint64_t DPDKManager::expected_key;
volatile int DPDKManager::in_flight_count;
uint32_t DPDKManager::congest_count;


int DPDKManager::port_init(struct rte_mempool *mbuf_recv_pool){
    struct rte_eth_txmode txmode;
    struct rte_eth_rxmode rxmode;
    // struct rte_eth_rss_conf rss_conf;
    rxmode.mtu = RTE_ETHER_MAX_LEN;
    // rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
    // rss_conf.rss_key = NULL;
    // rss_conf.rss_hf = RTE_ETH_RSS_IP;
    struct rte_eth_conf port_conf = {};
    struct rte_eth_rxconf rxq_conf = {};
    struct rte_eth_txconf txq_conf = {};
    rxq_conf.rx_drop_en = 0; //Don't drop packets if no descriptors are available.
    txq_conf.tx_free_thresh = 0; //Start freeing Tx buffers if there are less free descriptors than this value.
    port_conf.rxmode = rxmode;
    port_conf.txmode = txmode;
    // port_conf.rx_adv_conf.rss_conf = rss_conf;
	const uint16_t rx_rings = RX_RING, tx_rings = 1;
    struct rte_eth_dev_info dev_info;
	int retval;
	uint16_t q;

    /* Offload fast free */
    rte_eth_dev_info_get(0, &dev_info);
	// if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE){
    //     printf("Fast free\n");
    //     port_conf.txmode.offloads |=
	// 		DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    // }

	/* Init port 0 and setup rx and tx queues for it. */
	retval = rte_eth_dev_configure(0, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up RX_RING RX queues per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(0, q, WORKER_RX_RING_SIZE,
				rte_eth_dev_socket_id(0), &rxq_conf, mbuf_recv_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(0, q, WORKER_TX_RING_SIZE,
				rte_eth_dev_socket_id(0), &txq_conf);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(0);
	if (retval < 0)
		return retval;

    /* Enable receipt in promiscuous mode for an Ethernet device so that packets
       are accpected regardless of destination MAC */
    retval = rte_eth_promiscuous_enable(0);
    if (retval < 0)
		return retval;

	return 0;
}

#define MAX_PATTERN_NUM 4
static struct rte_flow *
generate_rule(uint16_t tx_port,  uint16_t rx_q, struct rte_flow_error *error) {
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[MAX_PATTERN_NUM];
    struct rte_flow_action action[MAX_PATTERN_NUM];
    struct rte_flow *flow = NULL;
    struct rte_flow_action_queue queue;
    queue.index = rx_q;
    struct rte_flow_item_eth eth_spec;
    struct rte_flow_item_eth eth_mask;

    memset(pattern, 0, sizeof(pattern));
    memset(action, 0, sizeof(action));
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.ingress = 1;

    action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[0].conf = &queue;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
    memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
    eth_spec.type = htons(0x0a00+rx_q);
    eth_mask.type = 0xffff;
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = &eth_spec;
    pattern[0].mask = &eth_mask;

    pattern[1].type = RTE_FLOW_ITEM_TYPE_END;

    int res = rte_flow_validate(tx_port, &attr, pattern, action, error);
    if (res < 0) printf("Flow validation failed %s\n", error->message);
    else flow = rte_flow_create(tx_port, &attr, pattern, action, error);
    
    return flow;
}

static void
flow_init(void) {
    const uint16_t rx_rings = RX_RING;
    struct rte_flow_error error;
    struct rte_flow *flow;
    for (uint16_t loop = 0; loop < rx_rings; loop++) {
        flow = generate_rule(0, loop, &error);
        if (!flow)
            rte_exit(EXIT_FAILURE, "Flow can't be created %d message: %s\n", error.type, error.message ? error.message : "(no stated reason)");
    }
}

int DPDKManager::RxThread(void* thread_args){
    unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("listening DPDK thread at lcore %u on CPU %d, thread id = %d\n", lcore_id, sched_getcpu(), pthread_self());

    int ack_count = 0;
    while(!should_shutdown){
#ifdef USE_EXPECTED_KEY
        struct rte_mbuf* recv_pkt[std::max(BURST_SIZE, 4)];
#else
		struct rte_mbuf* recv_pkt[std::max(BURST_SIZE, 1280)];
#endif
		/* retrieve received packets
        /* (port_id, queue_id, rte_mbuf** rx_pkts(points to the mbuf array of the received packets)
		/*  , maximum_num_pkts) */
#ifdef USE_EXPECTED_KEY
        uint16_t num_rx = rte_eth_rx_burst(0, lcore_id - 1, recv_pkt, std::max(BURST_SIZE, 4));
#else
		uint16_t num_rx = rte_eth_rx_burst(0, lcore_id - 1, recv_pkt, std::max(BURST_SIZE, 1280));
#endif
		if(num_rx == 0){
			//no packets retrieved, skip
			continue;
		}
		
        /* processing the retrieved packet */
		struct rte_ether_hdr* recv_eth_hdr;
#ifdef SWITCH_TIMING
        uint64_t ingress_tstamp, egress_tstamp;
        uint32_t high_part, low_part;
#endif
		/* print received message */
		for(uint16_t i=0;i<num_rx;i++){
		    /* Print the MAC address of the sender of the retrieved packets
            /* (mbuf, data type)
            */
			recv_eth_hdr = rte_pktmbuf_mtod(recv_pkt[i],struct rte_ether_hdr*);
            // _print_mutex.lock();
            // printf("Received rss hash %d\n", recv_pkt[i]->hash.rss);
            // printf("Received ether_type %hu\n", ntohs(recv_eth_hdr->ether_type));
            // printf("Received packet from MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
            //         " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " : ",
            //         recv_eth_hdr->src_addr.addr_bytes[0],recv_eth_hdr->src_addr.addr_bytes[1],
            //         recv_eth_hdr->src_addr.addr_bytes[2],recv_eth_hdr->src_addr.addr_bytes[3],
            //         recv_eth_hdr->src_addr.addr_bytes[4],recv_eth_hdr->src_addr.addr_bytes[5]);

            struct TestMessage* recv_msg;
#ifdef IP_HDR
			recv_msg = (struct TestMessage*)((rte_pktmbuf_mtod(recv_pkt[i],char*)) + sizeof(struct rte_ether_hdr)
                                              + sizeof(struct rte_ipv4_hdr));
#else
            recv_msg = (struct TestMessage*)((rte_pktmbuf_mtod(recv_pkt[i],char*)) + sizeof(struct rte_ether_hdr));
#endif
#ifdef SWITCH_TIMING
            // only selective record switch processing time
            if (recv_msg->key == (uint64_t)1){
                ingress_tstamp = recv_msg->p4ml_hdrs.ingress_global_timestamp;
                egress_tstamp = recv_msg->p4ml_hdrs.egress_global_timestamp;
                high_part = ntohl((uint32_t)(ingress_tstamp >> 32));
                low_part = ntohl((uint32_t)(ingress_tstamp & 0xFFFFFFFF));
                ingress_tstamp = (((uint64_t)low_part) << 32) | high_part;
                high_part = ntohl((uint32_t)(egress_tstamp >> 32));
                low_part = ntohl((uint32_t)(egress_tstamp & 0xFFFFFFFF));  
                egress_tstamp = (((uint64_t)low_part) << 32) | high_part;
                switch_process_time += (egress_tstamp - ingress_tstamp);
                switch_timing_count++;
            }
            
            // printf("ingress tstamp: %" PRIu64 ", ", (((uint64_t)low_part) << 32) | high_part);
            // high_part = ntohl((uint32_t)(egress_tstamp >> 32));
            // low_part = ntohl((uint32_t)(egress_tstamp & 0xFFFFFFFF));       
            // printf("egress tstamp: %" PRIu64 "\n", (((uint64_t)low_part) << 32) | high_part);
#endif
            // printf("RECV Key: %" PRIu64 ", CMD: %d, round_num: %hu of agtr_idx %hu\n", recv_msg->key, recv_msg->cmd, ntohs(recv_msg->p4ml_hdrs.roundNum),
            //        ntohs(recv_msg->p4ml_hdrs.agtr));
#ifdef SW_PS
            
            if((ntohs(recv_eth_hdr->ether_type) != 0x0d00 + lcore_id - 1) 
               || (recv_msg->cmd > TENSOR_INIT) || recv_msg->key >= DPDK_KEY_TOTAL)
#else
            if(ntohs(recv_eth_hdr->ether_type) != 0x0a00 || (recv_msg->cmd > TENSOR_INIT) || recv_msg->key >= DPDK_KEY_TOTAL)
#endif
            {
                // printf("received packet of wrong format\n");
                // _print_mutex.unlock();
                rte_pktmbuf_free(recv_pkt[i]);
                continue;
            }
#ifdef USE_EXPECTED_KEY
            // Note that the expected_key checking condition is relaxed so that we account for
            // receiving up to four packets at once and some of them might be reordered
            if((expected_key > 3 && recv_msg->key < expected_key - 4) && recv_msg->key != 0){
                // This is an old packet that is retrieved again, disregard it
                _print_mutex.lock();
                printf("received unexpected key %" PRIu64 " at address %p\n", recv_msg->key, recv_pkt[i]);
                _print_mutex.unlock();
                rte_pktmbuf_free(recv_pkt[i]);
                // continue;
            }
#else
            uint16_t agtr_index;
#ifdef SW_PS
            agtr_index = ntohs(recv_msg->p4ml_hdrs.agtr);  
#else
            if (recv_msg->cmd == TENSOR_NORM) 
                agtr_index = MAX_AGTR_COUNT + (recv_msg->key) % NORM_SLOT;
            if (recv_msg->cmd == TENSOR_COMM) 
                agtr_index = (ntohs(recv_msg->p4ml_hdrs.agtr) - (CIRC_TIME-1)) / CIRC_TIME;
#endif
            uint32_t expected_roundnum = slot_roundnum[agtr_index];
            // uint16_t expected_roundnum = slot_roundnum[ntohs(recv_msg->p4ml_hdrs.agtr)];

            if (ntohl(recv_msg->p4ml_hdrs.roundNum) != expected_roundnum){
                // _print_mutex.lock();
                // printf("cmd %d expected roundNum %hu for slot %hu but got %hu\n", recv_msg->cmd, expected_roundnum, 
                //         agtr_index, ntohl(recv_msg->p4ml_hdrs.roundNum));
                // _print_mutex.unlock();

                rte_pktmbuf_free(recv_pkt[i]);
                continue;
            }
#endif
            slot_roundnum[agtr_index]++;

            /* Process norm packets*/
            if (recv_msg->cmd == TENSOR_NORM){
                float norm_info;
                uint32_t norm_int_format = ntohl(recv_msg->p4ml_hdrs.norm_info);
                memcpy(&norm_info, &norm_int_format, sizeof(uint32_t));
                // printf("Key: %" PRIu64 ", norm: %.6f\n", recv_msg->key, norm_info);
                
                norm_value[recv_msg->key] = norm_info;
                norm_ready[recv_msg->key] = true;
#ifdef NORM_TIMING
                _norm_table_mutex.lock();
                auto current_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> time_span =
                    (current_time - norm_start_time[recv_msg->key]);
                norm_overall_time += time_span.count(); 
                _norm_table_mutex.unlock();
#endif

                // _print_mutex.unlock();
                rte_pktmbuf_free(recv_pkt[i]);
                continue;
            }

            else{
#ifndef NO_CPUBUFF_COPY
                if(recv_msg->cmd == TENSOR_COMM){
                    // update cpubuff data here
                    // _cpubuff_mutex.lock();
                    char* data_buff = (cpubuff_table[recv_msg->key]);
                    // _cpubuff_mutex.unlock();
                    if(!data_buff) printf("WARNING: key  %" PRIu64 " returns no cpubuff pointer\n", recv_msg->key);
                    else{
                        if(use_compression) 
                            memcpy(data_buff, recv_msg->p4ml_hdrs.vector, recv_msg->len);
                        else
                            memcpy(data_buff, recv_msg->p4ml_hdrs.vector, sizeof(uint32_t)*(recv_msg->len));
                        // _print_mutex.lock();
                        // printf("key  %" PRIu64 " returns cpubuff content\n", recv_msg->key);
                        // for(int i = 0; i < 10; ++i) printf("%hhu ", ((uint8_t*)data_buff)[i]);
                        // printf("\n");
                        // _print_mutex.unlock();
                    }
                    // _cpubuff_mutex.unlock();
                }
#endif
                // _print_mutex.unlock();
#ifdef USE_EXPECTED_KEY
                expected_key = recv_msg->key + 1;
#endif
                uint64_t tensor_key = dpdk_key_to_tensor_key[recv_msg->key];

                int mutex_idx = dpdk_key_to_mutex_idx[recv_msg->key];
                // mutex_pool[mutex_idx].lock();
                tensor_key_part_finish[mutex_idx]++;
                if(tensor_key_part_finish[mutex_idx] == tensor_key_part_total[mutex_idx]){
                    _key_queue_mutex.lock();
                    finishKeys.push(tensor_key);
                    _key_queue_mutex.unlock();
                }
                // mutex_pool[mutex_idx].unlock();

                // _slot_ready_mutex[agtr_index].lock();
                slot_ready[agtr_index] = true;
                slot_dpdk_key[agtr_index] = 0;
                // _slot_ready_mutex[agtr_index].unlock();

                // _in_flight_mutex.lock();
                // in_flight_count--;
                // _in_flight_mutex.unlock();
            
			    rte_pktmbuf_free(recv_pkt[i]);
                ++ack_count;
            }
		}	
    }
    printf("lcore %u number of ack: %d\n", lcore_id, ack_count);

    return 0;
}

int DPDKManager::TxThread(void* thread_args){
    unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("Sending DPDK thread at lcore %u on CPU %d, thread id = %d\n", lcore_id, sched_getcpu(), pthread_self());
    while(!should_shutdown){
        uint16_t pkts_to_send = std::min((uint16_t)SEND_BURST_SIZE, (uint16_t)(pushpulljobQueue.size()));
        if(!pkts_to_send) continue; // skip if the pushpulljobQueue is empty
        struct rte_mbuf* pkt[pkts_to_send];
        uint16_t pkt_idx = 0;

        while (pkt_idx < pkts_to_send){
            /* dequeue push job*/
            _pushpull_queue_mutex.lock();
            if(pushpulljobQueue.empty()){
                _pushpull_queue_mutex.unlock();
                usleep(1);
            } 
            else{
                struct Job push_job = pushpulljobQueue.front();
                // _slot_ready_mutex[push_job.agtr_index].lock();
                if (slot_ready[push_job.agtr_index]){
                    slot_ready[push_job.agtr_index] = false; // this slot is now being used
                    slot_dpdk_key[push_job.agtr_index] = push_job.key;

                    /* assemble a packet and put into the pkt array */
#ifdef NEW_INCA
                    struct IndexMessage* msg;
                    struct IndexMessage msg_body;
#else
                    struct TestMessage* msg;
                    struct TestMessage msg_body;
#endif
                    struct rte_ether_hdr *eth_hdr;
#ifdef IP_HDR
                    struct rte_ipv4_hdr  *ip4_hdr;
#endif
                    uint16_t send_buf_index;
                    uint32_t round_num;
                    // uint16_t round_num;
                    uint64_t key = push_job.key;
                    char* data = push_job.data;
                    int len = push_job.len;
                    int cmd = push_job.cmd;
                    std::string tensor_name = push_job.tensor_name;
                    uint16_t agtr_index = push_job.agtr_index;

                    send_buf_index = key % WORKER_SEND_NUM_MBUFS;
                    round_num = slot_roundnum[agtr_index];

                    if(use_compression) {
                        assemble_dpdk_p4ml_hdrs(&(msg_body.p4ml_hdrs), 
                            host, (uint8_t)NUM_WORKER, round_num, data, key, len);
                    }
                    else{
                        assemble_normal_hdrs(&(msg_body.p4ml_hdrs), 
                            host, (uint8_t)NUM_WORKER, round_num, data, key, len);
                    }
                    msg_body.key = key;
                    msg_body.cmd = cmd;
                    msg_body.len = len;
#ifdef SW_PS
                    msg_body.shutdown = false;
#endif
        
                    pkt[pkt_idx] = send_pkt_ptrs[send_buf_index];
                    eth_hdr = rte_pktmbuf_mtod(pkt[pkt_idx], struct rte_ether_hdr*);
                    eth_hdr->dst_addr = d_addr;
                    eth_hdr->src_addr = s_addr;
#ifdef SW_PS
                    eth_hdr->ether_type = htons(0x0c00+(uint16_t)((agtr_index)%PS_RX_RING));
#else
                    eth_hdr->ether_type = htons(0x0a00+(uint16_t)((agtr_index)%PS_RX_RING));
#endif
#ifdef IP_HDR
                    // add ip header
	                ip4_hdr = rte_pktmbuf_mtod_offset(pkt[pkt_idx], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
		            ip4_hdr->src_addr = htonl((uint32_t)agtr_index);
#ifdef NEW_INCA
                    msg = (struct IndexMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) 
                                                + sizeof(struct rte_ipv4_hdr));
                    int pkt_size = sizeof(struct IndexMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
                    msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) 
                                                + sizeof(struct rte_ipv4_hdr));
                    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#endif
#else
#ifdef NEW_INCA
                    msg = (struct IndexMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr));
                    int pkt_size = sizeof(struct IndexMessage) + sizeof(struct rte_ether_hdr);
#else
                    msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr));
                    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
#endif // IP_HDR
                    *msg = msg_body;

                    // _print_mutex.lock();
                    pkt[pkt_idx]->data_len = pkt_size;
                    pkt[pkt_idx]->pkt_len = pkt_size;

                    // printf("SENT Key: %" PRIu64 " for tensor %s with roundnum %hu of len %d\n", msg->key, tensor_name.c_str(), round_num, msg->len);
        // // printf("worker_bitmap: %d, num_worker: %d, worker_id: %d, round_num: %d\n", 
        // //         ntohl(msg->p4ml_hdrs.bitmap), msg->p4ml_hdrs.num_worker, ntohs(msg->p4ml_hdrs.agtr), ntohs(msg->p4ml_hdrs.roundNum));
        // printf("pkt len: %d, sample of data: \n", pkt[pkt_idx]->pkt_len);
        // for(int i = 0; i < 10; ++i) printf("%.6f ", (msg->data)[i]);
        // printf("\n");
// #ifdef USE_UINT8
//         for(int i = 0; i < 10; ++i) printf("%hhu ", ((uint8_t*)(msg->p4ml_hdrs.vector))[i]);
// #else
//         for(int i = 0; i < 10; ++i) printf("%d ", ntohl(msg->p4ml_hdrs.vector[i]));
// #endif
//         printf("\n");

        // update the expected roundnum of the used aggregator slot
        // slot_roundnum[agtr_index]++;
                    // _print_mutex.unlock();
                    // _slot_ready_mutex[push_job.agtr_index].unlock();
                    // // update the refcnt of this sending mbuf
                    // rte_pktmbuf_refcnt_update(pkt[pkt_idx], 1);

                    ++pkt_idx;
                    pushpulljobQueue.pop();
                    _pushpull_queue_mutex.unlock();
                }
                else{
                    // _slot_ready_mutex[push_job.agtr_index].unlock();
                    // push the job back into the queue
                    // pushpulljobQueue.push(push_job);
                    // pushpulljobQueue.pop();
                    _pushpull_queue_mutex.unlock();
                    usleep(1);
                }
                // _pushpull_queue_mutex.unlock();
            }
        }

        // _in_flight_mutex.lock();
        // in_flight_count+=pkts_to_send;
        // _in_flight_mutex.unlock();

        // if(in_flight_count > CONGEST_THRES) {
        //     // printf("WARNING: rx queue might overflow\n");
        //     congest_count++;
        //     while(in_flight_count > 100) pthread_yield();
        // }

        /* send the packets */
        uint16_t num_tx = 0;
        while (num_tx < pkts_to_send){
            num_tx += rte_eth_tx_burst(0, 0, pkt+num_tx, pkts_to_send-num_tx); // returns the number of packets transmitted
        } 
        // printf("SENT %hu packets\n", num_tx);

    }
    return 0;
}

int DPDKManager::BackupTxThread(void* thread_args){
    unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("Sending DPDK thread at lcore %u on CPU %d, thread id = %d\n", lcore_id, sched_getcpu(), pthread_self());
    while(!should_shutdown){
        _pushpull_queue_mutex.lock();
        if(pushpulljobQueue.empty()){
            _pushpull_queue_mutex.unlock();
            usleep(1);
        } 
        else{
            struct Job push_job = pushpulljobQueue.front();
            pushpulljobQueue.pop();
            _slot_ready_mutex[push_job.agtr_index].lock();
            if (slot_ready[push_job.agtr_index]){
                slot_ready[push_job.agtr_index] = false; // this slot is now being used
                slot_dpdk_key[push_job.agtr_index] = push_job.key;
                // send the packet
                Send(push_job.key, push_job.data, push_job.len, push_job.cmd, push_job.tensor_name, push_job.agtr_index);
                // _in_flight_mutex.lock();
                // in_flight_count++;
                // _in_flight_mutex.unlock();
                _slot_ready_mutex[push_job.agtr_index].unlock();
            }
            else{
                _slot_ready_mutex[push_job.agtr_index].unlock();
                // push the job back into the queue
                pushpulljobQueue.push(push_job);
            }
            // _slot_ready_mutex.unlock();
            _pushpull_queue_mutex.unlock();
        }
        // if(in_flight_count > 10000) {
        //     // printf("WARNING: rx queue might overflow\n");
        //     congest_count++;
        //     while(in_flight_count > 100) pthread_yield();
        // }
    }
    return 0;
}


DPDKManager::DPDKManager(uint32_t host, int num_worker, int appID, int num_PS, int use_compression, int argc, char** argv){
#ifdef SET_AFFINITY
    // /* set the CPU affinity for the running thread */    
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(16, &cpu_set);
    CPU_SET(17, &cpu_set);
    if(sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0){
        printf("WARNING: DPDK failed to set affinity\n");
    }
#endif
    this->host = host;
    this->appID = (uint16_t)appID;
    this->num_worker = (uint8_t)num_worker;
    this->num_PS = (uint8_t)num_PS;
    this->use_compression = use_compression;
    this->aggr_count = (uint64_t)0;
    this->dpdkKey = (uint64_t)0;
    this->curr_mutex_idx = 0;

    /* check arguments */
    printf("argc: %d\n", argc);
    for (int temp = 0; temp < argc; ++temp){
        int i = 0;
        while(argv[temp][i] != '\0' && i < 35){
            printf("%c",argv[temp][i]);
            ++i;
        }
        printf("\n");
    }

    /* initialize the DPDK connection here */
    
    // initialize EAL
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "initialize fail!");
    printf("Done EAL initialization!\n");
    
    // Creates a new mempool in memory to hold the mbufs.
    this->mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", WORKER_SEND_NUM_MBUFS+10,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    this->mbuf_recv_pool = rte_pktmbuf_pool_create("MBUF_RECV_POOL", WORKER_RECV_NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (this->mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    if (this->mbuf_recv_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf recv pool\n");
    printf("Congestion threshold: %d\n", CONGEST_THRES);
    printf("rte_socket_id: %u\n", rte_socket_id());

    for (int i = 0; i < WORKER_SEND_NUM_MBUFS+NORM_SLOT; ++i){
        this->send_pkt_ptrs[i] = rte_pktmbuf_alloc(this->mbuf_pool);
    }

    for (int i = 0; i < MAX_AGTR_COUNT+NORM_SLOT; ++i){
        this->slot_roundnum[i] = 0;
        this->slot_ready[i] = true;
        this->slot_dpdk_key[i] = 0;
    }
	
    // Initialize all ports.
    if (port_init(this->mbuf_recv_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu8 "\n", 0);
    // flow_init();

    // setup packet header fields
    switch (host)
    {
    case 0:
#ifdef SW_PS
#ifdef USE_TIMI
        this->s_addr = TIMI_ENS2F1;
        this->d_addr = RUMI_ENP132S0F1;
#else
        this->s_addr = LAMBDA_ENS8F0;
        this->d_addr = RUMI_ENP132S0F1;

        // this->s_addr = LAMBDA_ENS8F1;
        // this->d_addr = RUMI_ENP132S0F0;

        // this->s_addr = TIMI_ENS2F0;
        // this->d_addr = RUMI_ENP132S0F1;
#endif
#else
#ifdef USE_TIMI
        this->s_addr = TIMI_ENS2F1;
        this->d_addr = RUMI_ENP132S0F1;
#else
        this->s_addr = LAMBDA_ENS8F0;
        this->d_addr = RUMI_ENP132S0F1;
#endif
#endif
        // this->d_addr = TIMI_ENS2F1;
        printf("set src and dst for case 0\n");
        break;
    case 1:
        this->s_addr = RUMI_ENP132S0F1;
        this->d_addr = RUMI_ENP132S0F1;
        printf("set src and dst for case 1\n");
        break;

    default:
        break;
    }
#ifdef SWITCH_TIMING
    this->switch_process_time = 0;
    this->switch_timing_count = 0;
#endif

#ifdef SW_PS
    this->ether_type = htons(0x0c00);
#else
    this->ether_type = htons(0x0a00);
#endif
    printf("Done initializing the DPDK manager!\n");

    this->should_shutdown = false;
    this->expected_key = 0;
    this->in_flight_count = 0;
    this->congest_count = 0;

    printf("main DPDK thread at lcore %u on CPU %d\n", rte_lcore_id(), sched_getcpu());
    
    /* Launches the receiving function on each lcore. */
    struct rx_thread_args rx_args = {0};
    for (unsigned lcore_id = 1; lcore_id <= RX_RING; ++lcore_id) rte_eal_remote_launch(this->RxThread, &rx_args, lcore_id);
    rte_eal_remote_launch(this->TxThread, &rx_args, RX_RING+1);
    // rte_eal_remote_launch(this->TxThread, &rx_args, RX_RING+2);
    // rte_eal_remote_launch(this->RxThread, &rx_args, 1);
    // rte_eal_remote_launch(this->RxThread, &rx_args, 2);
    // rte_eal_remote_launch(this->RxThread, &rx_args, 3);
    // rte_eal_remote_launch(this->TxThread, &rx_args, 4);
    
}

uint64_t DPDKManager::GetNewKey(){
    std::lock_guard<std::mutex> lock(_DPDKKey_mutex);
    return this->dpdkKey++;
}

void DPDKManager::Send(uint64_t key, char* data, int len, int cmd, std::string tensor_name, uint16_t agtr_index){
    struct rte_mbuf* pkt[BURST_SIZE];
    struct TestMessage* msg;
    struct rte_ether_hdr *eth_hdr;
#ifdef IP_HDR
    struct rte_ipv4_hdr  *ip4_hdr;
#endif
    uint16_t send_buf_index;
    uint32_t round_num;
    // uint16_t round_num;

    for (int pkt_idx = 0; pkt_idx < BURST_SIZE; ++pkt_idx){
        struct TestMessage msg_body;
        send_buf_index = key % WORKER_SEND_NUM_MBUFS;
        round_num = slot_roundnum[agtr_index];

        if(use_compression) {
            assemble_dpdk_p4ml_hdrs(&(msg_body.p4ml_hdrs), host, (uint8_t)NUM_WORKER, round_num, data, key, len);
        }
        else{
            assemble_normal_hdrs(&(msg_body.p4ml_hdrs), host, (uint8_t)NUM_WORKER, round_num, data, key, len);
        }
        msg_body.key = key;
        msg_body.cmd = cmd;
        msg_body.len = len;
#ifdef SW_PS
        msg_body.shutdown = false;
#endif
        
        pkt[pkt_idx] = send_pkt_ptrs[send_buf_index];
        eth_hdr = rte_pktmbuf_mtod(pkt[pkt_idx], struct rte_ether_hdr*);
        eth_hdr->dst_addr = d_addr;
        eth_hdr->src_addr = s_addr;
        eth_hdr->ether_type = ether_type;
#ifdef IP_HDR
        // add ip header
	    ip4_hdr = rte_pktmbuf_mtod_offset(pkt[pkt_idx], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
		ip4_hdr->src_addr = htonl((uint32_t)agtr_index);
        msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) 
                                    + sizeof(struct rte_ipv4_hdr));
#else
        msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr));
#endif
        *msg = msg_body;
#ifdef IP_HDR
        int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
        int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
        // _print_mutex.lock();
        pkt[pkt_idx]->data_len = pkt_size;
        pkt[pkt_idx]->pkt_len = pkt_size;

        // printf("SENT Key: %" PRIu64 " for tensor %s with roundnum %hu of len %d\n", msg->key, tensor_name.c_str(), round_num, msg->len);
        // // printf("worker_bitmap: %d, num_worker: %d, worker_id: %d, round_num: %d\n", 
        // //         ntohl(msg->p4ml_hdrs.bitmap), msg->p4ml_hdrs.num_worker, ntohs(msg->p4ml_hdrs.agtr), ntohs(msg->p4ml_hdrs.roundNum));
        // printf("pkt len: %d, sample of data: \n", pkt[pkt_idx]->pkt_len);
        // for(int i = 0; i < 10; ++i) printf("%.6f ", (msg->data)[i]);
        // printf("\n");
// #ifdef USE_UINT8
//         for(int i = 0; i < 10; ++i) printf("%hhu ", ((uint8_t*)(msg->p4ml_hdrs.vector))[i]);
// #else
//         for(int i = 0; i < 10; ++i) printf("%d ", ntohl(msg->p4ml_hdrs.vector[i]));
// #endif
//         printf("\n");

        // update the expected roundnum of the used aggregator slot
        // slot_roundnum[agtr_index]++;
        // _print_mutex.unlock();
    }
    
    uint16_t num_tx = rte_eth_tx_burst(0, 0, pkt, BURST_SIZE); // returns the number of packets transmitted
    if(num_tx < 1) printf("WARNING: rte_eth_tx_burst sent zero packet!\n");
#ifdef IP_HDR
    if(round_num != ntohl(((struct TestMessage*)(rte_pktmbuf_mtod(pkt[0],char*) + sizeof(struct rte_ether_hdr) 
                                                 + sizeof(struct rte_ipv4_hdr)))->p4ml_hdrs.roundNum)){
        printf("expected to send round_num %u but sent %u\n", round_num,
          ntohl(((struct TestMessage*)(rte_pktmbuf_mtod(pkt[0],char*) + sizeof(struct rte_ether_hdr)))->p4ml_hdrs.roundNum));
    }
    // if(round_num != ntohs(((struct TestMessage*)(rte_pktmbuf_mtod(pkt[0],char*) + sizeof(struct rte_ether_hdr) 
    //                                              + sizeof(struct rte_ipv4_hdr)))->p4ml_hdrs.roundNum)){
    //     printf("expected to send round_num %hu but sent %hu\n", round_num,
    //       ntohs(((struct TestMessage*)(rte_pktmbuf_mtod(pkt[0],char*) + sizeof(struct rte_ether_hdr)))->p4ml_hdrs.roundNum));
    // }
#endif
    // printf("SENT %hu packets, key: %" PRIu64 "\n", num_tx, 
    //       ((struct TestMessage*)((rte_pktmbuf_mtod(pkt[0],char*)) + sizeof(struct rte_ether_hdr)))->key);
}

void DPDKManager::DPDKPushPull(uint64_t tensor_key, char* data, int len, int cmd, std::string tensor_name){
    // _print_mutex.lock();
    // printf("DPDKPushPull tensor %s of length %d\n", tensor_name.c_str(), len);
    // printf("sample of DPDKPushPull Data: \n");
    // for(int i = 0; i < len; ++i){
    //     printf("%hhu ", ((uint8_t*)data)[i]);
    //     if(i >= 10) break;
    // }
    // printf("\n");
    // _print_mutex.unlock();

    // If this tensor is not a gradient tensor, we need to treat it specially
    if (tensor_name[7] != 'G') {
        cmd = TENSOR_INIT;
        printf("DPDKPushPull special tensor %s of length %d\n", tensor_name.c_str(), len);
    }

    size_t bound;
    if(use_compression) bound = DPDK_ELEMENT_NUM; // number of bytes to pack into one packet
    else bound = MAX_ENTRIES_PER_PACKET;
    size_t accumulated = 0; // number of floats already packed
    int send_calls = 1; // number of packets has been pushed
    // _part_finish_mutex.lock();
    int mutex_idx = curr_mutex_idx;
    curr_mutex_idx = (curr_mutex_idx + 1) % DPDK_KEY_TOTAL;
    tensor_key_part_total[mutex_idx] = (len + bound - 1) / bound;
    tensor_key_part_finish[mutex_idx] = 0;
    // _part_finish_mutex.unlock();
    // printf("DPDKPushPull tensor %s of length %d and %d chunks\n", tensor_name.c_str(), len, tensor_key_part_total[tensor_key]);
    while(accumulated < len){
        uint64_t key = GetNewKey() % DPDK_KEY_TOTAL;
        dpdk_key_to_tensor_key[key] = tensor_key;
        dpdk_key_to_mutex_idx[key] = mutex_idx;
        uint32_t job_len = std::min(bound, len-accumulated);

        if (cmd == TENSOR_COMM) {
            cpubuff_table[key] = data + accumulated; //TODO: make sure that this is at the right beginning
#ifdef NEW_INCA
            // shift values here
            uint8_t* index_data = (uint8_t*)(data + accumulated);
            for (int i = 0; i < job_len/2; i++) {
                index_data[i] = index_data[2*i]*16 + index_data[2*i+1];
            }
            if (job_len % 2) {
                index_data[job_len/2] = index_data[job_len-1]*16;
            }
#endif
        }
        
        uint16_t agtr_index = key % MAX_AGTR_COUNT;
        struct Job job = {key, data + accumulated, job_len, cmd, tensor_name, agtr_index};
        // struct Job job = {key, data + accumulated * sizeof(float), std::min(bound, len-accumulated), cmd, tensor_name, agtr_index};
        
        _pushpull_queue_mutex.lock();
        pushpulljobQueue.push(job);
        _pushpull_queue_mutex.unlock();

        accumulated += bound;
    }
    
}

#ifdef SW_PS
void DPDKManager::ShutdownSignal(){
    struct rte_mbuf* pkt[BURST_SIZE];
#ifdef NEW_INCA
    struct IndexMessage* msg;
    struct IndexMessage msg_body;
#else
    struct TestMessage* msg;
    struct TestMessage msg_body;
#endif
    struct rte_ether_hdr *eth_hdr;

    msg_body.shutdown = true;

    pkt[0] = send_pkt_ptrs[0];
    eth_hdr = rte_pktmbuf_mtod(pkt[0], struct rte_ether_hdr*);
    eth_hdr->dst_addr = d_addr;
    eth_hdr->src_addr = s_addr;
    eth_hdr->ether_type = ether_type;
#ifdef IP_HDR
#ifdef NEW_INCA
    msg = (struct IndexMessage*)(rte_pktmbuf_mtod(pkt[0], char *) + sizeof(struct rte_ether_hdr)
                                + sizeof(struct rte_ipv4_hdr));
    int pkt_size = sizeof(struct IndexMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
    msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[0], char *) + sizeof(struct rte_ether_hdr)
                                + sizeof(struct rte_ipv4_hdr));
    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#endif
#else
#ifdef NEW_INCA
    msg = (struct IndexMessage*)(rte_pktmbuf_mtod(pkt[0], char *) + sizeof(struct rte_ether_hdr));
    int pkt_size = sizeof(struct IndexMessage) + sizeof(struct rte_ether_hdr);
#else
    msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[0], char *) + sizeof(struct rte_ether_hdr));
    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
#endif // IP_HDR
    *msg = msg_body;

    pkt[0]->data_len = pkt_size;
    pkt[0]->pkt_len = pkt_size;
    
    uint16_t num_tx = rte_eth_tx_burst(0, 0, pkt, BURST_SIZE); // returns the number of packets transmitted
    if(num_tx) printf("Sent shutdown signal to the Software PS\n");
}
#endif

float DPDKManager::CommunicateNorm(uint64_t key, float max_norm){
    struct rte_mbuf* pkt[BURST_SIZE];
#ifdef NEW_INCA
    struct IndexMessage* msg;
#else
    struct TestMessage* msg;
#endif
    struct rte_ether_hdr *eth_hdr;

    for (int pkt_idx = 0; pkt_idx < BURST_SIZE; ++pkt_idx){
#ifdef NEW_INCA
        struct IndexMessage msg_body;
#else
        struct TestMessage msg_body;
#endif
        uint16_t agtr_index = MAX_AGTR_COUNT + key % NORM_SLOT;
        uint16_t send_buf_index = WORKER_SEND_NUM_MBUFS + key % NORM_SLOT; // TODO: is this large enough
        uint32_t round_num = slot_roundnum[agtr_index];
        // uint16_t round_num = slot_roundnum[agtr_index];

        assemble_norm_pkt(&(msg_body.p4ml_hdrs), host, (uint8_t)NUM_WORKER, round_num, key, max_norm);
        msg_body.key = key;
        msg_body.cmd = TENSOR_NORM;
#ifdef SW_PS
        msg_body.shutdown = false;
#endif

        pkt[pkt_idx] = send_pkt_ptrs[send_buf_index];
        eth_hdr = rte_pktmbuf_mtod(pkt[pkt_idx], struct rte_ether_hdr*);
        eth_hdr->dst_addr = d_addr;
        eth_hdr->src_addr = s_addr;
        eth_hdr->ether_type = ether_type;
#ifdef IP_HDR
#ifdef NEW_INCA
        msg = (struct IndexMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) 
                                    + sizeof(struct rte_ipv4_hdr));
        int pkt_size = sizeof(struct IndexMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
        msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) 
                                    + sizeof(struct rte_ipv4_hdr));
        int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#endif
#else
#ifdef NEW_INCA
        msg = (struct IndexMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr));
        int pkt_size = sizeof(struct IndexMessage) + sizeof(struct rte_ether_hdr);
#else
        msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr));
        int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
#endif // IP_HDR
        *msg = msg_body;

        // _print_mutex.lock();
        pkt[pkt_idx]->data_len = pkt_size;
        pkt[pkt_idx]->pkt_len = pkt_size;
        
        norm_ready[key] = false;
#ifdef NORM_TIMING
        _norm_table_mutex.lock();
        norm_comm_count++;
        norm_start_time[key] = std::chrono::high_resolution_clock::now();
        _norm_table_mutex.unlock();
#endif

        // _print_mutex.unlock();
    }
    // printf("Send Norm Key: %" PRIu64 ", norm: %.6f\n", key, max_norm);
    
    uint16_t num_tx = rte_eth_tx_burst(0, 0, pkt, BURST_SIZE); // returns the number of packets transmitted

    // start busy waiting for norm communication to finish
    while(!norm_ready[key]) usleep(1);
    return norm_value[key];
}

/* This function returns an element in the finishKey queue. 
 */
int64_t DPDKManager::PollFinishKey(){
    // _key_queue_mutex.lock();
    std::lock_guard<std::mutex> lock(_key_queue_mutex);
    if(finishKeys.empty()){
        // _key_queue_mutex.unlock();
        return -1;
    } 
    uint64_t temp_key = finishKeys.front();
    finishKeys.pop();
    // _key_queue_mutex.unlock();
    return temp_key;
}

/* This function resets all agtr slots and all tasks after a timeout
 */
void DPDKManager::Reset(){

    _pushpull_queue_mutex.lock();
    // if(!pushpulljobQueue.empty()){
    //     // printf("Ignore DPDK reset call when pushpull queue is not empty\n");
    //     _pushpull_queue_mutex.unlock();
    //     return;
    // }
    // _pushpull_queue_mutex.unlock();

    for (int i = 0; i < MAX_AGTR_COUNT+NORM_SLOT; ++i){
        _slot_ready_mutex[i].lock();
        if (!slot_ready[i]){
            // this slot is occupied, free it and set round_num accordingly
            slot_roundnum[i] ++;
            slot_ready[i] = true;

            uint64_t tensor_key = dpdk_key_to_tensor_key[slot_dpdk_key[i]];
            // _part_finish_mutex.lock();
            int mutex_id = dpdk_key_to_mutex_idx[slot_dpdk_key[i]];
            mutex_pool[mutex_id].lock();
            tensor_key_part_finish[mutex_id]++;
            if(tensor_key_part_finish[mutex_id] == tensor_key_part_total[mutex_id]){
                _key_queue_mutex.lock();
                finishKeys.push(tensor_key);
                printf("Key %" PRIu64 " pushed through reset\n", tensor_key);
                _key_queue_mutex.unlock();
            }
            mutex_pool[mutex_id].unlock();
            // _part_finish_mutex.unlock();


            slot_dpdk_key[i] = 0;

            // _in_flight_mutex.lock();
            // in_flight_count--;
            // _in_flight_mutex.unlock();
        }
        _slot_ready_mutex[i].unlock();

    }
    _pushpull_queue_mutex.unlock();
}

int64_t DPDKManager::GetFinishKey(){
    if (!finishQueue.empty()) {
        std::lock_guard<std::mutex> lock(_finish_queue_mutex);
        Job* finish_job = finishQueue.front();
        uint64_t tmp_key = finish_job->key;
        // dequantizeAVX2((char*)finish_job->int_data, finish_job->len);
        finishQueue.pop();
        delete finish_job;
        return tmp_key;
    } else {
        return -1;
    }
}

int DPDKManager::Shutdown(){
#ifdef SW_PS
    /* tell the software PS to shutdown */
    ShutdownSignal();
#endif
#ifdef NORM_TIMING
    printf("Average norm communication: %.8f\n", norm_overall_time / norm_comm_count);
#endif
#ifdef SWITCH_TIMING
    printf("Average switch processing time (ns): %.8f\n", (double)switch_process_time / switch_timing_count);
#endif
    printf("slept %d times to allow rx thread to catch up\n", congest_count);
    this->should_shutdown = true;
    rte_eal_mp_wait_lcore();
    for (int pkt_idx = 0; pkt_idx < WORKER_SEND_NUM_MBUFS+NORM_SLOT; ++pkt_idx){
        rte_pktmbuf_free(send_pkt_ptrs[pkt_idx]); // free sending resources
    }
    printf("DPDK manager shutdown\n");
    return 0;
}
