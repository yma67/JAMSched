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

#include <stdio.h>
#include <cuda_runtime_api.h>
#include "deps/helper_cuda.h"
#include <string.h>
#include <chrono>
#include <iostream>
#include "deps/Kernel.h"
#include "deps/bzrline_cdp.cuh"

__forceinline__ __device__ float2 operator+(float2 a, float2 b)
{
    float2 c;
    c.x = a.x + b.x;
    c.y = a.y + b.y;
    return c;
}

__forceinline__ __device__ float2 operator-(float2 a, float2 b)
{
    float2 c;
    c.x = a.x - b.x;
    c.y = a.y - b.y;
    return c;
}

__forceinline__ __device__ float2 operator*(float a, float2 b)
{
    float2 c;
    c.x = a * b.x;
    c.y = a * b.y;
    return c;
}

__forceinline__ __device__ float length(float2 a)
{
    return sqrtf(a.x*a.x + a.y*a.y);
}

struct BezierLine
{
    float2 CP[3];
    float2 *vertexPos;
    int nVertices;
};

__global__ void computeBezierLinePositions(int lidx, BezierLine *bLines, int nTessPoints)
{
    int idx = threadIdx.x + blockDim.x*blockIdx.x;

    if (idx < nTessPoints)
    {
        float u = (float)idx/(float)(nTessPoints-1);
        float omu = 1.0f - u;

        float B3u[3];

        B3u[0] = omu*omu;
        B3u[1] = 2.0f*u*omu;
        B3u[2] = u*u;

        float2 position = {0,0};

        for (int i = 0; i < 3; i++)
        {
            position = position + B3u[i] * bLines[lidx].CP[i];
        }

        bLines[lidx].vertexPos[idx] = position;
    }
}

__global__ void computeBezierLinesCDP(BezierLine *bLines, int nLines)
{
    int lidx = threadIdx.x + blockDim.x*blockIdx.x;

    if (lidx < nLines)
    {
        float curvature = length(bLines[lidx].CP[1] - 0.5f*(bLines[lidx].CP[0] + bLines[lidx].CP[2]))/length(bLines[lidx].CP[2] - bLines[lidx].CP[0]);
        int nTessPoints = min(max((int)(curvature*16.0f),4),MAX_TESSELLATION);
        if (bLines[lidx].vertexPos == NULL)
        {
            bLines[lidx].nVertices = nTessPoints;
            cudaMalloc((void **)&bLines[lidx].vertexPos, nTessPoints*sizeof(float2));
        }
        computeBezierLinePositions<<<ceil((float)bLines[lidx].nVertices/MAX_TESSELLATION), MAX_TESSELLATION>>>(lidx, bLines, bLines[lidx].nVertices);
    }
}

__global__ void freeVertexMem(BezierLine *bLines, int nLines)
{
    int lidx = threadIdx.x + blockDim.x*blockIdx.x;

    if (lidx < nLines)
        cudaFree(bLines[lidx].vertexPos);
}

int main(int argc, char **argv)
{
    InitDummy();
    float2 last = {0,0};
    auto start = std::chrono::high_resolution_clock::now();
    BezierLine *bLines_h = new BezierLine[N_LINES];
    for (int i = 0; i < N_LINES; i++)
    {
        bLines_h[i].CP[0] = last;

        for (int j = 1; j < 3; j++)
        {
            bLines_h[i].CP[j].x = (float)rand()/(float)RAND_MAX;
            bLines_h[i].CP[j].y = (float)rand()/(float)RAND_MAX;
        }

        last = bLines_h[i].CP[2];
        bLines_h[i].vertexPos = NULL;
        bLines_h[i].nVertices = 0;
    }


    BezierLine *bLines_d;
    checkCudaErrors(cudaMalloc((void **)&bLines_d, N_LINES*sizeof(BezierLine)));
    checkCudaErrors(cudaMemcpy(bLines_d, bLines_h, N_LINES*sizeof(BezierLine), cudaMemcpyHostToDevice));
    printf("Computing Bezier Lines (CUDA Dynamic Parallelism Version) ... ");
    computeBezierLinesCDP<<< max((unsigned int)ceil((float)N_LINES/(float)BLOCK_DIM), 1), BLOCK_DIM >>>(bLines_d, N_LINES);
    printf("Done!\n");
    //Do something to draw the lines here
    cudaDeviceSynchronize();
    freeVertexMem<<< (unsigned int)ceil((float)N_LINES/(float)BLOCK_DIM), BLOCK_DIM >>>(bLines_d, N_LINES);
    checkCudaErrors(cudaFree(bLines_d));
    delete[] bLines_h;
    cudaDeviceSynchronize();
    std::cout << "Elapsed - " << std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() << " ns" << std::endl;
    exit(EXIT_SUCCESS);
}
