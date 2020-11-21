//
// Created by mayuxiang on 2020-11-11.
//

#ifndef JAMSCRIPT_KERNEL_H
#define JAMSCRIPT_KERNEL_H
#include "cuda_runtime.h"
#include "LogicalInput.h"
#include <vector>
void KernelInvoker(cudaStream_t, int*, int*, int*);
void Assignment1(cudaStream_t, g26::LogicalInput*, g26::LogicalInput*, g26::LogicalInput*, g26::LogicalInput*, unsigned int);

#endif //JAMSCRIPT_KERNEL_H
