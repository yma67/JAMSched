#include <catch2/catch.hpp>
#include <core/scheduler/task.h>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define task_niter 300

int ref[task_niter], calc[task_niter], sched_tick = 0;
scheduler_t scheduler;
task_t* flames[task_niter];


task_t* schedule_next() {
    return flames[sched_tick++];
}

void idle_task() {
    
}

void before_each(task_t* self) {
    
}

void after_each(task_t* self) {
    calc[*(static_cast<int*>(self->task_args))] = self->return_value;
    if (sched_tick >= task_niter) {
        sched_tick = 0;
        shutdown_scheduler(&scheduler);
    }
}

#define TEST_TASK_NAME "factorial"
int test_task(int n) {
    if (n < 2) return 1;
    return n * test_task(n - 1);
}

void test_task_core(task_t* self, void* args) {
    finish_task(self, test_task(*(static_cast<int*>(args))));
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
            std::thread([&] { test_task(i); return; }).join();
        }
        return;
    };
}
#endif


TEST_CASE("JAMCore", "[core]") {
    int xs[task_niter];
    for (int i = 0; i < task_niter; i++) flames[i] = reinterpret_cast<task_t*>(calloc(1, sizeof(task_t) + 256 * 1024));
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("JAMCore initialization ONLY") {
        make_scheduler(&scheduler, schedule_next, idle_task, before_each, after_each, memset);
        for (int i = 0; i < task_niter; i++) {
            xs[i] = i;
            make_task(flames[i], &scheduler, test_task_core, memset, &xs[i], NULL, 256 * 1024, reinterpret_cast<unsigned char*>(flames[i] + 1));
        }
        return;
    };
#endif
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    BENCHMARK("JAMCore init AND run schedule " TEST_TASK_NAME) {
#endif
        make_scheduler(&scheduler, schedule_next, idle_task, before_each, after_each, memset);
        for (int i = 0; i < task_niter; i++) {
            xs[i] = i;
            make_task(flames[i], &scheduler, test_task_core, memset, &xs[i], NULL, 256 * 1024, reinterpret_cast<unsigned char*>(flames[i] + 1));
        }
        scheduler_mainloop(&scheduler);
        return;
#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
    };
#endif
    for (int i = 0; i < 15; i++) REQUIRE(calc[i] == ref[i]); 
    for (int i = 0; i < task_niter; i++) free(flames[i]);
}