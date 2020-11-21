//
// Created by mayuxiang on 2020-11-11.
//
#include "../deps/Kernel.h"
#include "../deps/LogicalInput.h"
#include "../deps/cnmem.h"
#include <vector>
#include "jamc-cuda.h"
#include "cuda_runtime.h"
#include <chrono>
#include <tuple>
#include <array>
#include <algorithm>
#include <random>
#include <memory>
#include <cassert>
#include <functional>
#include <nvToolsExt.h>
#include <nvToolsExtCuda.h>


template <typename... Args>
class CommandArgs {
    using ArgTupleType = std::tuple<Args...>;
    std::array<void*, std::tuple_size<ArgTupleType>::value> arrayArgs;
    ArgTupleType actArgs;
public:
    CommandArgs() = default;
    CommandArgs(Args... args) : actArgs(std::forward<Args>(args)...) {
        std::apply([this](auto& ...xs) { arrayArgs = {(&xs)...}; }, actArgs);
    }
    void** GetCudaKernelArgs() { return arrayArgs.data(); }
};

__global__
void vector_add( int * a, int * b, int * c, int size) {
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    //int lim = idx + 4;
    //if (lim > size) lim = size;
    //for (int i = 0; i < lim; i++) c[idx] += a[i] * b[i];
    if (idx < size) {
        c[idx] = a[idx] + b[idx];
    }
}

__global__ void ComputeKernel(g26::LogicalInput* c, std::size_t sz)
{
    constexpr unsigned int kThreadsPerBlock = 1024;
    auto i = threadIdx.x + blockIdx.x * kThreadsPerBlock;
    for (int j = 0; j < 15; j++) if (i < sz) c[i].Calculate();
}


[[clang::optnone]] void GetDistribution(int * ha, int * hb, int sz)
{
//nvtxRangeId_t id2 = nvtxRangeStart("GetDistribution");
    std::minstd_rand generator;
        std::uniform_int_distribution<> distribution(1, 6);
        for ( int i = 0; i < sz; ++i) {
            ha[i] = distribution(generator);
            hb[i] = distribution(generator);
        }
        //nvtxRangeEnd(id2);
}

void KernelInvoker(cudaStream_t stream, int* host_a, int* host_b, int* host_c)
{
        int size = 256 * 256;
        int numIteration = 8;
        int full_size = numIteration * size;
        int * dev_a, * dev_b, * dev_c;
        //nvtxRangeId_t id0 = nvtxRangeStart("cnmemMalloc dev_a");
        cnmemMalloc((void**)(&dev_a), size * sizeof( int), stream );
        //nvtxRangeEnd(id0);
        //nvtxRangeId_t id1 = nvtxRangeStart("cnmemMalloc dev_b");
        cnmemMalloc((void**)(&dev_b), size * sizeof( int), stream );
        //nvtxRangeEnd(id1);
        //nvtxRangeId_t id2 = nvtxRangeStart("cnmemMalloc dev_c");
        cnmemMalloc((void**)(&dev_c), size * sizeof( int), stream );
        //nvtxRangeEnd(id2);
        GetDistribution(host_a, host_b, full_size);
        auto args = std::make_unique<CommandArgs<int*, int*, int*, int>>(dev_a, dev_b, dev_c, size);
        for ( int i = 0; i < full_size; i += size) {
            cudaMemcpyAsync( dev_a, host_a + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync( dev_b, host_b + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
            cudaLaunchKernel((void*)vector_add, dim3(size / 256), dim3(256), args->GetCudaKernelArgs(), 0, stream);
            cudaMemcpyAsync( host_c + i, dev_c, size * sizeof( int), cudaMemcpyDeviceToHost, stream);
        }
        WaitForCudaStream(stream);
        //nvtxRangeId_t idf0 = nvtxRangeStart("cnmemFree dev_a");
        cnmemFree(dev_a, stream);
        //nvtxRangeEnd(idf0);
        //nvtxRangeId_t idf1 = nvtxRangeStart("cnmemFree dev_b");
        cnmemFree( dev_b, stream);
        //nvtxRangeEnd(idf1);
        //nvtxRangeId_t idf2 = nvtxRangeStart("cnmemFree dev_c");
        cnmemFree( dev_c, stream);
        //nvtxRangeEnd(idf2);
}

void Assignment1(cudaStream_t s1, g26::LogicalInput* inputs, g26::LogicalInput* gpuInputs, g26::LogicalInput* outputs,g26::LogicalInput* answer, unsigned int lenInput)
{
    constexpr unsigned int kThreadsPerBlock = 1024;
    cudaMemcpyAsync(gpuInputs, inputs,
        lenInput * sizeof(g26::LogicalInput),
        cudaMemcpyHostToDevice, s1);
    auto args = std::make_unique<CommandArgs<g26::LogicalInput*, std::size_t>>(gpuInputs, lenInput);
    cudaLaunchKernel((void*)ComputeKernel, 
                     dim3(lenInput / kThreadsPerBlock + 1), 
                     dim3(std::min(lenInput, kThreadsPerBlock)), 
                     args->GetCudaKernelArgs(), 0, s1);
    cudaMemcpyAsync(outputs, gpuInputs,
        lenInput * sizeof(g26::LogicalInput),
        cudaMemcpyDeviceToHost, s1);
    assert(WaitForCudaStream(s1) == cudaSuccess);
    for (int i = 0; i < lenInput; i++) {
        assert(outputs[i].GetResult() == answer[i].GetResult());
    }
}
