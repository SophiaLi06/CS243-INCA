#include "ParameterServer.h"

std::mutex __print_mutex;
std::mutex _init_mutex;
int num_thread;
int print_count = 0;
int appID;

//bool is_completed_dpdk_key[1024000] = {0};

//int next_agtr[MAX_AGTR_COUNT] = {-1};
//HashTable* hash_table;

// int packet_full_count = 0;
// int packet_partial_count = 0;
// int packet_all_forward_count = 0;
// int packet_partial_total_count = 0;
#define SEND_REPLY
// #define PRINT_SAMPLE
#define MAX_MEASUREMENT_KEY 12000
// #define USE_TX_THREAD
int full_packet_count[MAX_MEASUREMENT_KEY][16518] = { 0 };
int resend_packet_count[MAX_MEASUREMENT_KEY][16518] = { 0 };
#ifdef USE_TX_THREAD
std::queue<PSJob> pushpulljobQueue;
std::mutex _pushpull_queue_mutex;
#endif
volatile bool should_shutdown = false;

int port_init(struct rte_mempool *mbuf_recv_pool){
    struct rte_eth_txmode txmode;
    struct rte_eth_rxmode rxmode;
    rxmode.mtu = RTE_ETHER_MAX_LEN;
    struct rte_eth_conf port_conf = {};
    struct rte_eth_rxconf rxq_conf;
    rxq_conf.rx_drop_en = 0; //Don't drop packets if no descriptors are available.
    port_conf.rxmode = rxmode;
    port_conf.txmode = txmode;
    // struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = PS_RX_RING, tx_rings = 1;
    struct rte_eth_dev_info dev_info;
	int retval;
	uint16_t q;

    rte_eth_dev_info_get(0, &dev_info);
	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE){
        printf("Fast free\n");
        port_conf.txmode.offloads |=
			DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    }

	/* Init port 0 and setup rx and tx queues for it. */
	retval = rte_eth_dev_configure(0, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(0, q, RX_RING_SIZE,
				rte_eth_dev_socket_id(0), &rxq_conf, mbuf_recv_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(0, q, TX_RING_SIZE,
				rte_eth_dev_socket_id(0), NULL);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(0);
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
    eth_spec.type = htons(0x0d00+rx_q);
    eth_mask.type = 0xffff;
    // if(rx_q == 0) eth_spec.src = TIMI_ENS2F1;
    // else eth_spec.src = LAMBDA_ENS8F0;
    // eth_mask.src = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
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
    const uint16_t rx_rings = PS_RX_RING;
    struct rte_flow_error error;
    struct rte_flow *flow;
    for (uint16_t loop = 0; loop < rx_rings; loop++) {
        flow = generate_rule(0, loop, &error);
        if (!flow)
            rte_exit(EXIT_FAILURE, "Flow can't be created %d message: %s\n", error.type, error.message ? error.message : "(no stated reason)");
    }
}

/*
 * This function adds a worker's MAC address into a vector of the PS.
 * Note that it is currently assuming a single-tenant scenario.
 * If we want multiple tenants, we need a map between tensor key and worker vector.
 */
bool worker_registered(struct rte_ether_addr worker_addr){
    // declare iterator
    std::vector<struct rte_ether_addr>::iterator iter;
    bool match;

    for(iter = worker_addrs.begin(); iter != worker_addrs.end(); ++iter){
        match = true;
        for(int i = 0; i < RTE_ETHER_ADDR_LEN; ++i){
            if ((*iter).addr_bytes[i] != worker_addr.addr_bytes[i]){
                match = false;
                break;
            }
        }
        if(match) return true;
    }
    // add worker MAC address
    worker_addrs.push_back(worker_addr);
    return false;
}

void init_tensor_response(struct rte_mempool *mbuf_pool, uint64_t key, struct rte_mbuf **send_pkt, struct rte_ether_addr d_addr){
    // setup packet header fields
    struct rte_ether_addr s_addr = {SERVER_SRC_MAC};
    if (worker_registered(d_addr)) {
        printf("Worker with MAC %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " already registered\n",
                d_addr.addr_bytes[0],d_addr.addr_bytes[1],
                d_addr.addr_bytes[2],d_addr.addr_bytes[3],
                d_addr.addr_bytes[4],d_addr.addr_bytes[5]);
    }
    //struct rte_ether_addr d_addr = {SERVER_DST_MAC};
    uint16_t ether_type = htons(0x0a01);

    struct rte_ether_hdr *send_eth_hdr;
    struct InitResponse *send_msg;
    struct InitResponse ack_obj = {.key=key, .data={'A', 'C', 'K', ' ', 'i', 'n', 'i', 't'}};
    //ack_obj.key = key;
	//ack_obj.data = {{'A', 'C', 'K', ' ', 'i', 'n', 'i', 't'}};
    send_msg = (struct InitResponse *)(rte_pktmbuf_mtod(send_pkt[0], char *) + sizeof(struct rte_ether_hdr));
    *send_msg = ack_obj;
    send_eth_hdr = rte_pktmbuf_mtod(send_pkt[0], struct rte_ether_hdr *);
    send_eth_hdr->dst_addr = d_addr;
    send_eth_hdr->src_addr = s_addr;
    send_eth_hdr->ether_type = ether_type;

    int pkt_size = sizeof(struct InitResponse) + sizeof(struct rte_ether_hdr);
    send_pkt[0]->data_len = pkt_size;
    send_pkt[0]->pkt_len = pkt_size;

    uint16_t num_tx = rte_eth_tx_burst(0, 0, send_pkt, 1);
	printf("Sent %d init tensor response packets\n", num_tx);
    // cleanup resources for sending ACK
	rte_pktmbuf_free(send_pkt[0]);

}

#ifndef USE_TX_THREAD
#ifdef NEW_INCA
void broadcast_packet(uint32_t bitmap, struct IndexMessage* msg)
#else
void broadcast_packet(uint32_t bitmap, struct TestMessage* msg)
#endif
{
    // setup packet header fields
    // struct rte_ether_addr s_addr;
    // if(msg->key % 2) s_addr = RUMI_ENP132S0F0; //test with changing the src address
    // else s_addr = RUMI_ENP132S0F1; //test with changing the src address
    // struct rte_mbuf *pkt[PS_WORKERS];
    // for (uint32_t i = 0; i < PS_WORKERS; ++i){
    //     pkt[i] = rte_pktmbuf_alloc(mbuf_pool);
    // }
    for (uint32_t worker_id = 0; worker_id < PS_WORKERS; ++worker_id){

        struct rte_mbuf *send_pkt = send_pkt_ptrs[worker_id][ntohs(msg->p4ml_hdrs.agtr)];
        if(!send_pkt) {
		    printf("ERROR: failed to alloc mbuf for send_pkt!\n");
		    return;
        }
        struct rte_ether_addr s_addr = RUMI_ENP132S0F1;
        // struct rte_ether_addr s_addr = RUMI_ENP132S0F0;
        struct rte_ether_addr d_addr = worker_addrs[worker_id];
        uint16_t ether_type = htons(0x0d00+(uint16_t)((msg->p4ml_hdrs.agtr)%RX_RING));
#ifdef PRINT_SAMPLE
        printf("Key: %" PRIu64 "\n", msg->key);
#endif
        struct rte_ether_hdr *send_eth_hdr;
#ifdef IP_HDR
        struct rte_ipv4_hdr  *send_ip4_hdr;
#endif
        struct TestMessage *send_msg;
        struct TestMessage msg_body;
        msg_body.cmd = msg->cmd;
        msg_body.key = msg->key;
        msg_body.len = msg->len;

        uint16_t agtr_idx = ntohs(msg->p4ml_hdrs.agtr);

        uint32_t reply_data[MAX_ENTRIES_PER_PACKET];
        for (uint32_t i = 0; i < MAX_ENTRIES_PER_PACKET; ++i) 
            reply_data[i] = data_slots[i][agtr_idx];

        assemble_reply_p4ml_hdrs(&(msg_body.p4ml_hdrs), 0, NUM_WORKER, ntohl(msg->p4ml_hdrs.roundNum), reply_data, msg->key, 
                                 norm_info[ntohs(msg->p4ml_hdrs.agtr)], ntohs(msg->p4ml_hdrs.agtr));
        // assemble_reply_p4ml_hdrs(&(msg_body.p4ml_hdrs), 0, NUM_WORKER, ntohs(msg->p4ml_hdrs.roundNum), reply_data, msg->key, 
        //                          norm_info[ntohs(msg->p4ml_hdrs.agtr)], ntohs(msg->p4ml_hdrs.agtr));
#ifdef IP_HDR            
        send_msg = (struct TestMessage *)(rte_pktmbuf_mtod(send_pkt, char *) + sizeof(struct rte_ether_hdr)
                                          + sizeof(struct rte_ipv4_hdr));
#else
        send_msg = (struct TestMessage *)(rte_pktmbuf_mtod(send_pkt, char *) + sizeof(struct rte_ether_hdr));
#endif
        *send_msg = msg_body;
        send_eth_hdr = rte_pktmbuf_mtod(send_pkt, struct rte_ether_hdr *);
        send_eth_hdr->dst_addr = d_addr;
        send_eth_hdr->src_addr = s_addr;
        send_eth_hdr->ether_type = ether_type;
#ifdef IP_HDR
        send_ip4_hdr = rte_pktmbuf_mtod_offset(send_pkt, struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
    	send_ip4_hdr->src_addr = htonl((uint32_t) 0x64000001);
        send_ip4_hdr->dst_addr = htonl((uint32_t) ((msg->p4ml_hdrs.agtr)%RX_RING + 0x64000001));

        int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
        int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
        send_pkt->data_len = pkt_size;
        send_pkt->pkt_len = pkt_size;

        uint16_t num_tx = rte_eth_tx_burst(0, 0, &send_pkt, 1);
	    // printf("Sent %d packets from %p \n", num_tx, send_pkt);

        /* print a sample of sent information and data */
        // printf("worker_bitmap: %d, num_worker: %d, round_num: %d, max_info: %d, norm_info: %d, type_and_data_index: %d, agtr: %d, \n", 
        //         ntohl(send_msg->p4ml_hdrs.bitmap), send_msg->p4ml_hdrs.num_worker, ntohs(send_msg->p4ml_hdrs.roundNum),
        //         ntohl(send_msg->p4ml_hdrs.max_info), ntohl(send_msg->p4ml_hdrs.norm_info), send_msg->p4ml_hdrs.type_and_index, 
        //         ntohs(send_msg->p4ml_hdrs.agtr));
        // for(int j=0;j<10;j++)
        //     printf("%hhu ",((uint8_t*)(send_msg->p4ml_hdrs.vector))[j]);
        // printf("\n");
    
	    rte_pktmbuf_free(send_pkt);
    }
}
#else
void broadcast_packet(uint64_t key, int cmd, int len, uint16_t agtr, uint32_t roundNum, struct rte_mbuf *send_pkt){
// void broadcast_packet(uint64_t key, int cmd, int len, uint16_t agtr, uint16_t roundNum, struct rte_mbuf *send_pkt){
    // setup packet header fields
    struct rte_ether_addr s_addr = RUMI_ENP132S0F1;
    struct rte_ether_addr d_addr = LAMBDA_ENS8F0;
    // struct rte_ether_addr s_addr = RUMI_ENP132S0F1;
    // struct rte_ether_addr d_addr = TIMI_ENS2F0;
    uint16_t ether_type = htons(0x0c00+(uint16_t)((agtr)%RX_RING));
#ifdef PRINT_SAMPLE
    printf("Key: %" PRIu64 "\n", key);
#endif
    struct rte_ether_hdr *send_eth_hdr;
#ifdef IP_HDR
    struct rte_ipv4_hdr  *send_ip4_hdr;
#endif
    struct TestMessage *send_msg;
    struct TestMessage msg_body;
    msg_body.cmd = cmd;
    msg_body.key = key;
    msg_body.len = len;
    uint32_t reply_data[MAX_ENTRIES_PER_PACKET];
    for (uint32_t i = 0; i < MAX_ENTRIES_PER_PACKET; ++i) reply_data[i] = data_slots[i][ntohs(agtr)];

    assemble_reply_p4ml_hdrs(&(msg_body.p4ml_hdrs), 0, NUM_WORKER, ntohl(roundNum), reply_data, key, 
                             norm_info[ntohs(agtr)], ntohs(agtr));
    // assemble_reply_p4ml_hdrs(&(msg_body.p4ml_hdrs), 0, NUM_WORKER, ntohs(roundNum), reply_data, key, 
    //                          norm_info[ntohs(agtr)], ntohs(agtr));

#ifdef IP_HDR
    send_msg = (struct TestMessage *)(rte_pktmbuf_mtod(send_pkt, char *) + sizeof(struct rte_ether_hdr)
                                      + sizeof(struct rte_ipv4_hdr));
#else
    send_msg = (struct TestMessage *)(rte_pktmbuf_mtod(send_pkt, char *) + sizeof(struct rte_ether_hdr));
#endif
    *send_msg = msg_body;
    send_eth_hdr = rte_pktmbuf_mtod(send_pkt, struct rte_ether_hdr *);
    send_eth_hdr->dst_addr = d_addr;
    send_eth_hdr->src_addr = s_addr;
    send_eth_hdr->ether_type = ether_type;
#ifdef IP_HDR
    send_ip4_hdr = rte_pktmbuf_mtod_offset(send_pkt, struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
	send_ip4_hdr->src_addr = htonl((uint32_t) 0x64000001);
    send_ip4_hdr->dst_addr = htonl((uint32_t) (agtr%RX_RING + 0x64000001));

    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
    send_pkt->data_len = pkt_size;
    send_pkt->pkt_len = pkt_size;

    uint16_t num_tx = rte_eth_tx_burst(0, 0, &send_pkt, 1);
	// printf("Sent %d packets from %p \n", num_tx, send_pkt);

    /* print a sample of sent information and data */
    // printf("worker_bitmap: %d, num_worker: %d, round_num: %d, max_info: %d, norm_info: %d, type_and_data_index: %d, agtr: %d, \n", 
    //         ntohl(send_msg->p4ml_hdrs.bitmap), send_msg->p4ml_hdrs.num_worker, ntohs(send_msg->p4ml_hdrs.roundNum),
    //         ntohl(send_msg->p4ml_hdrs.max_info), ntohl(send_msg->p4ml_hdrs.norm_info), send_msg->p4ml_hdrs.type_and_index, 
    //         ntohs(send_msg->p4ml_hdrs.agtr));
    // for(int j=0;j<10;j++)
    //     printf("%hhu ",((uint8_t*)(send_msg->p4ml_hdrs.vector))[j]);
    // printf("\n");
    
	// rte_pktmbuf_free(send_pkt);
}

int TxThread(void* thread_args){
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
            struct PSJob push_job = pushpulljobQueue.front();
            broadcast_packet(push_job.key, push_job.cmd, push_job.len, push_job.agtr, push_job.roundNum, push_job.send_pkt);
            // _slot_ready_mutex.lock();
            // if (slot_ready[push_job.agtr_index]){
            //     slot_ready[push_job.agtr_index] = false; // this slot is now being used
            //     slot_dpdk_key[push_job.agtr_index] = push_job.key;
            //     // send the packet
            //     Send(push_job.key, push_job.data, push_job.len, push_job.cmd, push_job.tensor_name, push_job.agtr_index);
            //     _in_flight_mutex.lock();
            //     in_flight_count++;
            //     _in_flight_mutex.unlock();
            //     _slot_ready_mutex.unlock();
            // }
            // else{
            //     _slot_ready_mutex.unlock();
            //     // push the job back into the queue
            //     pushpulljobQueue.push(push_job);
            // }
            // _slot_ready_mutex.unlock();
            pushpulljobQueue.pop();
            _pushpull_queue_mutex.unlock();
        }
    }
    return 0;
}
#endif // USE_TX_THREAD

#ifdef NEW_INCA
void process_data(struct IndexMessage* msg)
#else
void process_data(struct TestMessage* msg)
#endif
{
#ifdef NEW_INCA
    // new inca table
    uint8_t recv_table[16] = { 0,  3,  5,  7,  9, 11, 13, 14, 16, 17, 19, 21, 23, 25, 27, 30};
    uint32_t msg_actual_data[MAX_ENTRIES_PER_PACKET];

    uint16_t agtr_idx = ntohs(msg->p4ml_hdrs.agtr);
    for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
        uint16_t idx = ntohs(msg->p4ml_hdrs.vector[i]);
        uint32_t entry_data = 0;
        for (int j = 0; j < 4; ++j){
            entry_data = (entry_data << 8) | recv_table[idx % 16];
            idx /= 16;
        }
        msg_actual_data[i] = entry_data;
    }
    if(receive_count[agtr_idx] == 1){
        // the first packet for a given slot of a given round
        for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
            data_slots[i][agtr_idx] = msg_actual_data[i];
        }
    }
    else{
        for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
            data_slots[i][agtr_idx] += msg_actual_data[i];
        }
    }
#else
    uint16_t agtr_idx = ntohs(msg->p4ml_hdrs.agtr);
    if(receive_count[agtr_idx] == 1){
        // the first packet for a given slot of a given round
        for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
            data_slots[i][agtr_idx] = (msg->p4ml_hdrs.vector[i]);
        }
    }
    else{
        for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
            data_slots[i][agtr_idx] += (msg->p4ml_hdrs.vector[i]);
        }
    }

    // uint8_t recv_table[16] = { 0,  3,  5,  7,  9, 11, 13, 14, 16, 17, 19, 21, 23, 25, 27, 30};
    // uint8_t* msg_data = (uint8_t*)(msg->p4ml_hdrs.vector);
    // uint16_t agtr_idx = ntohs(msg->p4ml_hdrs.agtr);
    // for (int i = 0; i < MAX_ENTRIES_PER_PACKET * 4; ++i){
    //     msg_data[i] = recv_table[msg_data[i]];
    // }
    // if(receive_count[agtr_idx] == 1){
    //     // the first packet for a given slot of a given round
    //     for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
    //         data_slots[i][agtr_idx] = (msg->p4ml_hdrs.vector[i]);
    //     }
    // }
    // else{
    //     for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
    //         data_slots[i][agtr_idx] += (msg->p4ml_hdrs.vector[i]);
    //     }
    // }

#endif
}

// void test_receive_packet_loop(int thread_id, struct rte_mempool *mbuf_pool) {
int RxThread(void* thread_args) {
    unsigned lcore_id;
	lcore_id = rte_lcore_id();
    printf("listening DPDK thread at lcore %u on CPU %d, thread id = %d\n", lcore_id, sched_getcpu(), pthread_self());

    int received=0;
    // bool should_shutdown = false;
#ifdef USE_EXPECTED_KEY
    uint64_t expected_key = 0;
#endif

    while(!should_shutdown){
#ifdef USE_EXPECTED_KEY
        struct rte_mbuf * pkt[std::max(BURST_SIZE, 4)];
#else
		struct rte_mbuf * pkt[std::max(BURST_SIZE, 1280)];
#endif
#ifdef SEND_REPLY
		// struct rte_mbuf *send_pkt;
#endif
       		
		int i;
        /* retrieve received packets */
		// (port_id, queue_id, rte_mbuf** rx_pkts(points to the mbuf array of the received packets)
		//  , maximum_num_pkts)
		for(;;){
#ifdef USE_EXPECTED_KEY
            uint16_t num_rx = rte_eth_rx_burst(0, lcore_id-1, pkt, std::max(BURST_SIZE, 4));
#else
			uint16_t num_rx = rte_eth_rx_burst(0, lcore_id-1, pkt, std::max(BURST_SIZE, 1280));
#endif
			if(num_rx == 0)
			{
				// no packet retrieved, skip
				continue;
			}
#ifdef NEW_INCA
            struct IndexMessage* msg; 
#else
            struct TestMessage* msg; 
#endif
			struct rte_ether_hdr * eth_hdr;

			// print received data
			for(i=0;i<num_rx;i++){
				// (mbuf, data type)
				eth_hdr = rte_pktmbuf_mtod(pkt[i],struct rte_ether_hdr*);
                // printf("Receive packet from MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                //         " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " : ",
                //         eth_hdr->src_addr.addr_bytes[0],eth_hdr->src_addr.addr_bytes[1],
                //         eth_hdr->src_addr.addr_bytes[2],eth_hdr->src_addr.addr_bytes[3],
                //         eth_hdr->src_addr.addr_bytes[4],eth_hdr->src_addr.addr_bytes[5]);
#ifdef IP_HDR
#ifdef NEW_INCA
                msg = (struct IndexMessage*)((rte_pktmbuf_mtod(pkt[i],char*)) + sizeof(struct rte_ether_hdr) 
                                             + sizeof(struct rte_ipv4_hdr));
#else
                msg = (struct TestMessage*)((rte_pktmbuf_mtod(pkt[i],char*)) + sizeof(struct rte_ether_hdr)
                                             + sizeof(struct rte_ipv4_hdr));
#endif
#else
#ifdef NEW_INCA
                msg = (struct IndexMessage*)((rte_pktmbuf_mtod(pkt[i],char*)) + sizeof(struct rte_ether_hdr));
#else
                msg = (struct TestMessage*)((rte_pktmbuf_mtod(pkt[i],char*)) + sizeof(struct rte_ether_hdr));
#endif
#endif // IP_HDR
#ifdef PRINT_SAMPLE
                printf("Key: %" PRIu64 ", CMD: %d, round_num: %u\n", msg->key, msg->cmd, ntohl(msg->p4ml_hdrs.roundNum));
                // printf("Key: %" PRIu64 ", CMD: %d, round_num: %hu\n", msg->key, msg->cmd, ntohs(msg->p4ml_hdrs.roundNum));
#endif
                if(msg->shutdown){
                    should_shutdown = true;
                    rte_pktmbuf_free(pkt[i]);
                    break;
                }
                if((msg->cmd != TENSOR_COMM && msg->cmd != TENSOR_NORM && msg->cmd != TENSOR_INIT) 
                    || (ntohs(eth_hdr->ether_type) != 0x0c00 + lcore_id - 1)){
                    rte_pktmbuf_free(pkt[i]);
                    continue;
                }
                
#ifdef USE_EXPECTED_KEY
                if(msg->cmd != TENSOR_NORM && msg->key < std::max(expected_key, (uint64_t)4) - 4 && msg->key != 0){ 
                    // This is an old packet that is retrieved again, disregard it
                    rte_pktmbuf_free(pkt[i]);
                    continue;
                }
#endif
                uint16_t agtr_idx = ntohs(msg->p4ml_hdrs.agtr);
                // send_pkt = send_pkt_ptrs[agtr_idx];
                // if(!send_pkt) {
			    //     printf("ERROR: failed to alloc mbuf for send_pkt!\n");
		        //     return -1;
                // }
#ifdef PRINT_SAMPLE
                /* print a sample of information and data */
                printf("worker_bitmap: %d, num_worker: %d, round_num: %d, max_info: %d, norm_info: %d, type_and_data_index: %d, agtr: %d, \n", 
                        ntohl(msg->p4ml_hdrs.bitmap), msg->p4ml_hdrs.num_worker, ntohl(msg->p4ml_hdrs.roundNum),
                        ntohl(msg->p4ml_hdrs.max_info), ntohl(msg->p4ml_hdrs.norm_info), msg->p4ml_hdrs.type_and_index, ntohs(msg->p4ml_hdrs.agtr));
                for(int j=0;j<10;j++)
                    printf("%hhu ",((uint8_t*)(msg->p4ml_hdrs.vector))[j]);
                printf("\n");
#endif
                /* checkroundNum*/
                if(ntohl(msg->p4ml_hdrs.roundNum) < roundNumbers[agtr_idx]){
                // if(ntohs(msg->p4ml_hdrs.roundNum) < roundNumbers[agtr_idx]){
                    printf("CMD: %d slot %hu expected roundNum %u but got %u\n", 
                            msg->cmd, agtr_idx, roundNumbers[agtr_idx], ntohl(msg->p4ml_hdrs.roundNum));
                    // TODO: send zero-ed packet
                }
                else{
                    if (ntohl(msg->p4ml_hdrs.roundNum) > roundNumbers[agtr_idx]){
                        roundNumbers[agtr_idx] = ntohl(msg->p4ml_hdrs.roundNum); // use this larger roundNum
                    // if (ntohs(msg->p4ml_hdrs.roundNum) > roundNumbers[agtr_idx]){
                    //     roundNumbers[agtr_idx] = ntohs(msg->p4ml_hdrs.roundNum); // use this larger roundNum
                        receive_count[agtr_idx] = 1; // reset the aggregator slot counter
                        norm_info[agtr_idx] = ntohl(msg->p4ml_hdrs.norm_info);
                    }
                    else{
                        if (receive_count[agtr_idx] == msg->p4ml_hdrs.num_worker){
                            // this aggregator has finished the last round
                            receive_count[agtr_idx] = 1;
                        }
                        else receive_count[agtr_idx]++;
                        if (norm_info[agtr_idx] < ntohl(msg->p4ml_hdrs.norm_info)){
                            norm_info[agtr_idx] = ntohl(msg->p4ml_hdrs.norm_info);
                        }
                    }
                    // if(msg->cmd == TENSOR_NORM) {
                    //     float norm_info;
                    //     uint32_t norm_int_format = ntohl(msg->p4ml_hdrs.norm_info);
                    //     memcpy(&norm_info, &norm_int_format, sizeof(uint32_t));
                    //     printf("Key: %" PRIu64 ", norm: %.6f\n", msg->key, norm_info); 
                    // }

                    if(msg->cmd == TENSOR_COMM) {
                        process_data(msg);
#ifdef USE_EXPECTED_KEY
                        expected_key = msg->key + 1;
#endif
                    }

                    if(receive_count[agtr_idx] == msg->p4ml_hdrs.num_worker){
                        uint32_t return_bitmap = bitmap[agtr_idx] | ntohl(msg->p4ml_hdrs.bitmap);
                        bitmap[agtr_idx] = 0; //clear bitmap
#ifdef USE_TX_THREAD
                        struct PSJob job = {msg->key, msg->len, msg->cmd, msg->p4ml_hdrs.agtr, msg->p4ml_hdrs.roundNum, send_pkt_ptrs[0][agtr_idx]};
                        _pushpull_queue_mutex.lock();
                        pushpulljobQueue.push(job);
                        received++;
                        _pushpull_queue_mutex.unlock();
#else
                        broadcast_packet(return_bitmap, msg);
                        received++;
#endif
                    }
                    else{
                        bitmap[agtr_idx] |= ntohl(msg->p4ml_hdrs.bitmap); // update bitmap
                        // drop packet
                    }
                }
                rte_pktmbuf_free(pkt[i]);
			} //for-loop through the packets received through rte_eth_rx_burst
            if(should_shutdown) break;
		} //for(;;)
	} //while
    printf("received %d correct packets in total\n", received);
    return 0;
}

void main_receive_packet_loop(int thread_id, struct rte_mempool *mbuf_pool) {
    // setup packet header fields
    struct rte_ether_addr s_addr = {SERVER_SRC_MAC};
#if !MULTI_CLIENT
    struct rte_ether_addr d_addr = {SERVER_DST_MAC};
#endif
    uint16_t ether_type = htons(0x0a01);
    int round=0;
    int recv_count=0;

    while(round < 3){
		struct rte_mbuf * pkt[BURST_SIZE];
		struct rte_mbuf *send_pkt[BURST_SIZE];
       		
		int i;
        /* retrieve received packets */
		// (port_id, queue_id, rte_mbuf** rx_pkts(points to the mbuf array of the received packets)
		//  , maximum_num_pkts)
		for (i = 0; i < BURST_SIZE; i++){
			send_pkt[i] = rte_pktmbuf_alloc(mbuf_pool);
			if (!send_pkt[i]) {
				printf("failed to alloc mbuf for index %d\n", i);
				return;
            }
		}
		for(;;){
			uint16_t num_rx = rte_eth_rx_burst(0, 0, pkt, std::max(BURST_SIZE, 4));
			if(num_rx == 0)
			{
				// no packet retrieved, skip
				continue;
			}
            struct Message* msg; 
			struct rte_ether_hdr * eth_hdr;

			// print received data
			for(i=0;i<num_rx;i++){
				// (mbuf, data type)
				eth_hdr = rte_pktmbuf_mtod(pkt[i],struct rte_ether_hdr*);
                printf("Receive packet from MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                        " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " : ",
                        eth_hdr->src_addr.addr_bytes[0],eth_hdr->src_addr.addr_bytes[1],
                        eth_hdr->src_addr.addr_bytes[2],eth_hdr->src_addr.addr_bytes[3],
                        eth_hdr->src_addr.addr_bytes[4],eth_hdr->src_addr.addr_bytes[5]);
                msg = (struct Message*)((rte_pktmbuf_mtod(pkt[i],char*)) + sizeof(struct rte_ether_hdr));
                printf("worker_bitmap: %d, num_worker: %d, round_num: %d, max_info: %d, norm_info: %d, type_and_data_index: %d, agtr: %d, \n", 
                        ntohl(msg->p4ml_hdrs.bitmap), msg->p4ml_hdrs.num_worker, ntohl(msg->p4ml_hdrs.roundNum),
                        ntohl(msg->p4ml_hdrs.max_info), ntohl(msg->p4ml_hdrs.norm_info), msg->p4ml_hdrs.type_and_index, ntohs(msg->p4ml_hdrs.agtr));
                printf("CMD: %d\n", msg->cmd);
                if(msg->cmd == TENSOR_INIT) {
                    printf("Init Key: %" PRIu64 "\n", msg->key);
                    init_tensor_response(mbuf_pool, msg->key, send_pkt, eth_hdr->src_addr);
                    //init_tensor_response(mbuf_pool, msg->key, send_pkt)
                }
                else{
                    printf("Key: %" PRIu64 "\n", msg->key);
				    int j;
                    for(j=0;j<10;j++)
#ifdef USE_UINT8
                        printf("%hhu ",((uint8_t*)(msg->p4ml_hdrs.vector))[j]);
#else
                        printf("%d ",ntohl(msg->p4ml_hdrs.vector[j]));
#endif
                    printf("\n");
				    printf("pkt_len: %d, data_len: %d, eth_hdr len: %ld\n", pkt[i]->pkt_len,
				           pkt[i]->data_len, sizeof(struct rte_ether_hdr));
#ifdef USE_UINT8
                    if (((uint8_t*)(msg->p4ml_hdrs.vector))[0] == (uint8_t)NUM_WORKER)
#else
				    if (ntohl(msg->p4ml_hdrs.vector[0]) == (uint32_t)NUM_WORKER)
#endif
                    {
    				    struct rte_ether_hdr *send_eth_hdr;
                        struct Message *send_msg;
                        struct Message send_obj;
                        ps_assemble_p4ml_hdrs(&(send_obj.p4ml_hdrs), 0, (uint8_t)NUM_WORKER, 1, 0, (char*)msg->p4ml_hdrs.vector);
                        send_obj.key = msg->key;
                        send_obj.data = msg->data;
					    for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i){
                            // a fake aggregation. We shouldn;t need to worry about possibly overflowing uint8 here
                            send_obj.p4ml_hdrs.vector[i] = msg->p4ml_hdrs.vector[i] + msg->p4ml_hdrs.vector[i]; 
                        }
                        send_msg = (struct Message *)(rte_pktmbuf_mtod(send_pkt[recv_count], char *) + sizeof(struct rte_ether_hdr));
        			    *send_msg = send_obj;		
        			    send_eth_hdr = rte_pktmbuf_mtod(send_pkt[recv_count], struct rte_ether_hdr *);
        			    send_eth_hdr->dst_addr = d_addr;
        			    send_eth_hdr->src_addr = s_addr;
        			    send_eth_hdr->ether_type = ether_type;

        			    int pkt_size = sizeof(struct Message) + sizeof(struct rte_ether_hdr);
        			    send_pkt[recv_count]->data_len = pkt_size;
        			    send_pkt[recv_count]->pkt_len = pkt_size;
					    ++recv_count;
				    }
                } // process regular data packet
                rte_pktmbuf_free(pkt[i]);
			} //for-loop through the packets received through rte_eth_rx_burst
			if(recv_count == BURST_SIZE) {
				uint16_t num_tx = rte_eth_tx_burst(0, 0, send_pkt, BURST_SIZE);
				printf("Sent %d packets, round: %d\n", num_tx, round);
				++round;
    			// cleanup resources for sending ACK
				for (int i = 0; i < BURST_SIZE; i++)
					rte_pktmbuf_free(send_pkt[i]);
				recv_count = 0;
				break;
			}
			else if (recv_count > BURST_SIZE){
				printf("WARNING round: %d, recv_count:%d\n", round, recv_count);
				++round;
				recv_count = 0;
				for (int i = 0; i < BURST_SIZE; i++)
					rte_pktmbuf_free(send_pkt[i]);
				break;
			} 
		} //for(;;)
	} //while
}

// void Start(int thread_id, struct rte_mempool *mbuf_pool) {
//     printf("Start thread_id: %d\n", thread_id);
//     test_receive_packet_loop(thread_id, mbuf_pool);
//     // main_receive_packet_loop(thread_id, mbuf_pool); 
// }

int main(int argc, char *argv[]) {
    srand(time(NULL));

    appID = atoi(argv[1]);
    num_thread = 1;

    /* modify argc and argv accordingly to pass into rte_eal_init */
    argc--;
    argv++;
    
    /* initialize the DPDK connection here */
    
    // struct rte_mempool *mbuf_pool;
    // struct rte_mempool *mbuf_recv_pool;
    
    // initialize EAL
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "initialize fail!");
    
    // Creates a new mempool in memory to hold the mbufs.
    for (uint32_t i = 0; i < PS_WORKERS; ++i){
        mbuf_pool[i] = rte_pktmbuf_pool_create(("MBUF_POOL_"+std::to_string(i)).c_str(), TOTAL_AGTR_CNT,
		    MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        if (mbuf_pool[i] == NULL)
		    rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    }
    // mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", TOTAL_AGTR_CNT,
	// 	MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    mbuf_recv_pool = rte_pktmbuf_pool_create("MBUF_RECV_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_recv_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf recv pool\n");
	
    // Initialize all ports.
    if (port_init(mbuf_recv_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu8 "\n", 0);
    // flow_init();

    // initialize all arrays.
    for (uint32_t i = 0; i < TOTAL_AGTR_CNT; ++i){
        roundNumbers[i] = 0;
        bitmap[i] = 0;
        norm_info[i] = 0;
        receive_count[i] = 0;

        for (uint32_t j = 0; j < MAX_ENTRIES_PER_PACKET; ++j)
            data_slots[j][i] = 0;
        for (uint32_t j = 0; j < PS_WORKERS; ++j)
            send_pkt_ptrs[j][i] = rte_pktmbuf_alloc(mbuf_pool[j]);
    }
    
    // set workers' MAC addresses
#ifdef PS_TIMI
    worker_addrs.push_back(TIMI_ENS2F1);
#else
    worker_addrs.push_back(LAMBDA_ENS8F0);
#endif

    printf("Done initializing the DPDK manager!\n");

    /* Launches the receiving function on each lcore. */
    struct rx_thread_args rx_args = {0};
    for (unsigned lcore_id = 1; lcore_id <= PS_RX_RING; ++lcore_id) rte_eal_remote_launch(RxThread, &rx_args, lcore_id);
#ifdef USE_TX_THREAD
    rte_eal_remote_launch(TxThread, &rx_args, PS_RX_RING+1);
#endif
    // rte_eal_remote_launch(RxThread, &rx_args, 1);
    // rte_eal_remote_launch(RxThread, &rx_args, 2);
    // rte_eal_remote_launch(TxThread, &rx_args, 3);

    /* Start PS thread */
    // for (int i = 0; i < num_thread; i++)
    //     Start(i, mbuf_pool); //TODO: workQueue->enqueue(Start, i); might need a pool per thread?

    rte_eal_mp_wait_lcore();
    for (uint32_t i = 0; i < TOTAL_AGTR_CNT; ++i){
        for (uint32_t j = 0; j < PS_WORKERS; ++j)
            rte_pktmbuf_free(send_pkt_ptrs[j][i]); // free sending resources
    }

    rte_eal_cleanup();
    printf("Software PS shutdown\n");
    return 0;

}
