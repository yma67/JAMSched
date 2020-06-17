#include <core/coroutine/task.h>

#include <catch2/catch.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <core/task/task.h>
#include <boost/intrusive_ptr.hpp>
#define task_niter 300

int ref[task_niter], calc[task_niter], sched_tick = 0;
CScheduler scheduler;
CTask* flames[task_niter];

CTask* schedule_next(CScheduler* self) { return flames[sched_tick++]; }

void IdleTask(CScheduler* self) {}

void BeforeEach(CTask* self) {}

void AfterEach(CTask* self) {
    calc[*(static_cast<int*>(self->taskArgs))] = self->returnValue;
    if (sched_tick >= task_niter) {
        sched_tick = 0;
        ShutdownScheduler(&scheduler);
    }
}

#define TEST_TASK_NAME "factorial"
int test_task(int n) {
    if (n < 2)
        return 1;
    return n * test_task(n - 1);
}

void test_task_core(CTask* self, void* args) {
    FinishTask(self, test_task(*(static_cast<int*>(args))));
}

TEST_CASE("Baseline", "[core]") {
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("Baseline " TEST_TASK_NAME) {
#endif
        for (int i = 0; i < task_niter; i++) {
            ref[i] = test_task(i);
        }
        return;
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    };
#endif
}

#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
TEST_CASE("C++ Thread", "[core]") {
    BENCHMARK("C++ Thread " TEST_TASK_NAME) {
        for (int i = 0; i < task_niter; i++) {
            std::thread([&] {
                test_task(i);
                return;
            }).join();
        }
        return;
    };
}
#endif

TEST_CASE("JAMCore", "[core]") {
    int xs[task_niter];
    for (int i = 0; i < task_niter; i++)
        flames[i] = reinterpret_cast<CTask*>(calloc(1, sizeof(CTask) + 256 * 1024));
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("JAMCore initialization ONLY") {
        CreateScheduler(&scheduler, schedule_next, IdleTask, BeforeEach, AfterEach);
        for (int i = 0; i < task_niter; i++) {
            xs[i] = i;
            CreateTask(flames[i], &scheduler, test_task_core, &xs[i], 256 * 1024,
                       reinterpret_cast<unsigned char*>(flames[i] + 1));
        }
        return;
    };
#endif
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("JAMCore init AND run schedule " TEST_TASK_NAME) {
#endif
        CreateScheduler(&scheduler, schedule_next, IdleTask, BeforeEach, AfterEach);
        for (int i = 0; i < task_niter; i++) {
            xs[i] = i;
            CreateTask(flames[i], &scheduler, test_task_core, &xs[i], 256 * 1024,
                       reinterpret_cast<unsigned char*>(flames[i] + 1));
        }
        SchedulerMainloop(&scheduler);
        return;
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    };
#endif
    for (int i = 0; i < 15; i++) REQUIRE(calc[i] == ref[i]);
    for (int i = 0; i < task_niter; i++) free(flames[i]);
}

class BenchSched : public JAMScript::SchedulerBase {
public:
    JAMScript::TaskInterface* NextTask() override {
        return onlyTask;
    }
    void operator()() {
        this->onlyTask->SwapIn();

    }
    BenchSched(uint32_t stackSize) : JAMScript::SchedulerBase(stackSize) {}
    JAMScript::TaskInterface* onlyTask = nullptr;
};

TEST_CASE("JAMScript++", "[core]") {
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("Baseline " TEST_TASK_NAME) {
#endif
        BenchSched bSched(1024 * 256);
        for (int i = 0; i < task_niter; i++) {
            int rex = 0;
            bSched.onlyTask = new JAMScript::StandAloneStackTask(&bSched, 1024 * 256, [&](int k) {
                if (k < 2)
                    return rex = 1;
                return rex = k * test_task(k - 1);
            }, i);
            bSched();
            ref[i] = rex;
        }
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
        return;
    };
#endif
    for (int i = 0; i < 15; i++) REQUIRE(calc[i] == ref[i]);
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("Init Only " TEST_TASK_NAME) {
#endif
        BenchSched bSched3(1024 * 256);
        for (int i = 0; i < task_niter; i++) {
            int rex = 0;
            bSched3.onlyTask = (new JAMScript::StandAloneStackTask(&bSched3, 1024 * 256, [&](int k) {
                if (k < 2)
                    return rex = 1;
                return rex = k * test_task(k - 1);
            }, i));
            bSched3();
            ref[i] = rex;
        }
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
        return;
    };
#endif
    
    for (int i = 0; i < 15; i++) REQUIRE(calc[i] == ref[i]);
}