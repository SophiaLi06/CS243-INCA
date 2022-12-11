#include "dpdk_manager.h"

#define ENABLE_LOG true


/* 
 * This function initializes model with uint_8 values
 * return: pointers to uint_8 values
 */
uint8_t* init_model_uint8(int size, int seed) {
    uint8_t* tmp = new uint8_t[size];
    for (int i = 0; i < size; i++)
        tmp[i] = (uint8_t)((i+1) % 10 + seed);
    return tmp;
}

/* 
 * This function initializes model with uint_32 values
 * return: pointers to uint_32 values
 */
uint32_t* init_model(int size) {
    uint32_t* tmp = new uint32_t[size];
    for (int i = 0; i < size; i++)
        tmp[i] = i+1;
    return tmp;
}

/* 
 * This function initializes model with floating point values
 * return: pointers to float values
 */
float* init_model_float(int size) {
    float* tmp = new float[size];
    for (int i = 0; i < size; i++) {
        tmp[i] = (i+1.0) / 10000000.0;
        // tmp[i] = (i + 1.0) / 10000.0;
        // printf("%f ", tmp[i]);
    }
    // tmp[63] = 200;
    return tmp;
}

/* 
 * This function initializes model with floating point values that might overflow
 * return: pointers to float values
 */
float* init_model_float_with_overflow(int size) {
    float* tmp = new float[size];
    for (int i = 0; i < size; i++) {
        tmp[i] = (i+1.0) / 10000000.0;
    }
    for (int i = 0; i < 100; i++) {
        int rand_num = rand() % size;
        if (rand_num > size / 2)
            tmp[rand_num] = 200;
        else 
            tmp[rand_num] = 100;
        // printf("rand!!! %d\n", rand_num);
    }
    return tmp;
}

std::shared_ptr<DPDKManager> _dpdk_manager;
//std::shared_ptr<P4mlManager> _p4ml_manager;

int test_single_client(std::shared_ptr<DPDKManager> _dpdk_manager, char** tensor, int size){
    struct Job new_pushpull_job[3];
    for (int i = 0; i < 3; ++i){
        _dpdk_manager->InitJob(&(new_pushpull_job[i]), tensor[i], (uint32_t)size);
        for(int j = 0; j < NUM_WORKER; ++j) _dpdk_manager->Push(&(new_pushpull_job[i]), 1);
    }
    for (int i = 0; i < 3; ++i){
        for(int j = 0; j < NUM_WORKER; ++j) _dpdk_manager->Push(&(new_pushpull_job[i]), 1);
    }

    
    _dpdk_manager->Shutdown();
    return 0;
}

int test_multi_client(std::shared_ptr<DPDKManager> _dpdk_manager, char** tensor, int size){
    struct Job new_pushpull_job[3];
    for (int i = 0; i < 3; ++i){
        _dpdk_manager->InitJob(&(new_pushpull_job[i]), tensor[i], (uint32_t)size);
        _dpdk_manager->Push(&(new_pushpull_job[i]), 1);
    }
    _dpdk_manager->Shutdown();
    return 0;
}

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

    // For now, worker ID 0 is timi, and worker ID 1 is rumi
    _dpdk_manager = std::shared_ptr<DPDKManager>(new DPDKManager(host, num_worker, appID, num_PS,
     argc, argv));

    if(_dpdk_manager) printf("Start push-pull testing!\n");
    
    /* Here for int size to send per thread */
    /* ex. 25600 = 32*800 = 1 Round */
    int size = 1024000;
    int thread_to_use = 1; // TODO: single thread for now to make life easier
    //int thread_to_use = 12;
    int loop_time = 3; // TODO: increase
    //int loop_time = 1000;

    /* (40) Threads in thread pool */
    /* MAX_AGTR (32000) / 40 = 800 Agtr per thread */
    // _p4ml_manager->init_threadPool(thread_to_use);

    // _p4ml_manager->SetForceForward(0.25);
    // _p4ml_manager->SetMaxAgtrSizePerThread(50);

    int finish_counter = loop_time * thread_to_use;
#ifdef USE_UINT8
    uint8_t** int_tensor = new uint8_t*[thread_to_use * loop_time];
#else
    uint32_t** int_tensor = new uint32_t*[thread_to_use * loop_time];
#endif

    printf("\nModel initializing...\n");
#ifdef USE_UINT8
    for (int i = 0; i < thread_to_use * loop_time; i++){
        int_tensor[i] = init_model_uint8(size, i * 10);
    }
#else
    for (int i = 0; i < thread_to_use * loop_time; i++){
        int_tensor[i] = init_model(size);
    }
#endif
        
    printf("\nModel initialized completed. Start sending...\n\n");

#ifdef TEST_SINGLE
    int res = test_single_client(_dpdk_manager, (char**)int_tensor, size);
#else
    int res = test_multi_client(_dpdk_manager, (char**)int_tensor, size);
#endif // TEST_SINGLE

    if (res == 0) printf("Finished Testing \n");
    /* Check whether the lcoal data has been updated */
    for (int i = 0; i < 3; ++i){
        printf("tensor chunk %d:\n", i);
#ifdef USE_UINT8
        for (uint32_t j = 0; j < MAX_ENTRIES_PER_PACKET * 4; ++j) {
            printf("%hhu ", ((uint8_t*)(int_tensor[i]))[j]);
        }
#else
        for (uint32_t j = 0; j < MAX_ENTRIES_PER_PACKET; ++j) {
            printf("%d ", ((uint32_t*)(int_tensor[i]))[j]);
        }
#endif
        printf("\n");
    }
    // std::chrono::time_point<std::chrono::system_clock> timer = std::chrono::high_resolution_clock::now();

    // std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
    
    // for (int j = 0; j < loop_time; j++) {
    // /* thread to use */
    //     for (int i = 0; i < thread_to_use; i++) {
    //         uint64_t key = _dpdk_manager->GetNewKey();
    //         printf("index 0 is: %f\n", tensor[0][0]);
    //         //_dpdk_manager->PushPull(key, (char*) tensor[0], size, 0);
    //         _dpdk_manager->PushPull(key, (char*) tensor[0], size, 1);
    //     }
    // }

    // std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();    
    // std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
    // double transmit_size_in_m = (double)((double)size * loop_time * thread_to_use / (float)MAX_ENTRIES_PER_PACKET) * DPDK_PACKET_SIZE / 1024 / 1024;
    // double total_time = time_span.count();
    // double throughput = (transmit_size_in_m / 1024 * 8 ) / total_time;
    // printf("Finish all %d Tensors,\n  Time = %lf s,\n  Total Size = %lf MB,\n  Throughput: %lf Gbps\n\n", thread_to_use * loop_time, total_time, transmit_size_in_m, throughput);

	/* clean up the EAL */
	rte_eal_cleanup();
}
