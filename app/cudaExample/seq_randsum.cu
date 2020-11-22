//
// Created by mayuxiang on 2020-11-21.
//
#include <cstdint>
#include <random>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include "cuda_runtime.h"

constexpr bool useThread = true;
constexpr int kNumTrails = 128;
constexpr size_t kPerDimLen = 256;
constexpr size_t kNumIteration = 8;

__global__
void vector_add( int * a, int * b, int * c, int size) {
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    int lim = idx + 256;
    if (lim > size) lim = size;
    for (int i = idx; i < lim; i++) c[idx] += a[i] * b[i];
    if (idx < size) {
        c[idx] = a[idx] + b[idx];
    }
}

static void Compute() {
    int *host_a, *host_b, *host_c, *dev_a, *dev_b, *dev_c;
    std::vector<int> result;
	cudaStream_t stream;
	cudaStreamCreate(&stream);
	auto res1 = cudaHostAlloc(&host_a, kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), cudaHostAllocDefault);
	auto res2 = cudaHostAlloc(&host_b, kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), cudaHostAllocDefault);
	auto res3 = cudaHostAlloc(&host_c, kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), cudaHostAllocDefault);
	if (res1 != cudaSuccess) {
	    printf("shut up 1\n");
	    return;
	}
	if (res2 != cudaSuccess) {
	    printf("shut up 3\n");
	    return;
	}
	if (res3 != cudaSuccess) {
	    printf("shut up 3\n");
	    return;
	}
	cudaMalloc((void**)(&dev_a), kPerDimLen * kPerDimLen * sizeof( int) );
	cudaMalloc((void**)(&dev_b), kPerDimLen * kPerDimLen * sizeof( int) );
	cudaMalloc((void**)(&dev_c), kPerDimLen * kPerDimLen * sizeof( int) );
	{
	    std::minstd_rand generator;
	    std::uniform_int_distribution<> distribution(1, 6);
	    for ( int i = 0; i < kPerDimLen * kPerDimLen * kNumIteration; ++i) {
		host_a[i] = distribution(generator);
		host_b[i] = distribution(generator);
		result.push_back(host_a[i] + host_b[i]);
	    }
	}
	for ( int i = 0; i < kPerDimLen * kPerDimLen * kNumIteration; i += kPerDimLen * kPerDimLen) {
	    cudaMemcpyAsync( dev_a, host_a + i, kPerDimLen * kPerDimLen * sizeof( int), cudaMemcpyHostToDevice, stream);
	    cudaMemcpyAsync( dev_b, host_b + i, kPerDimLen * kPerDimLen * sizeof( int), cudaMemcpyHostToDevice, stream);
	    vector_add<<<kPerDimLen * kPerDimLen / 256, 256, 0, stream>>>(dev_a, dev_b, dev_c, kPerDimLen * kPerDimLen);
	    cudaMemcpyAsync( host_c + i, dev_c, kPerDimLen * kPerDimLen * sizeof( int), cudaMemcpyDeviceToHost, stream);
	}
	cudaStreamSynchronize(stream);
	for (int i = 0; i < kPerDimLen * kPerDimLen * kNumIteration; i++) assert(result[i] == host_c[i]);
	cudaFree(dev_a);
	cudaFree( dev_b);
	cudaFree( dev_c);
	cudaFreeHost(host_a);
	cudaFreeHost(host_b);
	cudaFreeHost(host_c);
	cudaStreamDestroy(stream);
}

int main() {
    std::vector<std::thread> px;
    auto startCuda = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kNumTrails; i++) {
        if constexpr(useThread) {
            px.emplace_back(Compute);
        } else {
            Compute();
        }
    }
    if constexpr(useThread) {
         for (auto& p: px) p.join();   
    }
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
    std::cout << "CPU time: " << dur << " us" << std::endl;
    return 0;
}
