//          Copyright Oliver Kowalke 2015.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// based on https://github.com/atemerev/skynet from Alexander Temerev 

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

#include <boost/fiber/all.hpp>
#include <boost/predef.h>

#include "barrier.hpp"

using clock_type = std::chrono::high_resolution_clock;
using duration_type = clock_type::duration;
using time_point_type = clock_type::time_point;
using channel_type = boost::fibers::buffered_channel< std::uint64_t >;
using allocator_type = boost::fibers::fixedsize_stack;
using lock_type = std::unique_lock< std::mutex >;

static bool done = false;
static std::mutex mtx{};
static boost::fibers::condition_variable_any cnd{};

// microbenchmark
void skynet( allocator_type & salloc, channel_type & c, std::size_t num, std::size_t size, std::size_t div) {
    if ( 1 == size) {
        c.push( num);
    } else {
        channel_type rc{ 16 };
        for ( std::size_t i = 0; i < div; ++i) {
            auto sub_num = num + i * size / div;
            boost::fibers::fiber{ boost::fibers::launch::dispatch,
                              std::allocator_arg, salloc,
                              skynet,
                              std::ref( salloc), std::ref( rc), sub_num, size / div, div }.detach();
        }
        std::uint64_t sum{ 0 };
        for ( std::size_t i = 0; i < div; ++i) {
            sum += rc.value_pop();
        }
        c.push( sum);
    }
}

void thread( std::uint32_t thread_count) {
    // thread registers itself at work-stealing scheduler
    boost::fibers::use_scheduling_algorithm< boost::fibers::algo::work_stealing >( thread_count);
    lock_type lk( mtx);
    cnd.wait( lk, [](){ return done; });
    BOOST_ASSERT( done);
}

int main(int argc, char *argv[]) {
    try {
        // count of logical cpus
        std::uint32_t thread_count = std::atoi(argv[1]);
        std::size_t size{ 1000000 };
        std::size_t div{ 10 };
        allocator_type salloc{ 2*allocator_type::traits_type::page_size() };
        std::uint64_t result{ 0 };
        channel_type rc{ 2 };
        std::vector< std::thread > threads;
        for ( std::uint32_t i = 1 /* count main-thread */; i < thread_count; ++i) {
            // spawn thread
            threads.emplace_back( thread, thread_count);
        }
        // main-thread registers itself at work-stealing scheduler
        boost::fibers::use_scheduling_algorithm< boost::fibers::algo::work_stealing >( thread_count);
        time_point_type start{ clock_type::now() };
        skynet( salloc, rc, 0, size, div);
        result = rc.value_pop();
        if ( 499999500000 != result) {
            throw std::runtime_error("invalid result");
        }
        auto duration = clock_type::now() - start;
        lock_type lk( mtx);
        done = true;
        lk.unlock();
        cnd.notify_all();
        for ( std::thread & t : threads) {
            t.join();
        }
        std::cout << "duration: " << duration.count() / 1000000 << " ms" << std::endl;
        return EXIT_SUCCESS;
    } catch ( std::exception const& e) {
        std::cerr << "exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "unhandled exception" << std::endl;
    }
	return EXIT_FAILURE;
}
/*
#include <jamscript.hpp>
#include <queue>

template <typename T, std::size_t maxSize>
class SingleConsumerQueue
{
    JAMScript::ConditionVariable cv;
    JAMScript::Mutex m;
    std::array<T, maxSize> vStore;
    std::size_t curr = 0;
public:
    void Push(T t) 
    {
        std::unique_lock sl(m);
        vStore[curr++] = t;
        if (curr == maxSize) cv.notify_one();
    }
    std::array<T, maxSize>& PopAll()
    {
        std::unique_lock sl(m);
        while (curr < maxSize) cv.wait(sl);
        return vStore;
    }
};
using clock_type = std::chrono::steady_clock;
using duration_type = clock_type::duration;
using time_point_type = clock_type::time_point;
JAMScript::StackTraits stCommonNode(false, 4096 * 16, true), stCommonLeaf(true, 0, true);
JAMScript::RIBScheduler *rib;
std::atomic<long> totalCreateTime = 0;
template <std::size_t nSk>
void skynet(SingleConsumerQueue<long, nSk>& cNum, long num, long size, long div)
{
    if (size == 1)
    {
        cNum.Push(num);
        return;
    }
    else
    {
        SingleConsumerQueue<long, 10> sc;
        for (long i = 0; i < div; i++)
        {
            long factor = size / div;
            long subNum = num + i * (factor);
                        //time_point_type start{ clock_type::now() };

            if (factor == 1) 
            {
                rib->CreateBatchTask(
                    stCommonLeaf, JAMScript::Duration::max(), 
                    skynet<10>, std::ref(sc), long(subNum), long(factor), long(div));
            }
            else 
            {
                rib->CreateBatchTask(
                    stCommonNode, JAMScript::Duration::max(), 
                    skynet<10>, std::ref(sc), long(subNum), long(factor), long(div));
            }
                        //auto duration = clock_type::now() - start;
            //std::cout << "duration: " << duration.count() << " ms" << std::endl;
            JAMScript::ThisTask::Yield();
        }
        long sum = 0;
        auto& v = sc.PopAll();
        for (long i = 0; i < div; i++)
        {
            sum += v[i];
        }
        cNum.Push(sum);
    }
}

int main(int argc, char *argv[])
{
    long totalNS = 0;
    for (int i = 0; i < 10; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 128);
        rib = &ribScheduler;
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst { };
        for (int i = 0; i < atoi(argv[1]); i++) vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 128)));
        ribScheduler.SetStealers(std::move(vst));
        ribScheduler.CreateBatchTask(
            stCommonNode, JAMScript::Duration::max(), [&ribScheduler, &totalNS] {
            auto tpStart = std::chrono::high_resolution_clock::now();
            SingleConsumerQueue<long, 1> sc;
            skynet<1>(sc, 0, 1000000, 10);
            auto res = sc.PopAll()[0];
            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - tpStart).count();
            totalNS += elapsed;
            std::cout << "result = " << res << " elapsed = " << elapsed / 1000000 << " ms per_fiber = " << elapsed / 1111111 << " ns/fiber" << std::endl;
            ribScheduler.ShutDown();
        });
        ribScheduler.RunSchedulerMainLoop();
    }
    printf("avg over 10 = %ld ms\n", totalNS / 10000000);
    return 0;
}

auto* ptrTaskCurrent = TaskInterface::Active();
                if (ptrTaskCurrent != nullptr && this != ptrTaskCurrent->scheduler && pNextThief->Size() > 0)
                {
                    static_cast<StealScheduler *>(ptrTaskCurrent->scheduler)->Steal(fn);
                }
                else if (pNextThief != nullptr && pNextThief->Size() == 0)
                {
                    pNextThief->Steal(fn);
                }
                else
                {
                    thiefs[rand() % thiefs.size()]->Steal(fn);
                }

*/