#include <cuda.h>
#include <curand_kernel.h>
#include <cuda_runtime.h>

//#define TOTAL_TIME_CUDA
//#define TIME_CUDA

float terngrad_scale(const void* gpu_ptr, size_t len);

float terngrad_compress(const void* gpu_ptr, size_t len, float scale);

void terngrad_decompress(const void* gpu_ptr, float scale, size_t len);
