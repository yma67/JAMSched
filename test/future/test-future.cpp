#include <core/scheduler/task.h>
#include <future/future.h>
#include <pthread.h>
#include <xtask/shared-stack-task.h>

#include <catch2/catch.hpp>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <thread>
#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/helgrind.h>
#include <valgrind/valgrind.h>
#endif
#define niter 10000
using namespace std;

CFuture future;
CScheduler scheduler_future;
CTask listener;
unsigned char listener_stack[1024 * 256];
// DOES NOT GUARANTEE ATOMICITY
CTask* placeholder = NULL;
std::chrono::time_point<std::chrono::high_resolution_clock> startt, endt;
unsigned long long int total_elapse = 0, remain = niter;
#if defined(__linux__)
pthread_barrier_t barrier;

CTask* schedule_next_future(CScheduler* self) {
    return __atomic_exchange_n(&placeholder, NULL, __ATOMIC_ACQ_REL);
}

void before_each_future(CTask* self) {
    if (remain < niter)
        REQUIRE((future.lockWord & 0x80000000) > 0);
}

void callback_postwait(CFuture* self) {
    __atomic_store(&placeholder, &(self->ownerTask), __ATOMIC_SEQ_CST);
}

void test_future_task(CTask* self, void* args) {
    while (remain) {
        CreateFuture(&future, self, &startt, callback_postwait);
        placeholder = &listener;
        pthread_barrier_wait(&barrier);
        WaitForValueFromFuture(&future);
        endt = std::chrono::high_resolution_clock::now();
        auto elps = std::chrono::duration_cast<std::chrono::nanoseconds>(endt - startt);
        REQUIRE(self->taskStatus == TASK_READY);
        REQUIRE((future.lockWord & 0x80000000) > 0);
        REQUIRE(elps.count() > 0);
        total_elapse += elps.count();
        pthread_barrier_wait(&barrier);
    }
    ShutdownScheduler(self->scheduler);
    FinishTask(self, 0);
}

void test_listener() {
    while (remain) {
        pthread_barrier_wait(&barrier);
        auto r = reinterpret_cast<std::chrono::time_point<std::chrono::high_resolution_clock>*>(
            future.data);
        this_thread::sleep_for(chrono::nanoseconds(rand() % 50));
        REQUIRE((future.lockWord & 0x80000000) == 0);
        *r = std::chrono::high_resolution_clock::now();
        NotifyFinishOfFuture(&future);
        remain = remain - 1;
        pthread_barrier_wait(&barrier);
    }
}

void test_future_idle(CScheduler* self) { this_thread::sleep_for(chrono::nanoseconds(1)); }
#ifndef JAMSCRIPT_ENABLE_VALGRIND
TEST_CASE("Performance CFuture", "[future]") {
    pthread_barrier_init(&barrier, NULL, 2);
    CreateScheduler(&scheduler_future, schedule_next_future, EmptyFuncNextIdle, before_each_future,
                    EmptyFuncBeforeAfter);
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    listener.v_stack_id =
        VALGRIND_STACK_REGISTER(listener_stack, (void*)((uintptr_t)listener_stack + 1024 * 256));
#endif
    CreateTask(&listener, &scheduler_future, test_future_task, NULL, 256 * 1024, listener_stack);
    thread lstn(test_listener);
    placeholder = &listener;
    SchedulerMainloop(&scheduler_future);
    lstn.join();
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(listener.v_stack_id);
#endif
    WARN("total elapse " << total_elapse << " ns, average elapse " << total_elapse / niter
                         << " ns");
}
#endif
#endif
/**
 * @test test chaining sleep/wakeup of 104 coroutines
 * @details 50% of them are regular tasks
 * @details 50% of them are xtasks
 */
deque<CTask*> sched_queue;
string secret(
    "Java is the best programming language in the world "
    "which has not yet been contaminated by the business\n");
string push_back_builder("");
int pushb_index = 0;

void renablr(CFuture* self) { sched_queue.push_back(self->ownerTask); }

void secret_completer(CTask* self, void* arg) {
    {
        char ch = *reinterpret_cast<char*>(arg);
        auto secret_getter = *reinterpret_cast<pair<CFuture*, CFuture*>*>(
            self->taskFunctionVector->GetUserData(self));
        push_back_builder = push_back_builder + ch;
        double x(0.25);
        x *= double(ch);
        string push_back_builder_snapshot = push_back_builder;
        WaitForValueFromFuture(secret_getter.second);
        REQUIRE(x == Approx(0.25 * double(ch)));
        REQUIRE((push_back_builder_snapshot +
                 *reinterpret_cast<string*>(secret_getter.second->data)) == secret);
        *reinterpret_cast<string*>(secret_getter.second->data) =
            ch + *reinterpret_cast<string*>(secret_getter.second->data);

        if (secret_getter.first != NULL) {
            secret_getter.first->data = secret_getter.second->data;
            NotifyFinishOfFuture(secret_getter.first);
        }
    }
    FinishTask(self, 0);
}

CTask* secret_scheduler_next(CScheduler* self) {
    if (sched_queue.size() > 0) {
        CTask* to_run = sched_queue.front();
        sched_queue.pop_front();
        return to_run;
    } else {
        ShutdownScheduler(&scheduler_future);
        return NULL;
    }
}

TEST_CASE("Interlock-10x", "[future]") {
    string completed_secret("");
    char chx[secret.size()];
    pair<CFuture*, CFuture*> px[secret.size()];
    CFuture* prev_future = NULL;
    CSharedStack* sstack = CreateSharedStack(1024 * 128, malloc, free, memcpy);
    memset(&scheduler_future, 0, sizeof(CScheduler));
    CreateScheduler(&scheduler_future, secret_scheduler_next, EmptyFuncNextIdle,
                    EmptyFuncBeforeAfter, EmptyFuncBeforeAfter);
    for (int i = 0; i < secret.size(); i++) {
        chx[i] = secret[i];
        CFuture* ptrf = reinterpret_cast<CFuture*>(calloc(sizeof(CFuture), 1));
        px[i] = make_pair(prev_future, ptrf);
        CTask* ntask;
        if (i % 2 == 0) {
            ntask = reinterpret_cast<CTask*>(calloc(sizeof(CTask), 1));
            unsigned char* nstack = reinterpret_cast<unsigned char*>(calloc(1024 * 64, 1));
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            ntask->v_stack_id =
                VALGRIND_STACK_REGISTER(nstack, (void*)((uintptr_t)nstack + 1024 * 64));
#endif
            CreateTask(ntask, &scheduler_future, secret_completer, &chx[i], 1024 * 64, nstack);
        } else {
            ntask = CreateSharedStackTask(&scheduler_future, secret_completer, &chx[i], sstack);
        }
        ntask->taskFunctionVector->SetUserData(ntask, &px[i]);
        CreateFuture(ptrf, ntask, NULL, renablr);
        prev_future = ptrf;
        sched_queue.push_back(ntask);
    }
    prev_future->data = &completed_secret;
    NotifyFinishOfFuture(prev_future);
    SchedulerMainloop(&scheduler_future);
    REQUIRE(secret == completed_secret);
    for (int i = 0; i < secret.size(); i++) {
        CTask* to_free = px[i].second->ownerTask;
        free(px[i].second);
        if (i % 2 == 0) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            VALGRIND_STACK_DEREGISTER(to_free->v_stack_id);
#endif
            free(to_free->stack);
            free(to_free);
        } else {
            DestroySharedStackTask(to_free);
        }
    }
    DestroySharedStack(sstack);
}
