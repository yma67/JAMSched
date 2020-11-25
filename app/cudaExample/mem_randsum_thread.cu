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
#include <atomic>
#include <boost/lockfree/stack.hpp>
#include <nvToolsExt.h>
#include <nvToolsExtCuda.h>
#include "deps/Kernel.h"
#include "deps/cnmem.h"
#include "cuda_runtime.h"

// change CNMEMMutexTypeDef to std::mutex

struct HostMemory {
    int *host_a, *host_b, *host_c;
};

constexpr int kNumTrails = 256;
constexpr int kPerDimLen = 256;
constexpr int kNumIteration = 8;
constexpr int perIterHostPerArray = kNumIteration * kPerDimLen * kPerDimLen;
constexpr int perIterHostTotal = perIterHostPerArray * 3;
constexpr int perIterDeviceArray = kPerDimLen * kPerDimLen;
constexpr int perIterDeviceTotal = perIterDeviceArray * 3;

std::atomic<int> hitCount{0}, hitCountStream{0};
boost::lockfree::stack<HostMemory> hostMemory;
boost::lockfree::stack<cudaStream_t> cudaStreams;

HostMemory jamcHostAlloc() {
    nvtxRangeId_t id0 = nvtxRangeStart("jamcHostAlloc");
    HostMemory h{};
    if (!hostMemory.pop(h)) {
        if (cudaSuccess != cudaHostAlloc(&(h.host_a), perIterHostTotal * sizeof(int), cudaHostAllocDefault)) {
            printf("bad allocation\n");
        } else {
            h.host_b = h.host_a + perIterHostPerArray;
            h.host_c = h.host_a + 2 * perIterHostPerArray;
        }
    } else {
        hitCount++;
    }
    nvtxRangeEnd(id0);
    return h;
}

void jamcHostFree(HostMemory h) {
    nvtxRangeId_t id0 = nvtxRangeStart("jamcHostFree");
    hostMemory.push(h);
    nvtxRangeEnd(id0);
}

cudaStream_t jamcStreamAlloc() {
    nvtxRangeId_t id0 = nvtxRangeStart("jamcStreamAlloc");
    cudaStream_t s;
    if (!cudaStreams.pop(s)) {
        if (cudaSuccess != cudaStreamCreateWithFlags(&s, cudaStreamNonBlocking)) {
            printf("bad allocation stream\n");
            exit(0);
        } else {
            cnmemRegisterStream(s);
        }
    } else {
        hitCountStream++;
    }
    nvtxRangeEnd(id0);
    return s;
}

void jamcStreamFree(cudaStream_t s) {
    nvtxRangeId_t id0 = nvtxRangeStart("jamcStreamFree");
    cudaStreams.push(s);
    nvtxRangeEnd(id0);
}

static void Compute(int k) {
    auto h = jamcHostAlloc();
    int *dev_a, *dev_b, *dev_c;
    auto stream = jamcStreamAlloc();
    nvtxRangeId_t id0 = nvtxRangeStart("cnmemMalloc dev_a");
    cnmemMalloc((void**)(&dev_a), kPerDimLen * kPerDimLen * sizeof(int), stream);
    nvtxRangeEnd(id0);
    nvtxRangeId_t id1 = nvtxRangeStart("cnmemMalloc dev_b");
    cnmemMalloc((void**)(&dev_b), kPerDimLen * kPerDimLen * sizeof(int), stream);
    nvtxRangeEnd(id1);
    nvtxRangeId_t id2 = nvtxRangeStart("cnmemMalloc dev_c");
    cnmemMalloc((void**)(&dev_c), kPerDimLen * kPerDimLen * sizeof(int), stream);
    nvtxRangeEnd(id2);
    int size = kPerDimLen * kPerDimLen;
    auto result = GetRandomArray(h.host_a, h.host_b, size, perIterHostPerArray);
    for ( int i = 0; i < perIterHostPerArray; i += size) {
        cudaMemcpyAsync( dev_a, h.host_a + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
        cudaMemcpyAsync( dev_b, h.host_b + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
        CircularSubarrayInnerProduct<<<size / 256, 256, 0, stream>>>(dev_a, dev_b, dev_c, size);
        cudaMemcpyAsync( h.host_c + i, dev_c, size * sizeof( int), cudaMemcpyDeviceToHost, stream);
    }
    cudaStreamSynchronize(stream);
    for (int i = 0; i < perIterHostPerArray; i++) if (result[i] != h.host_c[i]) perror("wrong result");
    nvtxRangeId_t idf0 = nvtxRangeStart("cnmemFree dev_a");
    cnmemFree(dev_a, stream);
    nvtxRangeEnd(idf0);
    nvtxRangeId_t idf1 = nvtxRangeStart("cnmemFree dev_b");
    cnmemFree(dev_b, stream);
    nvtxRangeEnd(idf1);
    nvtxRangeId_t idf2 = nvtxRangeStart("cnmemFree dev_c");
    cnmemFree(dev_c, stream);
    nvtxRangeEnd(idf2);
    jamcHostFree(h);
    jamcStreamFree(stream);
}

int main(int argc, char* argv[]) {
    InitDummy();
    std::vector<std::thread> pendings;
    auto startCuda = std::chrono::high_resolution_clock::now();
    cnmemDevice_t memDevice;
    memDevice.device = 0;
    memDevice.size = kNumTrails * perIterDeviceTotal * sizeof(int);
    memDevice.numStreams = 0;
    memDevice.streams = nullptr;
    memDevice.streamSizes = nullptr;
    cnmemInit(1, &memDevice, CNMEM_FLAGS_DEFAULT);
    for (int k = 0; k < kNumTrails; k++) pendings.emplace_back([k] { Compute(k); });
    for (auto& p: pendings) p.join();
    pendings.clear();
    hostMemory.consume_all([&pendings](const HostMemory& h) { 
        pendings.emplace_back([&h] { cudaFreeHost(h.host_a); });
    });
    cudaStreams.consume_all([&pendings](const cudaStream_t& h) {
        pendings.emplace_back([&h] { cudaStreamDestroy(h); }); 
    });
    for (auto& p: pendings) p.join();
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
    std::cout << "CPU time: " << dur << " us, alloc cache hit = " << float(hitCount) / float(kNumTrails) 
                                     << "%, stream cache hit = " << float(hitCountStream) / float(kNumTrails)  << "%" << std::endl;
    return 0;
}
