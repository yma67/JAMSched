/**
 * Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */
#include "bzrline_cdp.cuh"
#include "io/cuda-wrapper.h"
#include <stdio.h>
#include <cuda_runtime_api.h>
#include <string.h>
#include <tuple>
#include <memory>
#include "../deps/jamc-cuda.h"
#include "../deps/cnmem.h"
#include <cuda.h>

struct CPx3 {
    float2 CP[3];
};

__forceinline__ __device__ __host__ float2 operator+(float2 a, float2 b) {
    float2 c;
    c.x = a.x + b.x;
    c.y = a.y + b.y;
    return c;
}

__forceinline__ __device__ __host__ float2 operator-(float2 a, float2 b) {
    float2 c;
    c.x = a.x - b.x;
    c.y = a.y - b.y;
    return c;
}

__forceinline__ __device__ __host__ float2 operator*(float a, float2 b) {
    float2 c;
    c.x = a * b.x;
    c.y = a * b.y;
    return c;
}

__forceinline__ __device__ __host__ float length(float2 a) {
    return sqrtf(a.x*a.x + a.y*a.y);
}

__global__ void computeBezierLinePositions(CPx3 cpx3, float2 *bLines, int nTessPoints) {
    register int idx = threadIdx.x + blockDim.x*blockIdx.x;

    if (idx < nTessPoints) {
        register float u = (float)idx/(float)(nTessPoints-1);
        register float omu = 1.0f - u;

        register float B3u[3];

        B3u[0] = omu*omu;
        B3u[1] = 2.0f*u*omu;
        B3u[2] = u*u;

        register float2 position = {0,0};

        for (int i = 0; i < 3; i++)
        {
            position = position + B3u[i] * cpx3.CP[i];
        }

        bLines[idx] = position;
    }
}

void ComputeBezierLinesJAMC(int idxLine, void* handle, cudaStream_t s) {
#define useSingleThreadLaunch 0
    auto& cpx3 = (reinterpret_cast<CPx3*>(handle))[idxLine];
    float2* vertexPos;
    float curvature = length(cpx3.CP[1] - 0.5f*(cpx3.CP[0] + cpx3.CP[2]))/length(cpx3.CP[2] - cpx3.CP[0]);
    int nTessPoints = min(max((int)(curvature*16.0f),4),MAX_TESSELLATION);
    cnmemMallocOnDevice((void **)(&vertexPos), nTessPoints * sizeof(float2), s, 0);
    auto args = std::make_shared<jamc::cuda::CommandArgs<CPx3, float2*, int>>(cpx3, vertexPos, nTessPoints);
#ifdef useSingleThreadLaunch
    auto wg = CreateWaitGroup();
    AddWaitGroup(wg, 1);
    LaunchKernel([args, nTessPoints, s, wg] {
#endif
        cudaLaunchKernel((void*)computeBezierLinePositions, dim3(ceil((float)nTessPoints/MAX_TESSELLATION), 1, 1), dim3(MAX_TESSELLATION, 1, 1), args->GetCudaKernelArgs(), 0, s);
#ifdef useSingleThreadLaunch
        DoneWaitGroup(wg);
    });
    WaitForWaitGroup(wg);
    DestroyWaitGroup(wg);
#endif
    WaitForCudaStreamWithRounds(s, 4);
    cnmemFree(vertexPos, s);
}

void ComputeBezierLinesCPU(int idxLine, void* handle) {
    auto& cpx3 = (reinterpret_cast<CPx3*>(handle))[idxLine];
    float curvature = length(cpx3.CP[1] - 0.5f*(cpx3.CP[0] + cpx3.CP[2]))/length(cpx3.CP[2] - cpx3.CP[0]);
    int nTessPoints = min(max((int)(curvature*16.0f),4),MAX_TESSELLATION);
    float2 vertexPos[nTessPoints];
    for (int idx = 0; idx < nTessPoints; idx++) {
        float u = (float)idx/(float)(nTessPoints-1);
        float omu = 1.0f - u;

        float B3u[3];

        B3u[0] = omu*omu;
        B3u[1] = 2.0f*u*omu;
        B3u[2] = u*u;

        float2 position = {0,0};

        for (int i = 0; i < 3; i++)
        {
            position = position + B3u[i] * cpx3.CP[i];
        }

        vertexPos[idx] = position;
    }
}

void *GetBezierLinesInitJAMC() {
    CPx3 *bLines_h = new CPx3[N_LINES];
    float2 last = {0,0};
    for (int i = 0; i < N_LINES; i++)
    {
        bLines_h[i].CP[0] = last;
        for (int j = 1; j < 3; j++)
        {
            bLines_h[i].CP[j].x = (float)rand()/(float)RAND_MAX;
            bLines_h[i].CP[j].y = (float)rand()/(float)RAND_MAX;
        }
        last = bLines_h[i].CP[2];
    }
    return bLines_h;
}

void FreeBezierLinesInitJAMC(void *handle) {
    delete[] reinterpret_cast<CPx3*>(handle);
}
