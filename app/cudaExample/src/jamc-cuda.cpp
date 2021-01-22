//
// Created by mayuxiang on 2020-11-11.
//
#include "jamc-cuda.h"
#include "io/cuda-wrapper.h"
#include "concurrency/mutex.hpp"
#include "concurrency/waitgroup.hpp"
#include <random>
#include <nvToolsExt.h>
#include <nvToolsExtCuda.h>

void BoostGPUForCoroutine()
{
    jamc::Timer::SetGPUSampleRate(std::chrono::nanoseconds(0));
}

cudaError_t WaitForCudaStream(cudaStream_t s)
{
    return jamc::cuda::WaitStream(s, 4);
}

cudaError_t WaitForCudaStreamWithRounds(cudaStream_t s, size_t n)
{
    return jamc::cuda::WaitStream(s, n);
}

void LaunchKernel(const std::function<void()>& f)
{
    return jamc::cuda::CUDAPooler::GetInstance().EnqueueKernel(f);
}

jamc::Mutex& GetCudaKernelLaunchLockInstance()
{
    static jamc::Mutex mtxCudaLaunchKernel;
    return mtxCudaLaunchKernel;
}

void *CreateWaitGroup() {
    return new jamc::WaitGroup();
}

void DestroyWaitGroup(void *lpHandle) {
    auto wg = reinterpret_cast<jamc::WaitGroup*>(lpHandle);
    delete wg;
}

void DoneWaitGroup(void *lpHandle) {
    auto wg = reinterpret_cast<jamc::WaitGroup*>(lpHandle);
    wg->Done();
}

void AddWaitGroup(void *lpHandle, int i) {
    auto wg = reinterpret_cast<jamc::WaitGroup*>(lpHandle);
    wg->Add(i);
}

void WaitForWaitGroup(void *lpHandle) {
    auto wg = reinterpret_cast<jamc::WaitGroup*>(lpHandle);
    wg->Wait();
}

void CudaLaunchBefore()
{
    GetCudaKernelLaunchLockInstance().lock();
}

void CudaLaunchAfter()
{
    GetCudaKernelLaunchLockInstance().unlock();
}

constexpr int kInnerProductSize = 128;

std::vector<int> GetRandomArray(int * ha, int * hb, int sz, int fsz)
{
    nvtxRangeId_t id2 = nvtxRangeStart("GetRandomArray");
    std::vector<int> res(fsz, 0);
    std::minstd_rand generator;
    std::uniform_int_distribution<> distribution(1, 6);
    for ( int i = 0; i < fsz; i += sz) {
        int cumu = 0;
        for (int j = 0; j < sz; j++) {
            ha[i + j] = distribution(generator);
            hb[i + j] = distribution(generator);
        }
        for (int j = 0; j < kInnerProductSize; j++) {
            cumu += ha[i + j] * hb[i + j];
        }
        for (int j = 0; j < sz; j++) {
            res[i + j] = cumu;
            cumu -= ha[i + j] * hb[i + j];
            cumu += ha[i + ((j + kInnerProductSize) % sz)] * hb[i + ((j + kInnerProductSize) % sz)];
        }
    }
    nvtxRangeEnd(id2);
    return res;
}
