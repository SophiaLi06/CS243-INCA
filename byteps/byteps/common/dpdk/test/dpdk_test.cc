#include "dpdk_test.h"

uint32_t DPDKManager::host;
uint8_t DPDKManager::num_worker;
uint8_t DPDKManager::num_PS;
uint16_t DPDKManager::appID;
uint64_t DPDKManager::dpdkKey;
uint64_t DPDKManager::aggr_count;

std::queue<uint64_t> DPDKManager::finishKeys;

std::mutex DPDKManager::_DPDKKey_mutex;
std::mutex DPDKManager::_key_queue_mutex;
std::mutex DPDKManager::_queuePush_mutex;
std::mutex DPDKManager::_print_mutex;
std::mutex DPDKManager::_cpubuff_mutex;
std::mutex DPDKManager::_slot_roundnum_mutex[MAX_AGTR_COUNT+NORM_SLOT];
// std::mutex DPDKManager::_slot_roundnum_mutex;
std::mutex DPDKManager::_key_map_mutex;
std::mutex DPDKManager::_part_finish_mutex;
std::queue<Job*> DPDKManager::finishQueue; 
std::unordered_map<uint64_t, struct Job*> DPDKManager::job_table;
std::unordered_map<uint64_t, char*> DPDKManager::cpubuff_table;
std::unordered_map<uint64_t, uint64_t> DPDKManager::dpdk_key_to_tensor_key;
std::unordered_map<uint64_t, uint64_t> DPDKManager::tensor_key_part_total;
std::unordered_map<uint64_t, uint64_t> DPDKManager::tensor_key_part_finish;

struct rte_mempool* DPDKManager::mbuf_pool;
struct rte_mempool* DPDKManager::mbuf_recv_pool;
struct rte_ether_addr DPDKManager::s_addr;
struct rte_ether_addr DPDKManager::d_addr;
uint16_t DPDKManager::ether_type;
struct rte_mbuf* DPDKManager::send_pkt_ptrs[WORKER_SEND_NUM_MBUFS+NORM_SLOT];
uint16_t DPDKManager::slot_roundnum[MAX_AGTR_COUNT+NORM_SLOT];
bool DPDKManager::slot_ready[MAX_AGTR_COUNT+NORM_SLOT];
volatile bool DPDKManager::should_shutdown;
volatile uint64_t DPDKManager::expected_key;

// additional things for testing only
float DPDKManager::data[MAX_ENTRIES_PER_PACKET];

int DPDKManager::port_init(struct rte_mempool *mbuf_recv_pool){
    struct rte_eth_txmode txmode;
    struct rte_eth_rxmode rxmode;
    // struct rte_eth_rss_conf rss_conf;
    rxmode.mtu = RTE_ETHER_MAX_LEN;
    // rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
    // rss_conf.rss_key = NULL;
    // rss_conf.rss_hf = RTE_ETH_RSS_PROTO_MASK;
    // rss_conf.rss_hf = RTE_ETH_RSS_IPV4;
    struct rte_eth_conf port_conf = {};
    struct rte_eth_rxconf rxq_conf = {};
    rxq_conf.rx_drop_en = 0; //Don't drop packets if no descriptors are available.
    port_conf.rxmode = rxmode;
    port_conf.txmode = txmode;
    // port_conf.rx_adv_conf.rss_conf = rss_conf;
	const uint16_t rx_rings = RX_RING, tx_rings = 1;
    struct rte_eth_dev_info dev_info;
	int retval;
	uint16_t q;

    /* Offload fast free */
    rte_eth_dev_info_get(0, &dev_info);
	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE){
        printf("Fast free\n");
        port_conf.txmode.offloads |=
			DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    /* update rss_conf */
    // port_conf.rx_adv_conf.rss_conf.rss_hf &=
    //     dev_info.flow_type_rss_offloads;
    // printf("rss_hf %lu\n", port_conf.rx_adv_conf.rss_conf.rss_hf);

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

/* This function is for testing purpose only!!!
TODO: remove*/
int DPDKManager::TestLcore(void* thread_args){
    unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("listening DPDK thread at lcore %u on CPU %d\n", lcore_id, sched_getcpu());
    printf("From the function, the thread id = %d\n", pthread_self()); //get current thread id
    int ack_count = 0;
    while(!should_shutdown){

		struct rte_mbuf* recv_pkt[std::max(BURST_SIZE, 1280)];

		/* retrieve received packets
        /* (port_id, queue_id, rte_mbuf** rx_pkts(points to the mbuf array of the received packets)
		/*  , maximum_num_pkts) */

		uint16_t num_rx = rte_eth_rx_burst(0, lcore_id - 1, recv_pkt, std::max(BURST_SIZE, 1280));

		if(num_rx == 0){
			//no packets retrieved, skip
			continue;
		}
	
        /* processing the retrieved packet */
		struct rte_ether_hdr* recv_eth_hdr;
#ifdef IP_HDR
        struct rte_ipv4_hdr* recv_ip4_hdr;
#endif
		/* print received message */
		for(uint16_t i=0;i<num_rx;i++){
		    /* Print the MAC address of the sender of the retrieved packets
            /* (mbuf, data type)
            */
			recv_eth_hdr = rte_pktmbuf_mtod(recv_pkt[i],struct rte_ether_hdr*);
#ifdef IP_HDR
            recv_ip4_hdr = rte_pktmbuf_mtod_offset(recv_pkt[i], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
#endif
            // _print_mutex.lock();
            
            if(ntohs(recv_eth_hdr->ether_type) != 0x0a00 + lcore_id - 1) 
                printf("lcore %u Received ether_type %hu\n", lcore_id, ntohs(recv_eth_hdr->ether_type));
            // printf("Received packet from MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
            //         " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " : ",
            //         recv_eth_hdr->src_addr.addr_bytes[0],recv_eth_hdr->src_addr.addr_bytes[1],
            //         recv_eth_hdr->src_addr.addr_bytes[2],recv_eth_hdr->src_addr.addr_bytes[3],
            //         recv_eth_hdr->src_addr.addr_bytes[4],recv_eth_hdr->src_addr.addr_bytes[5]);

            struct TestMessage* recv_msg;
#ifdef IP_HDR
			recv_msg = (struct TestMessage*)((rte_pktmbuf_mtod(recv_pkt[i],char*)) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
#else
            recv_msg = (struct TestMessage*)((rte_pktmbuf_mtod(recv_pkt[i],char*)) + sizeof(struct rte_ether_hdr));
#endif
            // printf("RECV Key: %" PRIu64 ", CMD: %d, round_num: %hu\n", recv_msg->key, recv_msg->cmd, ntohs(recv_msg->p4ml_hdrs.roundNum));

            _part_finish_mutex.lock();
            tensor_key_part_finish[recv_msg->key]--;
            _part_finish_mutex.unlock();

            _slot_roundnum_mutex[ntohs(recv_msg->p4ml_hdrs.agtr)].lock();
            slot_roundnum[ntohs(recv_msg->p4ml_hdrs.agtr)]++;
            slot_ready[ntohs(recv_msg->p4ml_hdrs.agtr)] = true;
            _slot_roundnum_mutex[ntohs(recv_msg->p4ml_hdrs.agtr)].unlock();
            
            // _print_mutex.unlock();
            rte_pktmbuf_free(recv_pkt[i]);
            ack_count++;
		}
        pthread_yield();	
    }
    printf("lcore %u got %d\n", lcore_id, ack_count);
    return 0;
}

void DPDKManager::Send(uint64_t key, char* data, int len, int cmd, uint16_t agtr_index){
    struct rte_mbuf* pkt[BURST_SIZE];
    struct TestMessage* msg;
    struct rte_ether_hdr *eth_hdr;
#ifdef IP_HDR
    struct rte_ipv4_hdr  *ip4_hdr;
#endif
    uint16_t send_buf_index;
    uint16_t round_num;

    for (int pkt_idx = 0; pkt_idx < BURST_SIZE; ++pkt_idx){
        _slot_roundnum_mutex[agtr_index].lock();
        while(!slot_ready[agtr_index]) {
            _slot_roundnum_mutex[agtr_index].unlock();
            usleep(1);
            _slot_roundnum_mutex[agtr_index].lock();
        }
        round_num = slot_roundnum[agtr_index];
        _slot_roundnum_mutex[agtr_index].unlock();

        struct TestMessage msg_body;
        send_buf_index = key % WORKER_SEND_NUM_MBUFS;
    
        assemble_normal_hdrs(&(msg_body.p4ml_hdrs), host, (uint8_t)NUM_WORKER, round_num, data, key, len);

        _part_finish_mutex.lock();
        tensor_key_part_finish[key]++;
        _part_finish_mutex.unlock();
        
        msg_body.key = key;
        msg_body.cmd = cmd;
        msg_body.len = len;

        // assuming that we are always using this test code with a software ps
        msg_body.shutdown = false;
        
        pkt[pkt_idx] = send_pkt_ptrs[send_buf_index];
        eth_hdr = rte_pktmbuf_mtod(pkt[pkt_idx], struct rte_ether_hdr*);
        eth_hdr->dst_addr = d_addr;
        eth_hdr->src_addr = s_addr;
        eth_hdr->ether_type = ether_type;
        // add ip header
#ifdef IP_HDR
	    ip4_hdr = rte_pktmbuf_mtod_offset(pkt[pkt_idx], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
		ip4_hdr->src_addr = htonl((uint32_t)(agtr_index % 2));
        ip4_hdr->dst_addr = htonl((uint32_t)(agtr_index % 2));
        msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
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

        // printf("SENT Key: %" PRIu64 " with roundnum %hu of len %d\n", msg->key, round_num, msg->len);

        // _print_mutex.unlock();
    }
    
    uint16_t num_tx = rte_eth_tx_burst(0, 0, pkt, BURST_SIZE); // returns the number of packets transmitted
    if(num_tx < 1) printf("WARNING: rte_eth_tx_burst sent zero packet!\n");
    // if(round_num != ntohs(((struct TestMessage*)(rte_pktmbuf_mtod(pkt[0],char*) + sizeof(struct rte_ether_hdr)))->p4ml_hdrs.roundNum)){
    //     printf("expected to send round_num %hu but sent %hu\n", round_num,
    //       ntohs(((struct TestMessage*)(rte_pktmbuf_mtod(pkt[0],char*) + sizeof(struct rte_ether_hdr)))->p4ml_hdrs.roundNum));
    // }
}

/* This function sends pkt_num packet at once. */
void DPDKManager::TestBurstySend(uint64_t pkt_num, uint64_t start_key){

    uint64_t sent_count = 0;
    while(sent_count < pkt_num){
        struct rte_mbuf* pkt[SEND_BURST_SIZE];

        for (int pkt_idx = 0; pkt_idx < SEND_BURST_SIZE; ++pkt_idx){
            uint64_t dpdk_key = (start_key+pkt_idx) % DPDK_KEY_TOTAL;
            uint16_t agtr_index = dpdk_key % MAX_AGTR_COUNT;

            struct TestMessage* msg;
            struct rte_ether_hdr *eth_hdr;
#ifdef IP_HDR
            struct rte_ipv4_hdr  *ip4_hdr;
#endif
            uint16_t send_buf_index;
            uint16_t round_num;

            _slot_roundnum_mutex[agtr_index].lock();
            while(!slot_ready[agtr_index]) {
                _slot_roundnum_mutex[agtr_index].unlock();
                usleep(1);
                _slot_roundnum_mutex[agtr_index].lock();
            }
            round_num = slot_roundnum[agtr_index];
            _slot_roundnum_mutex[agtr_index].unlock();

            struct TestMessage msg_body;
            send_buf_index = dpdk_key % WORKER_SEND_NUM_MBUFS;
    
            assemble_normal_hdrs(&(msg_body.p4ml_hdrs), host, (uint8_t)NUM_WORKER, round_num, (char*)data, dpdk_key, MAX_ENTRIES_PER_PACKET);

            _part_finish_mutex.lock();
            tensor_key_part_finish[dpdk_key]++;
            _part_finish_mutex.unlock();
        
            msg_body.key = dpdk_key;
            msg_body.cmd = TENSOR_COMM;
            msg_body.len = MAX_ENTRIES_PER_PACKET;

            // assuming that we are always using this test code with a software ps
            msg_body.shutdown = false;
        
            pkt[pkt_idx] = send_pkt_ptrs[send_buf_index];
            eth_hdr = rte_pktmbuf_mtod(pkt[pkt_idx], struct rte_ether_hdr*);
            eth_hdr->dst_addr = d_addr;
            eth_hdr->src_addr = s_addr;
            eth_hdr->ether_type = ether_type;
            // add ip header
#ifdef IP_HDR
	        ip4_hdr = rte_pktmbuf_mtod_offset(pkt[pkt_idx], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
		    ip4_hdr->src_addr = htonl((uint32_t)(agtr_index % 2));
            ip4_hdr->dst_addr = htonl((uint32_t)(agtr_index % 2));
            msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[pkt_idx], char *) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
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

            // printf("SENT Key: %" PRIu64 " with roundnum %hu of len %d\n", msg->key, round_num, msg->len);

            // _print_mutex.unlock();
        }

        uint16_t num_tx = rte_eth_tx_burst(0, 0, pkt, SEND_BURST_SIZE); // returns the number of packets transmitted
        sent_count += num_tx;
        start_key += num_tx;
    }

    // for (uint64_t i = 0; i < pkt_num; ++i){
    //     uint64_t dpdk_key = (start_key+i) % DPDK_KEY_TOTAL;
    //     uint16_t agtr_index = dpdk_key % MAX_AGTR_COUNT;
    //     Send(dpdk_key, (char*)data, MAX_ENTRIES_PER_PACKET, TENSOR_COMM, agtr_index);
    // }

}

#ifdef SW_PS
void DPDKManager::ShutdownSignal(){
    struct rte_mbuf* pkt[BURST_SIZE];
    struct TestMessage* msg;
    struct rte_ether_hdr *eth_hdr;

    struct TestMessage msg_body;
    msg_body.shutdown = true;

    pkt[0] = send_pkt_ptrs[0];
    eth_hdr = rte_pktmbuf_mtod(pkt[0], struct rte_ether_hdr*);
    eth_hdr->dst_addr = d_addr;
    eth_hdr->src_addr = s_addr;
    eth_hdr->ether_type = ether_type;
#ifdef IP_HDR
    msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[0], char *) + sizeof(struct rte_ether_hdr)
                                + sizeof(struct rte_ipv4_hdr));
#else
    msg = (struct TestMessage*)(rte_pktmbuf_mtod(pkt[0], char *) + sizeof(struct rte_ether_hdr));
#endif
    *msg = msg_body;
#ifdef IP_HDR
    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
#else
    int pkt_size = sizeof(struct TestMessage) + sizeof(struct rte_ether_hdr);
#endif
    pkt[0]->data_len = pkt_size;
    pkt[0]->pkt_len = pkt_size;
    
    uint16_t num_tx = rte_eth_tx_burst(0, 0, pkt, BURST_SIZE); // returns the number of packets transmitted
    if(num_tx) printf("Sent shutdown signal to the Software PS\n");
}
#endif

DPDKManager::DPDKManager(uint32_t host, int num_worker, int appID, int num_PS, int argc, char** argv){

    this->host = host;
    this->appID = (uint16_t)appID;
    this->num_worker = (uint8_t)num_worker;
    this->num_PS = (uint8_t)num_PS;
    this->aggr_count = (uint64_t)0;
    this->dpdkKey = (uint64_t)0;

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
    this->mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", WORKER_SEND_NUM_MBUFS+NORM_SLOT,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    this->mbuf_recv_pool = rte_pktmbuf_pool_create("MBUF_RECV_POOL", WORKER_RECV_NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (this->mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    if (this->mbuf_recv_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf recv pool\n");

    for (int i = 0; i < WORKER_SEND_NUM_MBUFS+NORM_SLOT; ++i){
        this->send_pkt_ptrs[i] = rte_pktmbuf_alloc(this->mbuf_pool);
        if (!this->send_pkt_ptrs[i]) rte_exit(EXIT_FAILURE, "Cannot allocate a new send mbuf from a mempool\n");
    }

    for (int i = 0; i < MAX_AGTR_COUNT+NORM_SLOT; ++i){
        this->slot_roundnum[i] = 0;
        this->slot_ready[i] = true;
    }
	
    for (uint64_t i = 0; i < DPDK_KEY_TOTAL; ++i){
        this->tensor_key_part_finish[i] = 0;
    }

    // Initialize the testing data
    for (int i = 0; i < MAX_ENTRIES_PER_PACKET; ++i) this->data[i] = 0.0;

    // Initialize all ports.
    if (port_init(this->mbuf_recv_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu8 "\n", 0);
    flow_init();
    
    // setup packet header fields
    switch (host)
    {
    case 0:
        this->s_addr = LAMBDA_ENS8F1;
        this->d_addr = RUMI_ENP132S0F0;
        // this->s_addr = TIMI_ENS2F0;
        // this->d_addr = RUMI_ENP132S0F1;
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
    this->ether_type = htons(0x0a00);
    printf("Done initializing the DPDK manager!\n");

    this->should_shutdown = false;
    
    printf("main DPDK thread at lcore %u on CPU %d\n", rte_lcore_id(), sched_getcpu());

    printf("From the function, the thread id = %d\n", pthread_self()); //get current thread id
    
    /* Launches the receiving function on each lcore. */
    struct rx_thread_args rx_args = {0};
    for (unsigned lcore_id = 1; lcore_id <= RX_RING; ++lcore_id) rte_eal_remote_launch(this->TestLcore, &rx_args, lcore_id);
    // rte_eal_remote_launch(this->TestLcore, &rx_args, 1);
    // rte_eal_remote_launch(this->TestLcore, &rx_args, 2);
    // rte_eal_remote_launch(this->TestLcore, &rx_args, 3);
    // unsigned lcore_id;
	// RTE_LCORE_FOREACH_WORKER(lcore_id) {
	// 	rte_eal_remote_launch(this->RxThread, &rx_args, lcore_id);
	// }
    // RTE_LCORE_FOREACH_WORKER(lcore_id) {
	// 	rte_eal_remote_launch(this->TestLcore, &rx_args, lcore_id);
	// }

}

int DPDKManager::Shutdown(){
#ifdef SW_PS
    /* tell the software PS to shutdown */
    ShutdownSignal();
#endif
    this->should_shutdown = true;
    rte_eal_mp_wait_lcore();
    // for (uint64_t i = 0; i < DPDK_KEY_TOTAL; ++i){
    //     if (tensor_key_part_finish[i] != 0){
    //         printf("WARNING: for key %" PRIu64 " we got %d unanswered send\n", i, tensor_key_part_finish[i]);
    //     }
    // }
    // for (int pkt_idx = 0; pkt_idx < WORKER_SEND_NUM_MBUFS+NORM_SLOT; ++pkt_idx){
    //     rte_pktmbuf_free(send_pkt_ptrs[pkt_idx]); // free sending resources
    //     printf("freed %d\n", pkt_idx);
    // }
    printf("DPDK manager shutdown\n");
    return 0;
}
