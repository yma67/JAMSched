#include <catch2/catch.hpp>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <future/future.h>
#include <pthread.h>
#include <chrono>
#include <thread>
#include <queue>
#include <cstring>
#include <cstdlib>
#include <iostream>
#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/helgrind.h>
#include <valgrind/valgrind.h>
#endif
#define niter 100
using namespace std;

jamfuture_t future;
scheduler_t scheduler_future;
task_t listener;
unsigned char listener_stack[1024 * 256];
// DOES NOT GUARANTEE ATOMICITY
task_t* placeholder = NULL;
std::chrono::time_point<std::chrono::high_resolution_clock> startt, endt;
unsigned long long int total_elapse = 0, remain = niter;
#if defined(__linux__)
pthread_barrier_t barrier;

task_t* schedule_next_future(scheduler_t* self) {
    return __atomic_exchange_n(&placeholder, NULL, __ATOMIC_ACQ_REL);
}

void before_each_future(task_t* self) {
    if (remain < niter) REQUIRE((future.lock_word & 0x80000000) > 0);
}

void callback_postwait(jamfuture_t* self) {
    __atomic_store(&placeholder, &(self->owner_task), __ATOMIC_SEQ_CST);
}

void test_future_task(task_t* self, void* args) {
    while (remain) {
        make_future(&future, self, &startt, callback_postwait);
        placeholder = &listener;
        pthread_barrier_wait(&barrier);
        get_future(&future);
        endt = std::chrono::high_resolution_clock::now();
        auto elps = std::chrono::duration_cast<std::chrono::nanoseconds>
                    (endt - startt);
        REQUIRE(elps.count() > -1);
        total_elapse += elps.count();
        
        pthread_barrier_wait(&barrier);
    }
    shutdown_scheduler(self->scheduler);
    finish_task(self, 0);
}

void test_listener() {
    while (remain) {
        pthread_barrier_wait(&barrier);
        auto r = reinterpret_cast<std::chrono::time_point
                <std::chrono::high_resolution_clock> *>(future.data);
        this_thread::sleep_for(chrono::nanoseconds(rand() % 50));
        REQUIRE((future.lock_word & 0x80000000) == 0);
        *r = std::chrono::high_resolution_clock::now();
        notify_future(&future);
        remain = remain - 1;
        pthread_barrier_wait(&barrier);
    }
}

void test_future_idle(scheduler_t* self) {
    this_thread::sleep_for(chrono::nanoseconds(1));
}
#ifndef JAMSCRIPT_ENABLE_VALGRIND
TEST_CASE("Performance Future", "[future]") {
    pthread_barrier_init(&barrier, NULL, 2);
    make_scheduler(&scheduler_future, schedule_next_future, 
                   empty_func_next_idle, before_each_future, 
                   empty_func_before_after);
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    listener.v_stack_id = VALGRIND_STACK_REGISTER(
        listener_stack, 
        (void*)((uintptr_t)listener_stack + 1024 * 256)
    );
#endif
    make_task(&listener, &scheduler_future, test_future_task, 
              NULL, 256 * 1024, listener_stack);
    thread lstn(test_listener);
    placeholder = &listener;
    scheduler_mainloop(&scheduler_future);
    lstn.join();
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(listener.v_stack_id);
#endif
    WARN("total elapse " << total_elapse << " ns, average elapse " 
         << total_elapse / niter << " ns");
}
#endif
#endif
/**
 * @test test chaining sleep/wakeup of 104 coroutines
 * @details 50% of them are regular tasks
 * @details 50% of them are xtasks
 */
deque<task_t*> sched_queue;
string secret("Java is the best programming language in the world " 
              "which has not yet been contaminated by the business\n");
string push_back_builder("");
int pushb_index = 0;

void renablr(jamfuture_t* self) {
    sched_queue.push_back(self->owner_task);
}

void secret_completer(task_t* self, void* arg) {
    {
        char ch = *reinterpret_cast<char*>(arg);
        auto secret_getter = *reinterpret_cast<pair<jamfuture_t*, 
                                jamfuture_t*>*>(self->task_fv->get_user_data(self));
        push_back_builder = push_back_builder + ch;
        double x(0.25);
        x *= double(ch);
        string push_back_builder_snapshot = push_back_builder;
        get_future(secret_getter.second);
        REQUIRE(x == Approx(0.25 * double(ch)));
        REQUIRE((push_back_builder_snapshot + 
                *reinterpret_cast<string*>(secret_getter.second->data)) == 
                secret);
        *reinterpret_cast<string*>(secret_getter.second->data) = ch + 
                                                *reinterpret_cast<string*>
                                                (secret_getter.second->data);
        
        if (secret_getter.first != NULL) {
            secret_getter.first->data = secret_getter.second->data;
            notify_future(secret_getter.first);
        }
    }
    finish_task(self, 0);
}

task_t* secret_scheduler_next(scheduler_t* self) {
    if (sched_queue.size() > 0) {
        task_t* to_run = sched_queue.front();
        sched_queue.pop_front();
        return to_run;
    } else {
        shutdown_scheduler(&scheduler_future);
        return NULL;
    }
}

TEST_CASE("Interlock-10x", "[future]") {
    string completed_secret("");
    char chx[secret.size()];
    pair<jamfuture_t*, jamfuture_t*> px[secret.size()];
    jamfuture_t* prev_future = NULL;
    shared_stack_t* sstack = make_shared_stack(1024 * 128, 
                                               malloc, free, memcpy);
    memset(&scheduler_future, 0, sizeof(scheduler_t));
    make_scheduler(&scheduler_future, secret_scheduler_next, 
                   empty_func_next_idle, empty_func_before_after, 
                   empty_func_before_after);
    for (int i = 0; i < secret.size(); i++) {
        chx[i] = secret[i];
        jamfuture_t* ptrf = reinterpret_cast<jamfuture_t*>
                            (calloc(sizeof(jamfuture_t), 1));
        px[i] = make_pair(prev_future, ptrf);
        task_t* ntask;
        if (i % 2 == 0) {
            ntask = reinterpret_cast<task_t*>(calloc(sizeof(task_t), 1));
            unsigned char* nstack = reinterpret_cast<unsigned char*>
                                    (calloc(1024 * 64, 1));
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            ntask->v_stack_id = VALGRIND_STACK_REGISTER(
                nstack, 
                (void*)((uintptr_t)nstack + 1024 * 64)
            );
#endif 
            make_task(ntask, &scheduler_future, secret_completer, &chx[i], 
                      1024 * 64, nstack);
        } else {
            ntask = make_shared_stack_task(&scheduler_future, secret_completer,
                                           &chx[i], sstack);
        }
        ntask->task_fv->set_user_data(ntask, &px[i]);
        make_future(ptrf, ntask, NULL, renablr);
        prev_future = ptrf;
        sched_queue.push_back(ntask);
    }
    prev_future->data = &completed_secret;
    notify_future(prev_future);
    scheduler_mainloop(&scheduler_future);
    REQUIRE(secret == completed_secret);
    for (int i = 0; i < secret.size(); i++) {
        task_t* to_free = px[i].second->owner_task;
        free(px[i].second);
        if (i % 2 == 0) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(to_free->v_stack_id);
#endif
            free(to_free->stack);
            free(to_free);
        } else {
            destroy_shared_stack_task(to_free);
        }
    }
    destroy_shared_stack(sstack);
}
