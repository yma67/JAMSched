#pragma once
#include <cuda_runtime_api.h>
#define N_LINES 256
#define MAX_TESSELLATION 32
#define BLOCK_DIM 32
void *GetBezierLinesInitJAMC();
void FreeBezierLinesInitJAMC(void *handle);
void ComputeBezierLinesJAMC(int idxLine, void* handle, cudaStream_t s);
void ComputeBezierLinesCPU(int idxLine, void* handle);
