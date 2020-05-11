#include <catch2/catch.hpp>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/resource.h>
#include <vector>
#include <queue>

using namespace std;

deque<task_t*> shared_task_queue;

vector<task_t*> to_free;
shared_stack_t* xstack_app;
scheduler_t xsched;
int coro_count = 0, idbg;

int naive_fact(int x) {
    return (x > 1) ? (naive_fact(x - 1) * x) : (1);
}

void share_fact_wrapper(task_t* self, void* args) {
    naive_fact(rand() % 10);
    finish_task(self, 0);
}

task_t* xstask_app_sched(scheduler_t* self) {
    coro_count += 1;
#if defined(__APPLE__) || defined(JAMSCRIPT_ENABLE_VALGRIND)
    if (coro_count == 50) return NULL;
#endif
    task_t* t = make_shared_stack_task(&xsched, share_fact_wrapper, NULL,
                                       xstack_app);
    to_free.push_back(t);
    return t;
}

void common_xtask_idle(scheduler_t* self) {
    shutdown_scheduler(&xsched);
}

#if defined(__linux__) && !defined(JAMSCRIPT_ENABLE_VALGRIND)

TEST_CASE("Performance XTask", "[xtask]") {
    WARN(sizeof(jam_ucontext_t));
    struct rlimit hlmt;
    if (getrlimit(RLIMIT_AS, &hlmt)) {
        REQUIRE(false);
    }
    struct rlimit prev = hlmt;
    hlmt.rlim_cur = 1024 * 1024 * 128;
    hlmt.rlim_cur = 1024 * 1024 * 128;
    if (setrlimit(RLIMIT_AS, &hlmt)) {
        REQUIRE(false);
    }
    unsigned int iallocmax = 1024 * 1024;
    for (; iallocmax < 1024 * 1024 * 128; 
           iallocmax = iallocmax + 1024 * 1024) {
        try {
            void* p = malloc(iallocmax);
            if (p!=NULL)memset(p, 1, 102);
            free(p);
            if (p == NULL) {
                break;
            }
        } catch (int e) {
            break;
        }
    }
    WARN("largest could allocate is " << iallocmax / 1024 / 1024 << "mb");
    xstack_app = make_shared_stack(1024 * 32, malloc, free, memcpy);
    make_scheduler(&xsched, xstask_app_sched, common_xtask_idle, 
                   empty_func_before_after, empty_func_before_after);
    scheduler_mainloop(&xsched);
    for (int i = 0; i < to_free.size() - 1; i++) {
        idbg = i;
        if (to_free[i] != NULL) 
            destroy_shared_stack_task(to_free[i]);
    }
    destroy_shared_stack(xstack_app);
    WARN("coroutine per GB is " << coro_count * 
         (1024 / (iallocmax / 1024 / 1024)));
    REQUIRE(coro_count > 0);
    if (setrlimit(RLIMIT_AS, &prev)) {
        REQUIRE(false);
    }
}
#else
TEST_CASE("Performance XTask", "[xtask]") {
    xstack_app = make_shared_stack(1024 * 32, malloc, free, memcpy);
    make_scheduler(&xsched, xstask_app_sched, common_xtask_idle, 
                   empty_func_before_after, empty_func_before_after);
    scheduler_mainloop(&xsched);
    for (auto& t: to_free) if (t != NULL) destroy_shared_stack_task(t);
    destroy_shared_stack(xstack_app);
    REQUIRE(coro_count > 30);
}
#endif