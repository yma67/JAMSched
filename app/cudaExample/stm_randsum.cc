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
#include "scheduler/scheduler.hpp"
#include "../deps/Kernel.h"
#include "cuda_runtime.h"

constexpr int kNumTrails = 256;
constexpr int kPerDimLen = 256;
constexpr int kNumIteration = 8;

static void Compute() {
    int *host_a, *host_b, *host_c, *dev_a, *dev_b, *dev_c;
    std::vector<int> result;
    cudaStream_t stream;
    cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
    auto res1 = cudaHostAlloc(&host_a, kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), cudaHostAllocDefault);
    auto res2 = cudaHostAlloc(&host_b, kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), cudaHostAllocDefault);
    auto res3 = cudaHostAlloc(&host_c, kPerDimLen * kPerDimLen * kNumIteration * sizeof(int), cudaHostAllocDefault);
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
    cudaMalloc((void**)(&dev_a), kPerDimLen * kPerDimLen * sizeof(int));
    cudaMalloc((void**)(&dev_b), kPerDimLen * kPerDimLen * sizeof(int));
    cudaMalloc((void**)(&dev_c), kPerDimLen * kPerDimLen * sizeof(int));
    KernelInvoker(stream, host_a, host_b, host_c, dev_a, dev_b, dev_c, kPerDimLen * kPerDimLen, kNumIteration);
    cudaFree(dev_a);
    cudaFree(dev_b);
    cudaFree(dev_c);
    cudaFreeHost(host_a);
    cudaFreeHost(host_b);
    cudaFreeHost(host_c);
    cudaStreamDestroy(stream);
}

int main(int argc, char* argv[]) {
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    auto nThreads = std::atoi(argv[1]);
    InitDummy();
    for (int i = 0; i < nThreads; i++) vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(false, 4096 * 2, true, false), jamc::Duration::max(), [&ribScheduler] {
        std::vector<jamc::TaskHandle> pendings;
        auto startCuda = std::chrono::high_resolution_clock::now();
        for (int k = 0; k < kNumTrails; k++) {
            pendings.emplace_back(ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true, false), jamc::Duration::max(), Compute));
        }
        for (auto& p: pendings) p.Join();
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
        std::cout << "CPU time: " << dur << " us" << std::endl;
        ribScheduler.ShutDown();
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
