#include <catch2/catch.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <jamscript>
#include <boost/intrusive_ptr.hpp>
#define task_niter 300

int ref[task_niter], calc[task_niter], sched_tick = 0;

#define TEST_TASK_NAME "factorial"
int test_task(int n)
{
    if (n < 2)
        return 1;
    return n * test_task(n - 1);
}

TEST_CASE("Baseline", "[core]")
{
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("Baseline " TEST_TASK_NAME)
    {
#endif
        for (int i = 0; i < task_niter; i++)
        {
            ref[i] = test_task(i);
        }
        return;
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    };
#endif
}

#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
TEST_CASE("C++ Thread", "[core]")
{
    BENCHMARK("C++ Thread " TEST_TASK_NAME)
    {
        for (int i = 0; i < task_niter; i++)
        {
            std::thread(test_task, i).join();
        }
        return;
    };
}
#endif

class BenchSched : public jamc::SchedulerBase
{
public:

    void Enable(jamc::TaskInterface *toEnable) override {}
    void EnableImmediately(jamc::TaskInterface *toEnable) override {}
    void RunSchedulerMainLoop() override
    {
        jamc::TaskInterface::ResetTaskInfos();
        onlyTask->SwapFrom(nullptr);
    }
    jamc::TaskInterface *GetNextTask() override
    {
        onlyTask->SwapTo(nullptr);
        return nullptr;
    }
    void EndTask(jamc::TaskInterface *tx) override {}
    BenchSched(uint32_t stackSize) : jamc::SchedulerBase(stackSize) {}
    ~BenchSched() { if (onlyTask != nullptr) delete onlyTask; }
    jamc::TaskInterface *onlyTask = nullptr;
};

TEST_CASE("jamc++", "[core]")
{
    for (int i = 0; i < task_niter; i++)
    {
        calc[i] = test_task(i);
    }
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("Baseline " TEST_TASK_NAME)
    {
#endif
        
        for (int i = 0; i < task_niter; i++)
        {
            int rex = 0;
            BenchSched bSched(1024 * 256);
            bSched.onlyTask = new jamc::StandAloneStackTask(
                &bSched, 1024 * 256, [i] {
                    if (i < 2)
                        return ref[i] = 1;
                    return ref[i] = i * test_task(i - 1);
                });
            bSched.RunSchedulerMainLoop();
        }
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
        return;
    };
#endif
    for (int i = 0; i < 15; i++)
        REQUIRE(calc[i] == ref[i]);
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("Init Only " TEST_TASK_NAME)
    {
#endif
        
        for (int i = 0; i < task_niter; i++)
        {
            int rex = 0;
            BenchSched bSched3(1024 * 256);
            bSched3.onlyTask = (new jamc::StandAloneStackTask(
                &bSched3, 1024 * 256, [&](int k) {
                    if (k < 2)
                        return rex = 1;
                    return rex = k * test_task(k - 1);
                },
                int(i)));
            bSched3.RunSchedulerMainLoop();
            ref[i] = rex;
        }
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
        return;
    };
#endif

    for (int i = 0; i < 15; i++)
        REQUIRE(calc[i] == ref[i]);
}