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
        // if (cudaSuccess != cudaStreamCreate(&s)) {
            printf("bad allocation stream\n");
            exit(0);
        } else {
            cnmemRegisterStream(s);
            cnlockedRegisterStream(s);
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
    int *host_a, *host_b, *host_c, *dev_a, *dev_b, *dev_c;
    auto stream = jamcStreamAlloc();
    cnlockedMalloc((void**)(&host_a), kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), stream);
    cnlockedMalloc((void**)(&host_b), kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), stream);
    cnlockedMalloc((void**)(&host_c), kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), stream);
    nvtxRangeId_t id0 = nvtxRangeStart("cnmemMalloc dev_a");
    cnmemMallocOnDevice((void**)(&dev_a), kPerDimLen * kPerDimLen * sizeof(int), stream, 0);
    nvtxRangeEnd(id0);
    auto result = GetRandomArray(host_a, host_b, kPerDimLen * kPerDimLen, kPerDimLen * kPerDimLen * kNumIteration);
    nvtxRangeId_t id1 = nvtxRangeStart("cnmemMalloc dev_b");
    cnmemMallocOnDevice((void**)(&dev_b), kPerDimLen * kPerDimLen * sizeof(int), stream, 0);
    nvtxRangeEnd(id1);
    nvtxRangeId_t id2 = nvtxRangeStart("cnmemMalloc dev_c");
    cnmemMallocOnDevice((void**)(&dev_c), kPerDimLen * kPerDimLen * sizeof(int), stream, 0);
    nvtxRangeEnd(id2);
    KernelInvoker(stream, host_a, host_b, host_c, dev_a, dev_b, dev_c, kPerDimLen * kPerDimLen, kNumIteration, result);
    nvtxRangeId_t idf0 = nvtxRangeStart("cnmemFree dev_a");
    cnmemFree(dev_a, stream);
    nvtxRangeEnd(idf0);
    nvtxRangeId_t idf1 = nvtxRangeStart("cnmemFree dev_b");
    cnmemFree(dev_b, stream);
    nvtxRangeEnd(idf1);
    nvtxRangeId_t idf2 = nvtxRangeStart("cnmemFree dev_c");
    cnmemFree(dev_c, stream);
    nvtxRangeEnd(idf2);
    cnlockedFree(host_a, stream);
    cnlockedFree(host_b, stream);
    cnlockedFree(host_c, stream);
    jamcStreamFree(stream);
}

int main(int argc, char* argv[]) {
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    auto nThreads = std::atoi(argv[1]);
    InitDummy();
    // jamc::Timer::SetGPUSampleRate(std::chrono::nanoseconds(0));
    for (int i = 0; i < nThreads; i++) vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(false, 1024 * 256, true), jamc::Duration::max(), [&ribScheduler, nThreads] {
        std::vector<jamc::TaskHandle> pendings;
        jamc::WaitGroup wg;
        auto startCuda = std::chrono::high_resolution_clock::now();
        cnmemDevice_t memDevice, memHost;
        memDevice.device = 0;
        memDevice.size = 0;
        memDevice.numStreams = 0;
        memDevice.streams = nullptr;
        memDevice.streamSizes = nullptr;
        memHost.device = 0;
        memHost.size = kNumTrails * perIterDeviceTotal * sizeof(int);
        memHost.numStreams = 0;
        memHost.streams = nullptr;
        memHost.streamSizes = nullptr;
        auto ret = jamc::async([&] { cnmemInit(1, &memDevice, CNMEM_FLAGS_DEFAULT); return 0; });
        cnlockedInit(1, &memHost, CNMEM_FLAGS_HOST_LOCKED);
        ret.wait();
        for (int k = 0; k < kNumTrails; k++) {
            pendings.emplace_back(ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true), jamc::Duration::max(), [k] { Compute(k); }));
            //auto stx = std::chrono::high_resolution_clock::now();
            //constexpr auto sleepCount = std::chrono::milliseconds(5);
            //jamc::ctask::SleepFor(sleepCount);
            //std::cout << "Sleep Jitter - " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - stx - sleepCount).count() << " us" << std::endl;
        }
        for (auto& p: pendings) p.Join();
        hostMemory.consume_all([&wg, &ribScheduler](const HostMemory& h) { 
            wg.Add();
            ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true, false), jamc::Duration::max(), [&h, &wg] { 
                cudaFreeHost(h.host_a); wg.Done();
            });
        });
        cudaStreams.consume_all([&wg, &ribScheduler](const cudaStream_t& h) { 
            wg.Add();
            ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true, false), jamc::Duration::max(), [&h, &wg] { 
                cudaStreamDestroy(h); wg.Done();
            });    
        });
        wg.Add();
        ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true, false), jamc::Duration::max(), [&wg] { 
            cnlockedFinalize(); wg.Done();
        });
        cnmemFinalize();
        wg.Wait();
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
        std::cout << "CPU time: " << dur << " us, alloc cache hit = " << float(hitCount) / float(kNumTrails) 
                                         << "%, stream cache hit = " << float(hitCountStream) / float(kNumTrails)  << "%" << std::endl;
        ribScheduler.ShutDown();
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
