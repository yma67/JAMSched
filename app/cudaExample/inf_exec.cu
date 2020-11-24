//
// Created by mayuxiang on 2020-11-21.
//
#include <cstdint>
#include <random>
#include <chrono>
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include "cuda_runtime.h"
#include "deps/Kernel.h"

constexpr int kSize { 256 * 256 * 256 * 8 };

struct GpuTimer
{
    cudaEvent_t start;
    cudaEvent_t stop;

    GpuTimer()
    {
        cudaEventCreate(&start);
        cudaEventCreate(&stop);
    }

    ~GpuTimer()
    {
        cudaEventDestroy(start);
        cudaEventDestroy(stop);
    }
    void Start();
    void Stop();
    float Elapsed();
};

inline void GpuTimer::Start()
{
    cudaEventRecord(start, 0);
}

inline void GpuTimer::Stop()
{
    cudaEventRecord(stop, 0);
}

inline float GpuTimer::Elapsed()
{
    float elapsed;
    cudaEventSynchronize(stop);
    cudaEventElapsedTime(&elapsed, start, stop);
    return elapsed;
}

static void Compute() {
    int *host_a, *host_b, *host_c, *dev_a, *dev_b, *dev_c;
    auto res1 = cudaHostAlloc(&host_a, kSize * sizeof(int), cudaHostAllocDefault);
    auto res2 = cudaHostAlloc(&host_b, kSize * sizeof(int), cudaHostAllocDefault);
    auto res3 = cudaHostAlloc(&host_c, kSize * sizeof(int), cudaHostAllocDefault);
    if (res1 != cudaSuccess) {
        printf("hostAlloc Error 1\n");
        return;
    }
    if (res2 != cudaSuccess) {
        printf("hostAlloc Error 3\n");
        return;
    }
    if (res3 != cudaSuccess) {
        printf("hostAlloc Error 3\n");
        return;
    }
    cudaMalloc((void**)(&dev_a), kSize * sizeof( int) );
    cudaMalloc((void**)(&dev_b), kSize * sizeof( int) );
    cudaMalloc((void**)(&dev_c), kSize * sizeof( int) );
    auto result = GetRandomArray(host_a, host_b, kSize, kSize);
    cudaMemcpy( dev_a, host_a, kSize * sizeof( int), cudaMemcpyHostToDevice);
    cudaMemcpy( dev_b, host_b, kSize * sizeof( int), cudaMemcpyHostToDevice);
    cudaDeviceSynchronize();
    GpuTimer t;
    t.Start();
    auto startCuda = std::chrono::high_resolution_clock::now();
    CircularSubarrayInnerProduct<<<kSize / 256, 256>>>(dev_a, dev_b, dev_c, kSize);
    /*for (int i = 0; i < kSize; i++) {
        host_c[i] = 0;
        for (int j = i; j < i + 128; j++) {
            host_c[i] += host_a[j % kSize] * host_b[j % kSize];
        }
    }*/
    cudaDeviceSynchronize();
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
    t.Stop();
    cudaMemcpy( host_c, dev_c, kSize * sizeof( int), cudaMemcpyDeviceToHost);
    cudaDeviceSynchronize();
    for (long i = 0; i < kSize; i++) assert(result[i] == host_c[i]);
    std::cout << "Kernel GPU time: " << t.Elapsed() << " us" << std::endl;
    std::cout << "Kernel CPU time: " << dur << " us" << std::endl;
    cudaFree(dev_a);
    cudaFree( dev_b);
    cudaFree( dev_c);
    cudaFreeHost(host_a);
    cudaFreeHost(host_b);
    cudaFreeHost(host_c);
}

int main() {
    auto startCuda = std::chrono::high_resolution_clock::now();
    int dn;
    cudaGetDevice(&dn);
    Compute();
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
    std::cout << "CPU time: " << dur << " us" << std::endl;
    return 0;
}
