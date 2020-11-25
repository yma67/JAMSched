//
// Created by mayuxiang on 2020-11-11.
//

#ifndef JAMSCRIPT_JAMC_CUDA_H
#define JAMSCRIPT_JAMC_CUDA_H
#include "cuda_runtime.h"
#include <vector>
#ifdef __cplusplus
extern "C" {
#endif
cudaError_t WaitForCudaStream(cudaStream_t);
#ifdef __cplusplus
}
std::vector<int> GetRandomArray(int * ha, int * hb, int sz, int fsz);
#endif
#endif //JAMSCRIPT_JAMC_CUDA_H
