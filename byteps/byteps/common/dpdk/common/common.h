#ifndef DPDK_COMMON_H
#define DPDK_COMMON_H
#include <algorithm>
#include <unistd.h>

/* push-pull tensor type */
#define TENSOR_COMM 1
#define TENSOR_NORM 2
#define TENSOR_INIT 3

/* common macros */
#define SW_PS
// #define IP_HDR
// #define USE_EXPECTED_KEY
// #define SET_AFFINITY
// #define TEST_NORM
//#define TEST_SINGLE
#define USE_UINT8

struct Job{
    uint64_t key;
    char* data;
    uint32_t len;
    int cmd;
    std::string tensor_name;
    uint16_t agtr_index;
};

struct AppInfo
{
    uint32_t host;
    uint16_t appID;
    uint8_t num_worker;
    uint8_t num_PS;
};

#endif //DPDK_COMMON_H