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
    int lim = idx + 256;
    if (lim > size) lim = size;
    for (int i = idx; i < lim; i++) c[idx] += a[i % size] * b[i % size];
    if (idx < size) {
        c[idx] = a[idx] + b[idx];
    }
}

static std::vector<int> GetDistribution(int * ha, int * hb, int sz)
{
    nvtxRangeId_t id2 = nvtxRangeStart("GetDistribution");
    std::vector<int> res;
    std::minstd_rand generator;
    std::uniform_int_distribution<> distribution(1, 6);
    for ( int i = 0; i < sz; ++i) {
        ha[i] = distribution(generator);
        hb[i] = distribution(generator);
        res.push_back(ha[i] + hb[i]);
    }
    nvtxRangeEnd(id2);
    return res;
}

void KernelInvoker(cudaStream_t stream, int* host_a, int* host_b, int* host_c, int* dev_a, int* dev_b, int* dev_c, int size, int numIteration)
{
        int full_size = numIteration * size;
        auto result = GetDistribution(host_a, host_b, full_size);
        auto args = std::make_unique<CommandArgs<int*, int*, int*, int>>(dev_a, dev_b, dev_c, size);
        for ( int i = 0; i < full_size; i += size) {
            cudaMemcpyAsync( dev_a, host_a + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync( dev_b, host_b + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
            cudaLaunchKernel((void*)vector_add, dim3(size / 256), dim3(256), args->GetCudaKernelArgs(), 0, stream);
            cudaMemcpyAsync( host_c + i, dev_c, size * sizeof( int), cudaMemcpyDeviceToHost, stream);
        }
        WaitForCudaStream(stream);
        for (int i = 0; i < full_size; i++) assert(result[i] == host_c[i]);
}
