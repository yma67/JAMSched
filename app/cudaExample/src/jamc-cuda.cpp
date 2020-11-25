//
// Created by mayuxiang on 2020-11-11.
//
#include "jamc-cuda.h"
#include "io/cuda-wrapper.h"
#include <random>
#include <nvToolsExt.h>
#include <nvToolsExtCuda.h>
cudaError_t WaitForCudaStream(cudaStream_t s)
{
    return jamc::cuda::WaitStream(s);
}

constexpr int kInnerProductSize = 128;

std::vector<int> GetRandomArray(int * ha, int * hb, int sz, int fsz)
{
    assert(kInnerProductSize > sz);
    nvtxRangeId_t id2 = nvtxRangeStart("GetRandomArray");
    std::vector<int> res(fsz, 0);
    std::minstd_rand generator;
    std::uniform_int_distribution<> distribution(1, 6);
    for ( int i = 0; i < fsz; i += sz) {
        int cumu = 0;
        for (int j = 0; j < sz; j++) {
            ha[i + j] = distribution(generator);
            hb[i + j] = distribution(generator);
        }
        for (int j = 0; j < kInnerProductSize; j++) {
            cumu += ha[i + j] * hb[i + j];
        }
        for (int j = 0; j < sz; j++) {
            res[i + j] = cumu;
            cumu -= ha[i + j] * hb[i + j];
            cumu += ha[i + ((j + kInnerProductSize) % sz)] * hb[i + ((j + kInnerProductSize) % sz)];
        }
    }
    nvtxRangeEnd(id2);
    return res;
}
