#include <catch2/catch.hpp>
#include <core/task/task.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/resource.h>
#include <vector>
#include <queue>
#include <iostream>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive_ptr.hpp>
#include <scheduler/scheduler.h>
using namespace std;
int coro_count = 0, idbg;

int naive_fact(int x) {
    return (x > 1) ? (naive_fact(x - 1) * x) : (1);
}

class BenchSchedXS : public JAMScript::SchedulerBase {
public:
    std::vector<JAMScript::TaskInterface*> freeList;
   JAMScript::TaskInterface* NextTask() {
        try {
            auto* newTask = (new JAMScript::SharedCopyStackTask(
                    reinterpret_cast<JAMScript::SchedulerBase*>(this), naive_fact, rand() % 1000));
            freeList.push_back(newTask);
            coro_count++;
            return newTask;
        } catch (...) {
            ShutDown();
            return nullptr;
        }
    }
    void Run() {
        while (toContinue) {
            try {
                auto nextTask = NextTask();
#ifdef JAMSCRIPT_ENABLE_VALGRIND
                if (nextTask != nullptr) {
                    nextTask->SwapIn();
                }
                if (coro_count >= 300) ShutDown();
#else
                if (nextTask != nullptr) {
                    nextTask->SwapIn();
                } else {
                    ShutDown();
                }
#endif
            } catch (...) {
                ShutDown();
            }
        }
    }
    // JAMScript::JAMStorageTypes::BatchQueueType freeList;
    BenchSchedXS(uint32_t stackSize) : JAMScript::SchedulerBase(stackSize) {}
    ~BenchSchedXS() {
        std::for_each(freeList.begin(), freeList.end(), [] (JAMScript::TaskInterface* x) { delete x; });
    }
    JAMScript::TaskInterface* onlyTask = nullptr;
};

#if defined(__linux__) && !defined(JAMSCRIPT_ENABLE_VALGRIND)

TEST_CASE("Performance XTask", "[xtask]") {
    struct rlimit hlmt;
    if (getrlimit(RLIMIT_AS, &hlmt)) {
        REQUIRE(false);
    }
    struct rlimit prev = hlmt;
    hlmt.rlim_cur = 1024 * 1024 * 256;
    hlmt.rlim_cur = 1024 * 1024 * 256;
    if (setrlimit(RLIMIT_AS, &hlmt)) {
        REQUIRE(false);
    }
    unsigned int iallocmax = 1024 * 1024;
    for (; iallocmax < 1024 * 1024 * 256; 
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
    BenchSchedXS bSched2(1024 * 128);
    try {
        bSched2.Run();
        std::cout << "coroutine per GB is " << coro_count * 
         (1024 / (iallocmax / 1024 / 1024)) << std::endl;
    } catch (const std::exception& e) {

    }
    
    
    REQUIRE(coro_count > 0);
    if (setrlimit(RLIMIT_AS, &prev)) {
        REQUIRE(false);
    }
}
#else
TEST_CASE("Performance XTask", "[xtask]") {
    {
        BenchSchedXS bSched2(1024 * 128);
    bSched2();
    REQUIRE(coro_count > 30);
    }
    
}
#endif