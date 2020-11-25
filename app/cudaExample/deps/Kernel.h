//
// Created by mayuxiang on 2020-11-11.
//

#ifndef JAMSCRIPT_KERNEL_H
#define JAMSCRIPT_KERNEL_H
#include "cuda_runtime.h"
#include "LogicalInput.h"
#include "jamc-cuda.h"
#ifdef __CUDACC__
__global__
#endif
void CircularSubarrayInnerProduct( int * a, int * b, int * c, int size);
void KernelInvoker(cudaStream_t, int*, int*, int*, int*, int*, int*, int, int);

void InitDummy();
#endif //JAMSCRIPT_KERNEL_H
