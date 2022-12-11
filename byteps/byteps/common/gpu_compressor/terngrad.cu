#include "terngrad.cuh"
#include "math.h"
#include <stdio.h>
#include <iostream>

__global__ void setup_kernel(curandState *state)
{
    int id = threadIdx.x + blockIdx.x * blockDim.x;
    /* Each thread gets same seed, a different sequence
       number, no offset */
    curand_init(1234, id, 0, &state[id]);
}

__global__ void find_grad_max(const void* gpu_ptr, size_t len, float* result){
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(gpu_ptr));
    // printf("find_grad_max sample: %p, %.6f, %.6f, %.6f, %.6f, %.6f \n", gpu_ptr,
    //        ptr[0], ptr[1], ptr[2], ptr[3], ptr[4]);

    float grad_max;
    if (ptr[0] >= 0) grad_max = ptr[0];
    else grad_max = -ptr[0];
    float grad_abs;
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for(size_t i = index; i < len; i+=stride){
        if (ptr[i] >= 0) grad_abs = ptr[i];
        else grad_abs = -ptr[i];
        if (grad_abs > grad_max) grad_max = grad_abs;
    }
    *result = grad_max;
}

__global__ void para_max(const void* gpu_ptr, size_t len, float* result){
    extern __shared__ float res_cache[];

    float* ptr = reinterpret_cast<float*>(const_cast<void*>(gpu_ptr));

    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    int cacheIndex = threadIdx.x;

    float res = -1.0;
    for(size_t i = index; i < len; i+=stride){
        if (fabsf(ptr[i]) > res) res = fabsf(ptr[i]);
    }

    res_cache[cacheIndex] = res; // set the result cache value
    __syncthreads();

    // Perform parallel reduction
    int inc = blockDim.x / 2;
    while (inc != 0){
        if (cacheIndex < inc && res_cache[cacheIndex + inc] > res_cache[cacheIndex]) {
            res_cache[cacheIndex] = res_cache[cacheIndex + inc];
        }

        __syncthreads();
        inc /= 2;
    }

    if (cacheIndex == 0) result[blockIdx.x] = res_cache[0];
}

__global__ void terngrad_compress_kernel(const void* gpu_ptr, size_t len, curandState *state, float grad_max){
    //threadIdx.x contains the index of the current thread within its block, 
    //and blockDim.x contains the number of threads in the block
    //and gridDim.x gives the number of blocks in a grid
    int id = threadIdx.x + blockIdx.x * blockDim.x;
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(gpu_ptr));
    
    // if (id == 0) {
    //     printf("compress sample: %p, %.6f, %.6f, %.6f, %.6f, %.6f \n", gpu_ptr,
    //            ptr[0], ptr[1], ptr[2], ptr[3], ptr[4]);
    // }
    float x;
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    /* Copy state to local memory for efficiency */
    curandState localState = state[id];
    /* Generate pseudo-random uniforms */
    for(size_t i = index; i < len; i+=stride) {
        x = curand_uniform(&localState);
        if(x < fabsf(ptr[i])/grad_max) {
            if (ptr[i] > 0) ptr[i] = 1.0;
            else ptr[i] = -1.0;
        }
        else ptr[i] = 0.0;
    }
    // /* use the last value of gpu_ptr to store the "scaling factor" grad_max */
    // ptr[len-1] = grad_max;
    /* Copy state back to global memory */
    state[id] = localState;
    // TODO: change data type from float to uint8
}

__global__ void terngrad_decompress_kernel(const void* gpu_ptr, size_t len, float scale){
    //threadIdx.x contains the index of the current thread within its block, 
    //and blockDim.x contains the number of threads in the block
    //and gridDim.x gives the number of blocks in a grid
    int id = threadIdx.x + blockIdx.x * blockDim.x;
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(gpu_ptr));
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;

    for(size_t i = index; i < len-1; i+=stride) {
        ptr[i] *= scale;
    }
}

float terngrad_scale(const void* gpu_ptr, size_t len){
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(gpu_ptr));
    
    float grad_max = 0.0;

    if (len > 200){
        float *host_max_res, *dev_max_res;

        const unsigned int maxBlockCount = 32;
        const unsigned int maxThreadPerBlock = 128;
    
        // Allocate space for result on host
        host_max_res = (float*)calloc(maxBlockCount, sizeof(float));
        // Allocate space for result on device
        cudaMalloc(&dev_max_res, maxBlockCount * sizeof(float));
        para_max<<<maxBlockCount, maxThreadPerBlock, maxThreadPerBlock * sizeof(float)>>>(gpu_ptr, len, dev_max_res);

        // Copy device result to host
        cudaMemcpy(host_max_res, dev_max_res, maxBlockCount * sizeof(float), cudaMemcpyDeviceToHost);
        cudaFree(dev_max_res);

        // Find the maximum value across all blocks
        for (int i = 0; i < maxBlockCount; ++i){
            if (host_max_res[i] > grad_max) grad_max = host_max_res[i];
        }
    }
    else{
        float* grad_max_answer;
        cudaMalloc(&grad_max_answer, sizeof(float));
        //find_grad_max<<<64, 64>>>(gpu_ptr, len, grad_max_answer);
        find_grad_max<<<1, 1>>>(gpu_ptr, len, grad_max_answer);
        cudaMemcpy(&grad_max, grad_max_answer, sizeof(float), cudaMemcpyDeviceToHost);
        cudaFree(grad_max_answer);
    }
    return grad_max;
}

float terngrad_compress(const void* gpu_ptr, size_t len, float scale){
    // This is a tensor used for push-pulling the accuracy
    // TODO: in the future. just don't call terngrad_compress and decompress on it
    if (len <= 2) return 1.0; 
#ifdef TOTAL_TIME_CUDA
    // Create the timer
    cudaEvent_t total_start, total_stop;
    cudaEventCreate(&total_start);
    cudaEventCreate(&total_stop);

    // Start the timer
    cudaEventRecord(total_start, 0);
#endif
    float* ptr = reinterpret_cast<float*>(const_cast<void*>(gpu_ptr));

    // std::cout << "compress sample: " << gpu_ptr << " " << ptr[0]
    //           << " " << ptr[1] << " " << ptr[2] << " " << ptr[3] << " " 
    //           << ptr[4] << " " << ptr[5] << std::endl;
    
    //float grad_max = 0.0;

#ifdef TIME_CUDA
    // Create the timer
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    // Start the timer
    cudaEventRecord(start, 0);
#endif
    // if (len > 200){
    //     float *host_max_res, *dev_max_res;

    //     const unsigned int maxBlockCount = 32;
    //     const unsigned int maxThreadPerBlock = 128;
    
    //     // Allocate space for result on host
    //     host_max_res = (float*)calloc(maxBlockCount, sizeof(float));
    //     // Allocate space for result on device
    //     cudaMalloc(&dev_max_res, maxBlockCount * sizeof(float));
    //     para_max<<<maxBlockCount, maxThreadPerBlock, maxThreadPerBlock * sizeof(float)>>>(gpu_ptr, len, dev_max_res);

    //     // Copy device result to host
    //     cudaMemcpy(host_max_res, dev_max_res, maxBlockCount * sizeof(float), cudaMemcpyDeviceToHost);
    //     cudaFree(dev_max_res);

    //     // Find the maximum value across all blocks
    //     for (int i = 0; i < maxBlockCount; ++i){
    //         if (host_max_res[i] > grad_max) grad_max = host_max_res[i];
    //     }
    // }
    // else{
    //     float* grad_max_answer;
    //     cudaMalloc(&grad_max_answer, sizeof(float));
    //     //find_grad_max<<<64, 64>>>(gpu_ptr, len, grad_max_answer);
    //     find_grad_max<<<1, 1>>>(gpu_ptr, len, grad_max_answer);
    //     cudaMemcpy(&grad_max, grad_max_answer, sizeof(float), cudaMemcpyDeviceToHost);
    //     cudaFree(grad_max_answer);
    // }

#ifdef TIME_CUDA
    // Stop the timer
    cudaEventRecord(stop, 0);
    cudaEventSynchronize(stop);
    float find_max_time;
    cudaEventElapsedTime(&find_max_time, start, stop);
    std::cout << "Time to find grad_max: " << find_max_time << std::endl;
#endif

    const unsigned int threadsPerBlock = 512;
    //const unsigned int blockCount = 64;
    const unsigned int blockCount = (len + threadsPerBlock - 1) / threadsPerBlock;
    const unsigned int totalThreads = threadsPerBlock * blockCount;
    curandState *devStates;

    // /* Allocate space for results on host */
    // hostResults = (unsigned int *)calloc(totalThreads, sizeof(int));

    /* Allocate space for prng states on device */
    cudaMalloc((void**)&devStates, totalThreads * sizeof(curandState));
    //std::cout << "Done mallocing for devStates" << std::endl;

#ifdef TIME_CUDA
    // Start the timer
    cudaEventRecord(start, 0);
#endif
    /* Setup prng states */
    setup_kernel<<<blockCount, threadsPerBlock>>>(devStates);

    // scale is zero, should not divide by grad_max, just return for now
    // TODO: should investigate how to avoid compressing/decompressing parameter tensors
    if(scale) terngrad_compress_kernel<<<blockCount, threadsPerBlock>>>(gpu_ptr, len, devStates, scale);
    //terngrad_compress_kernel<<<blockCount, threadsPerBlock>>>(gpu_ptr, len, devStates, grad_max);
    //if(scale != grad_max) std::cout << "grad max: "<< grad_max << " scale: " << scale;
    //std::cout << "grad max: "<< grad_max << " host scale: " << scale;

#ifdef TIME_CUDA
    // Stop the timer
    cudaEventRecord(stop, 0);
    cudaEventSynchronize(stop);
    float find_terngrad_time;
    cudaEventElapsedTime(&find_terngrad_time, start, stop);
    std::cout << "Time to compress w/ terngrad: " << find_terngrad_time << std::endl;
#endif
#ifdef TOTAL_TIME_CUDA
    // Stop the timer
    cudaEventRecord(total_stop, 0);
    cudaEventSynchronize(total_stop);
    float total_terngrad_time;
    cudaEventElapsedTime(&total_terngrad_time, total_start, total_stop);
    std::cout << "Total time to compress w/ terngrad: " << total_terngrad_time << std::endl;
#endif
    /* Cleanup */
    cudaFree(devStates);
#ifdef TIME_CUDA
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
#endif
#ifdef TOTAL_TIME_CUDA
    cudaEventDestroy(total_start);
    cudaEventDestroy(total_stop);
#endif
    return scale;
}

void terngrad_decompress(const void* gpu_ptr, float scale, size_t len){
    // This is a tensor used for push-pulling the accuracy
    // TODO: in the future. just don't call terngrad_compress and decompress on it
    if (len <= 2) return;

    // maybe we can just "throw away" the last floating point value of the old
    // gradient (assume it's zero) and use that space to store the scale
    // Later we can just play with the size of the tensor when we initailze them (shrink for compression
    // + 1 for storing the scale)
    const unsigned int threadsPerBlock = 512;
    //const unsigned int blockCount = 64;
    const unsigned int blockCount = (len + threadsPerBlock - 1) / threadsPerBlock;
    if(scale) terngrad_decompress_kernel<<<blockCount, threadsPerBlock>>>(gpu_ptr, len, scale);
    return;
}
