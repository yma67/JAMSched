//
// Created by mayuxiang on 2020-11-11.
//

#ifndef JAMSCRIPT_JAMC_CUDA_H
#define JAMSCRIPT_JAMC_CUDA_H
#include "cuda_runtime.h"
#include <vector>
#include <functional>
#ifdef __cplusplus
extern "C" {
#endif
cudaError_t WaitForCudaStream(cudaStream_t);
cudaError_t WaitForCudaStreamWithRounds(cudaStream_t, size_t);
void *CreateWaitGroup();
void BoostGPUForCoroutine();
void DestroyWaitGroup(void *);
void DoneWaitGroup(void *lpHandle);
void AddWaitGroup(void *lpHandle, int i);
void WaitForWaitGroup(void *lpHandle);
void CudaLaunchBefore();
void CudaLaunchAfter();
#ifdef __cplusplus
}
void LaunchKernel(const std::function<void()>& f);
std::vector<int> GetRandomArray(int * ha, int * hb, int sz, int fsz);
#endif
#endif //JAMSCRIPT_JAMC_CUDA_H
