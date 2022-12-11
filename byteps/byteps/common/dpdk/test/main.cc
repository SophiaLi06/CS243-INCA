#include "dpdk_test.h"


std::shared_ptr<DPDKManager> _dpdk_manager;


int main(int argc, char *argv[])
{
    //bindingCPU(0);

    if (argc < 5) {
        printf("\nUsage %s [MyID] [Num of Worker] [AppID] [Num of PS]\n\n", argv[0]);
        exit(1);
    }

    int host = atoi(argv[1]); 
    int num_worker = atoi(argv[2]); 
    int appID = atoi(argv[3]); 
    int num_PS = atoi(argv[4]);
    int eal_argc = 10;
    char* eal_argv[eal_argc] = {"./client", "0", "1", "0", "1", "-a", "c1:00.1", "--lcores=0@16,1@16,2@17", "--main-lcore=0", "--legacy-mem"};
    // char* eal_argv[eal_argc] = {"./client", "0", "1", "0", "1", "-a", "21:00.0", "--lcores=0@16,1@16,2@17,3@17", "--main-lcore=0", "--legacy-mem"};

    // For now, worker ID 0 is timi, and worker ID 1 is rumi
    _dpdk_manager = std::shared_ptr<DPDKManager>(new DPDKManager(host, num_worker, appID, num_PS,
     eal_argc, eal_argv));

    if(_dpdk_manager) printf("Initialized DPDK Manager!\n");

    for(int i = 0; i < TEST_ROUND; ++i) _dpdk_manager->TestBurstySend(TEST_SIZE, i*TEST_ROUND);
    printf("DONE Bursty Send!\n");

    sleep(30);

    _dpdk_manager->Shutdown();
    
	/* clean up the EAL */
	rte_eal_cleanup();
}
