#include "test.cuh"
#include <stdio.h>
#include <stdint.h>
#include <algorithm>

// assume the data type is float 32 for now. In the future, better do dtype check
__global__ void test_kernel(void){}

__global__ void test_mul(const void* p, size_t len){
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(p));
    for(size_t i = 0; i < len; i++) {
        //printf("Before %f\n", ptr[i]);
        ptr[i] = ptr[i] * 2.0;
        //printf("After %f\n", ptr[i]);
    }
}

__global__ void test_div(const void* p, size_t len){
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(p));
    for(size_t i = 0; i < len; i++) {
        //printf("Before %f\n", ptr[i]);
        ptr[i] = ptr[i] / 2.0;
        //printf("After %f\n", ptr[i]);
    }
}

__global__ void test_quan(const void* p, size_t len){
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(p));
    uint8_t* res_ptr = reinterpret_cast<uint8_t*>(const_cast<void*>(p));
    float max_val = 0;
    for(size_t i = 0; i < len; ++i) {
        if (ptr[i] > max_val) max_val = ptr[i];
    }
    //printf("max_val %f\n", max_val);
    for(size_t i = 0; i < len; ++i) {
        res_ptr[i] = uint8_t(ptr[i] / max_val);
        //printf("After %hhu\n", res_ptr[i]);
    }
    ptr[len/4] = max_val;
}

__global__ void test_unclip(const void* p, size_t len){
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(p));
    uint8_t* quan_ptr = reinterpret_cast<uint8_t*>(const_cast<void*>(p));
    size_t compressed_len = len / 4;
    float max_val = ptr[compressed_len];
    for(size_t i = len - 1; i >= 0; --i) {
        //printf("Before %f\n", ptr[i]);
        ptr[i] = (float(quan_ptr[i]) / float(256) * max_val);
        //printf("After %f\n", ptr[i]);
    }
}

void test_wrapper(void){
    test_kernel <<<1, 1>>> ();
    std::cout << "Tested CUDA kernel" << std::endl;
}

void test_wrapper(void* gpu_ptr){
    test_kernel <<<1, 1>>> ();
    if(gpu_ptr) std::cout << "GPU address: " << gpu_ptr << std::endl;
}

void test_wrapper(void* gpu_ptr, const void* data_ptr, int offset){
    test_kernel <<<1, 1>>> ();
    //if(gpu_ptr) std::cout << "GPU address: " << gpu_ptr << std::endl;
    //if(data_ptr) std::cout << "Data pointer: " << data_ptr << "offset: " << offset << std::endl;
}

void test_mul_wrapper(const void* p, size_t len){
    test_mul <<<1, 1>>> (p, len);
    // Wait for GPU to finish
    cudaDeviceSynchronize();
    //std::cout << "Tested CUDA multiply" << std::endl;
}

void test_div_wrapper(const void* p, size_t len){
    test_div <<<1, 1>>> (p, len);
    // Wait for GPU to finish
    cudaDeviceSynchronize();
    //std::cout << "Tested CUDA multiply" << std::endl;
}

void test_quan_wrapper(const void* p, size_t len){
    test_quan <<<1, 1>>> (p, len);
    // Wait for GPU to finish
    cudaDeviceSynchronize();
}

void test_clipping(const void* p, size_t len){
    test_unclip <<<1, 1>>> (p, len);
    // Wait for GPU to finish
    cudaDeviceSynchronize();
}