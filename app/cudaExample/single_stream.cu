//          Copyright Oliver Kowalke 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <memory>
#include <random>
#include <tuple>

#include <cuda.h>

#include <boost/assert.hpp>
#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>

#include <boost/fiber/all.hpp>
#include <boost/fiber/cuda/waitfor.hpp>

#include "deps/Kernel.h"

constexpr int kNumTrails = 256;
constexpr int wh = 256;
constexpr int nt = 8;

static int fiber_count = kNumTrails;
static std::mutex mtx_count{};
static boost::fibers::condition_variable_any cnd_count{};
typedef std::unique_lock< std::mutex > lock_type;

int main(int argc, char *argv[]) {
    try {
        bool done = false;
        std::vector<std::thread> tx;
        int nthreads = 1;
        InitDummy();
        if (argc > 1) nthreads = std::atoi(argv[1]);
        for (int i = 0; i < nthreads - 1; i++) tx.emplace_back([nthreads] {
            boost::fibers::use_scheduling_algorithm< boost::fibers::algo::work_stealing >(nthreads);
            lock_type lk(mtx_count);
            cnd_count.wait( lk, [](){ return 0 >= fiber_count; } ); 
        });
        boost::fibers::use_scheduling_algorithm< boost::fibers::algo::work_stealing >(nthreads); 
        boost::fibers::fiber fg([&] {
            std::vector<boost::fibers::fiber> vf;
            auto startCuda = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < kNumTrails; i++) {
                vf.emplace_back([&done, i, nthreads]{
		    try {
		        cudaStream_t stream;
		        int size = wh * wh;
		        int full_size = nt * size;
		        int * host_a, * host_b, * host_c;
		        int * dev_a, * dev_b, * dev_c;
		        cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
                        cudaHostAlloc(&host_a, wh * wh * nt * sizeof(int), cudaHostAllocDefault);
                        cudaHostAlloc(&host_b, wh * wh * nt * sizeof(int), cudaHostAllocDefault);
                        cudaHostAlloc(&host_c, wh * wh * nt * sizeof(int), cudaHostAllocDefault);
		        cudaMalloc( & dev_a, size * sizeof( int) );
		        cudaMalloc( & dev_b, size * sizeof( int) );
		        cudaMalloc( & dev_c, size * sizeof( int) );
                        auto res = GetRandomArray(host_a, host_b, size, full_size);
		        for ( int i = 0; i < full_size; i += size) {
		            cudaMemcpyAsync( dev_a, host_a + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
		            cudaMemcpyAsync( dev_b, host_b + i, size * sizeof( int), cudaMemcpyHostToDevice, stream);
		            CircularSubarrayInnerProduct<<< size / 256, 256, 0, stream >>>( dev_a, dev_b, dev_c, size);
		            cudaMemcpyAsync( host_c + i, dev_c, size * sizeof( int), cudaMemcpyDeviceToHost, stream);
		        }
		        auto result = boost::fibers::cuda::waitfor_all( stream);
		        BOOST_ASSERT( stream == std::get< 0 >( result) );
		        BOOST_ASSERT( cudaSuccess == std::get< 1 >( result) );
		        for (int cpr = 0; cpr < full_size; cpr++) if (res[cpr] != host_c[cpr]) perror("error value\n");
		        cudaFree(dev_a);
		        cudaFree(dev_b);
		        cudaFree(dev_c);
		        cudaFreeHost(host_a);
                        cudaFreeHost(host_b);
                        cudaFreeHost(host_c);
                        cudaStreamDestroy(stream);
		        done = true;
		        lock_type lk( mtx_count);
		        if (--fiber_count <= 0) cnd_count.notify_all();
		    } catch ( std::exception const& ex) {
		        std::cerr << "exception: " << ex.what() << std::endl;
		    }
		});
            }
            for (auto& f: vf) f.join();
            auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startCuda).count();
            std::cout << "CPU time: " << dur << " us" << std::endl;
            lock_type lk( mtx_count);
            if (--fiber_count <= 0) cnd_count.notify_all();
        });
        fg.join();
        lock_type lk(mtx_count);
        cnd_count.wait( lk, [](){ return 0 >= fiber_count; } ); 
        for (auto& t: tx) t.join();
        std::cout << "done." << std::endl;
        return EXIT_SUCCESS;
    } catch ( std::exception const& e) {
        std::cerr << "exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "unhandled exception" << std::endl;
    }
	return EXIT_FAILURE;
}
