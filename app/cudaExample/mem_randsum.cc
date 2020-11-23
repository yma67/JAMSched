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
#include <boost/lockfree/stack.hpp>
#include <jamscript>
#include <nvToolsExt.h>
#include <nvToolsExtCuda.h>
#include "../deps/Kernel.h"
#include "../deps/cnmem.h"
#include "cuda_runtime.h"


struct HostMemory {
    int *host_a, *host_b, *host_c;
};

constexpr int kNumTrails = 512;
constexpr size_t kPerDimLen = 256;
constexpr size_t kNumIteration = 8;
size_t sizes[kNumTrails];
cudaStream_t streams[kNumTrails];
boost::lockfree::stack<HostMemory> hostMemory;

HostMemory jamcHostAlloc() {
    nvtxRangeId_t id0 = nvtxRangeStart("jamcHostAlloc");
    HostMemory h{};
    hostMemory.pop(h);
    nvtxRangeEnd(id0);
    return h;
}

void jamcHostFree(HostMemory h) {
    nvtxRangeId_t id0 = nvtxRangeStart("jamcHostFree");
    hostMemory.push(h);
    nvtxRangeEnd(id0);
}

static void Compute(int k) {
    auto h = jamcHostAlloc();
    int *dev_a, *dev_b, *dev_c;
    nvtxRangeId_t id0 = nvtxRangeStart("cnmemMalloc dev_a");
    cnmemMalloc((void**)(&dev_a), kPerDimLen * kPerDimLen * sizeof(int), streams[k]);
    nvtxRangeEnd(id0);
    nvtxRangeId_t id1 = nvtxRangeStart("cnmemMalloc dev_b");
    cnmemMalloc((void**)(&dev_b), kPerDimLen * kPerDimLen * sizeof(int), streams[k]);
    nvtxRangeEnd(id1);
    nvtxRangeId_t id2 = nvtxRangeStart("cnmemMalloc dev_c");
    cnmemMalloc((void**)(&dev_c), kPerDimLen * kPerDimLen * sizeof(int), streams[k]);
    nvtxRangeEnd(id2);
    KernelInvoker(streams[k], h.host_a, h.host_b, h.host_c, dev_a, dev_b, dev_c, kPerDimLen * kPerDimLen, kNumIteration);
    nvtxRangeId_t idf0 = nvtxRangeStart("cnmemFree dev_a");
    cnmemFree(dev_a, streams[k]);
    nvtxRangeEnd(idf0);
    nvtxRangeId_t idf1 = nvtxRangeStart("cnmemFree dev_b");
    cnmemFree(dev_b, streams[k]);
    nvtxRangeEnd(idf1);
    nvtxRangeId_t idf2 = nvtxRangeStart("cnmemFree dev_c");
    cnmemFree(dev_c, streams[k]);
    nvtxRangeEnd(idf2);
    jamcHostFree(h);
}

int main(int argc, char* argv[]) {
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    auto nThreads = std::atoi(argv[1]);
    for (int i = 0; i < nThreads; i++) vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(false, 1024 * 256, true, false), jamc::Duration::max(), [&ribScheduler] {
        std::vector<jamc::TaskHandle> pendings;
        int *hostMemoryPtr;
        
        constexpr size_t perIterHostPerArray = kNumIteration * kPerDimLen * kPerDimLen;
        constexpr size_t perIterHostTotal = perIterHostPerArray * 3;
        constexpr size_t perIterDeviceArray = kPerDimLen * kPerDimLen;
        constexpr size_t perIterDeviceTotal = perIterDeviceArray * 3;
        cudaHostAlloc(&hostMemoryPtr, kNumTrails * perIterHostTotal * sizeof(int), cudaHostAllocDefault);
        for (int i = 0; i < kNumTrails; i++) {
            cudaStreamCreateWithFlags(&streams[i], cudaStreamNonBlocking);
            sizes[i] = perIterDeviceTotal * sizeof(int);
            HostMemory h{};
            h.host_a = hostMemoryPtr + i * perIterHostTotal;
            h.host_b = hostMemoryPtr + i * perIterHostTotal + perIterHostPerArray;
            h.host_c = hostMemoryPtr + i * perIterHostTotal + 2 * perIterHostPerArray;
            hostMemory.push(h);
        }
        cnmemDevice_t memDevice;
        memDevice.device = 0;
        memDevice.size = kNumTrails * perIterDeviceTotal * sizeof(int);
        memDevice.numStreams = kNumTrails;
        memDevice.streams = streams;
        memDevice.streamSizes = sizes;
        cnmemInit(1, &memDevice, CNMEM_FLAGS_DEFAULT);
        auto startCuda = std::chrono::high_resolution_clock::now();
        for (int k = 0; k < kNumTrails; k++) pendings.emplace_back(ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true, false), jamc::Duration::max(), [k] { Compute(k); }));
        for (auto& p: pendings) p.Join();
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
        std::cout << "CPU time: " << dur << " us" << std::endl;
        cnmemFinalize();
        cudaFreeHost(hostMemoryPtr);
        for (int i = 0; i < kNumTrails; i++) cudaStreamDestroy(streams[i]);
        
        ribScheduler.ShutDown();
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
