#include <core/scheduler/task.h>
#include <sys/resource.h>
#include <xtask/shared-stack-task.h>

#include <catch2/catch.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <vector>

using namespace std;

deque<CTask*> shared_task_queue;

vector<CTask*> to_free;
CSharedStack* xstack_app;
CScheduler xsched;
int coro_count = 0, idbg;

int naive_fact(int x) { return (x > 1) ? (naive_fact(x - 1) * x) : (1); }

void share_fact_wrapper(CTask* self, void* args) {
    naive_fact(rand() % 10);
    FinishTask(self, 0);
}

CTask* xstask_app_sched(CScheduler* self) {
    coro_count += 1;
#if defined(__APPLE__) || defined(JAMSCRIPT_ENABLE_VALGRIND)
    if (coro_count == 50)
        return NULL;
#endif
    CTask* t = CreateSharedStackTask(&xsched, share_fact_wrapper, NULL, xstack_app);
    to_free.push_back(t);
    return t;
}

void common_xtask_idle(CScheduler* self) { ShutdownScheduler(&xsched); }

#if defined(__linux__) && !defined(JAMSCRIPT_ENABLE_VALGRIND)

TEST_CASE("Performance XTask", "[xtask]") {
    WARN(sizeof(CTask));
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
    for (; iallocmax < 1024 * 1024 * 128; iallocmax = iallocmax + 1024 * 1024) {
        try {
            void* p = malloc(iallocmax);
            if (p != NULL)
                memset(p, 1, 102);
            free(p);
            if (p == NULL) {
                break;
            }
        } catch (int e) {
            break;
        }
    }
    WARN("largest could allocate is " << iallocmax / 1024 / 1024 << "mb");
    xstack_app = CreateSharedStack(1024 * 32, malloc, free, memcpy);
    CreateScheduler(&xsched, xstask_app_sched, common_xtask_idle, EmptyFuncBeforeAfter,
                    EmptyFuncBeforeAfter);
    SchedulerMainloop(&xsched);
    for (int i = 0; i < to_free.size() - 1; i++) {
        idbg = i;
        if (to_free[i] != NULL)
            DestroySharedStackTask(to_free[i]);
    }
    DestroySharedStack(xstack_app);
    WARN("coroutine per GB is " << coro_count * (1024 / (iallocmax / 1024 / 1024)));
    REQUIRE(coro_count > 0);
    if (setrlimit(RLIMIT_AS, &prev)) {
        REQUIRE(false);
    }
}
#else
TEST_CASE("Performance XTask", "[xtask]") {
    xstack_app = CreateSharedStack(1024 * 32, malloc, free, memcpy);
    CreateScheduler(&xsched, xstask_app_sched, common_xtask_idle, EmptyFuncBeforeAfter,
                    EmptyFuncBeforeAfter);
    SchedulerMainloop(&xsched);
    for (auto& t : to_free)
        if (t != NULL)
            DestroySharedStackTask(t);
    DestroySharedStack(xstack_app);
    REQUIRE(coro_count > 30);
}
#endif