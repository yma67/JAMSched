//
// Created by mayuxiang on 2020-11-11.
//

#ifndef JAMSCRIPT_JAMC_CUDA_H
#define JAMSCRIPT_JAMC_CUDA_H
#include "cuda_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
cudaError_t WaitForCudaStream(cudaStream_t);
#ifdef __cplusplus
}
#endif
#endif //JAMSCRIPT_JAMC_CUDA_H
