#include <chrono>
#include <iostream>
#include <jamscript>
#include "deps/Kernel.h"
#include "deps/cnmem.h"
#include "bzrline_cdp.cuh"
#include <boost/lockfree/stack.hpp>

boost::lockfree::stack<cudaStream_t> cudaStreams;
std::atomic<int> hitCountStream{0};

void jamcStreamInit(size_t n) {
    for (int i = 0; i < n; i++) {
        cudaStream_t s;
        cudaStreamCreateWithFlags(&s, cudaStreamNonBlocking);
        cnmemRegisterStreamGivenDevice(s, 0);
        cudaStreams.push(s);
    }
}

cudaStream_t jamcStreamAlloc() {
    cudaStream_t s;
    if (!cudaStreams.pop(s)) {
        // if (cudaSuccess != cudaStreamCreateWithFlags(&s, cudaStreamNonBlocking)) {
        if (cudaSuccess != cudaStreamCreate(&s)) {
            printf("bad allocation stream\n");
            exit(0);
        } else {
            cnmemRegisterStreamGivenDevice(s, 0);
        }
    } else {
        hitCountStream++;
    }
    return s;
}

void jamcStreamFree(cudaStream_t s) {
    cudaStreams.push(s);
}

void jamcStreamClear() {
    cudaStreams.consume_all([] (const cudaStream_t& h) { cudaStreamDestroy(h); });
}

int main(int argc, char **argv) {
    InitDummy();
    using namespace std::literals::chrono_literals;
    jamc::Timer::SetGPUSampleRate(0ns);
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    int numOfWorkers = std::atoi(argv[1]);
    for (int i = 0; i < numOfWorkers; i++) {
        vst.push_back(
            std::move(
                std::make_unique<jamc::StealScheduler>(
                    &ribScheduler, 1024 * 256
                )
            )
        );
    }
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(
        jamc::StackTraits(false, 1024 * 256, true, false), 
        jamc::Duration::max(), [&ribScheduler] {
        auto start = std::chrono::high_resolution_clock::now();
        cnmemDevice_t memDevice, memHost;
        memDevice.device = 0;
        memDevice.size = N_LINES * MAX_TESSELLATION * 4;
        memDevice.numStreams = 0;
        memDevice.streams = nullptr;
        memDevice.streamSizes = nullptr;
        cnmemInit(1, &memDevice, CNMEM_FLAGS_DEFAULT);
        auto wg = std::make_unique<jamc::WaitGroup>();
        std::vector<jamc::TaskHandle> pendings;
        auto bLines_h = GetBezierLinesInitJAMC();
        for (int k = 0; k < N_LINES; k++)  {
            wg->Add();
            ribScheduler.CreateBatchTask(
                jamc::StackTraits(true, 0, true), 
                jamc::Duration::max(), 
                [k, bLines_h, &wg] { 
                    auto s = jamcStreamAlloc();
                    ComputeBezierLinesJAMC(k, bLines_h, s);
                    jamcStreamFree(s);
                    wg->Done();
                }
            );
        }
        wg->Wait();
        jamcStreamClear();
        cnmemFinalize();
        FreeBezierLinesInitJAMC(bLines_h);
        std::cout << "Elapsed - " 
                  << std::chrono::duration_cast<std::chrono::nanoseconds>
                  (std::chrono::high_resolution_clock::now() - start).count() 
                  << " ns, stream cache hit = " 
                  << float(hitCountStream) / float(N_LINES) 
                  << "%" << std::endl;
        ribScheduler.ShutDown();
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
