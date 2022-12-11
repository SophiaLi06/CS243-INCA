#include <cuda_runtime.h>
#include <iostream>

void test_wrapper(void);

void test_wrapper(void* gpu_ptr);

void test_wrapper(void* gpu_ptr, const void* data_ptr, int offset);

void test_mul_wrapper(const void* p, size_t len);

void test_div_wrapper(const void* p, size_t len);

void test_clipping(const void* p, size_t len);

void test_quan_wrapper(const void* p, size_t len);
