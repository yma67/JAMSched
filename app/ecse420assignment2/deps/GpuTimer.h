#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
struct GpuTimer
{
    cudaEvent_t start;
    cudaEvent_t stop;

    GpuTimer()
    {
        cudaEventCreate(&start);
        cudaEventCreate(&stop);
    }

    ~GpuTimer()
    {
        cudaEventDestroy(start);
        cudaEventDestroy(stop);
    }
    void Start();
    void Stop();
    float Elapsed();
};

inline void GpuTimer::Start()
{
    cudaEventRecord(start, 0);
}

inline void GpuTimer::Stop()
{
    cudaEventRecord(stop, 0);
}

inline float GpuTimer::Elapsed()
{
    float elapsed;
    cudaEventSynchronize(stop);
    cudaEventElapsedTime(&elapsed, start, stop);
    return elapsed;
}
