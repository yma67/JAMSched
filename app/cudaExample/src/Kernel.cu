//
// Created by mayuxiang on 2020-11-11.
//
#include "../deps/Kernel.h"
#include "../deps/LogicalInput.h"
#include "../deps/cnmem.h"
#include <vector>
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

constexpr int kInnerProductSize = 128;

__global__
void CircularSubarrayInnerProduct( int * a, int * b, int * c, int size) {
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    c[idx] = 0;
    for (int i = idx; i < idx + kInnerProductSize; i++) {
        c[idx] += a[i % size] * b[i % size];
    }
}

void InitDummy()
{
    constexpr auto xlen = 50;
    cudaStream_t dummys[xlen];
    for (int i = 0; i < xlen; i++) cudaStreamCreate(&dummys[i]);
    for (int i = 0; i < xlen; i++) cudaStreamDestroy(dummys[i]);
    for (int i = 0; i < xlen; i++) cudaStreamCreateWithFlags(&dummys[i], cudaStreamNonBlocking);
    for (int i = 0; i < xlen; i++) cudaStreamDestroy(dummys[i]);
}

void KernelInvoker(cudaStream_t stream, int* host_a, int* host_b, int* host_c, int* dev_a, int* dev_b, int* dev_c, int size, int numIteration, const std::vector<int>& result)
{
#define useSingleThreadLaunch 0
    int full_size = numIteration * size;
    auto args = std::make_shared<CommandArgs<int*, int*, int*, int>>(dev_a, dev_b, dev_c, size);
#if useSingleThreadLaunch
    auto wg = CreateWaitGroup();
#endif
    for ( int i = 0; i < full_size; i += size) {
#if useSingleThreadLaunch
        AddWaitGroup(wg, 1);
        LaunchKernel([host_a, host_b, host_c, dev_a, dev_b, dev_c, i, size, stream, args, wg] {
#endif
            cudaMemcpyAsync( dev_a, host_a + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
            cudaMemcpyAsync( dev_b, host_b + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
            cudaLaunchKernel((void*)CircularSubarrayInnerProduct, dim3(size / 256), dim3(256), args->GetCudaKernelArgs(), 0, stream);
            cudaMemcpyAsync( host_c + i, dev_c, size * sizeof( int), cudaMemcpyDeviceToHost, stream);
#if useSingleThreadLaunch
            DoneWaitGroup(wg);});
#endif
    }
#if useSingleThreadLaunch
    WaitForWaitGroup(wg);
    DestroyWaitGroup(wg);
#endif
    // WaitForCudaStream(stream);
    // for (int i = 0; i < full_size; i++) if (result[i] != host_c[i]) perror("wrong result");
}
