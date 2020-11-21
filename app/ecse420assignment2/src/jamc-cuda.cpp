//
// Created by mayuxiang on 2020-11-11.
//
#include "jamc-cuda.h"
#include "io/cuda-wrapper.h"

cudaError_t WaitForCudaStream(cudaStream_t s)
{
    return jamc::cuda::WaitStream(s);
}
