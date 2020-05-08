#include <catch2/catch.hpp>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <future/future.h>
#include <cstring>
#include <chrono>
#include <pthread.h>
#include <thread>
#include <iostream>
#include <string>
#include <queue>
#include <vector>

#define niter 10000

using namespace std;

jamfuture_t future;
scheduler_t scheduler_future;
task_t listener;
unsigned char listener_stack[1024 * 256];
// DOES NOT GUARANTEE ATOMICITY
task_t* placeholder = NULL;
std::chrono::time_point<std::chrono::high_resolution_clock> startt, endt;
unsigned long long int total_elapse = 0, remain = niter;
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
        auto elps = std::chrono::duration_cast<std::chrono::nanoseconds>(endt - startt);
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
        auto r = reinterpret_cast<std::chrono::time_point<std::chrono::high_resolution_clock> *>(future.data);
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

TEST_CASE("Performance Future", "[future]") {
    pthread_barrier_init(&barrier, NULL, 2);
    make_scheduler(&scheduler_future, schedule_next_future, empty_func_next_idle, before_each_future, empty_func_before_after);
    make_task(&listener, &scheduler_future, test_future_task, NULL, NULL, 256 * 1024, listener_stack);
    thread lstn(test_listener);
    placeholder = &listener;
    scheduler_mainloop(&scheduler_future);
    lstn.join();
    WARN("total elapse " << total_elapse << " ns, average elapse " << total_elapse / niter << " ns");
}

/**
 * @test test chaining sleep/wakeup of 104 coroutines
 * @details 50% of them are regular tasks
 * @details 50% of them are xtasks
 */

deque<task_t*> sched_queue;
string secret("Java is the best programming language in the world which has not yet been contaminated by the business\n");
string push_back_builder("");

void renablr(jamfuture_t* self) {
    sched_queue.push_back(self->owner_task);
}

void secret_completer(task_t* self, void* arg) {
    char ch = *reinterpret_cast<char*>(arg);
    pair<jamfuture_t*, jamfuture_t*> secret_getter = *reinterpret_cast<pair<jamfuture_t*, jamfuture_t*>*>(self->get_user_data(self));
    string push_back_builder_snapshot = push_back_builder + ch;
    push_back_builder = push_back_builder_snapshot;
    get_future(secret_getter.second);
    REQUIRE((push_back_builder_snapshot + *reinterpret_cast<string*>(secret_getter.second->data)) == secret);
    *reinterpret_cast<string*>(secret_getter.second->data) = ch + *reinterpret_cast<string*>(secret_getter.second->data);
    if (secret_getter.first != NULL) {
        secret_getter.first->data = secret_getter.second->data;
        notify_future(secret_getter.first);
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

TEST_CASE("Interlock 10x", "[future]") {
    string completed_secret("");
    char chx[secret.size()];
    pair<jamfuture_t*, jamfuture_t*> px[secret.size()];
    jamfuture_t* prev_future = NULL;
    shared_stack_t* sstack = make_shared_stack(1024 * 128, malloc, free, memcpy);
    make_scheduler(&scheduler_future, secret_scheduler_next, empty_func_next_idle, empty_func_before_after, empty_func_before_after);
    for (int i = 0; i < secret.size(); i++) {
        jamfuture_t* ptrf = reinterpret_cast<jamfuture_t*>(malloc(sizeof(jamfuture_t)));
        chx[i] = secret[i];
        px[i] = make_pair(prev_future, ptrf);
        task_t* ntask;
        if (i % 2 == 0) {
            ntask = reinterpret_cast<task_t*>(malloc(sizeof(task_t)));
            unsigned char* nstack = reinterpret_cast<unsigned char*>(malloc(1024 * 64));
            make_task(ntask, &scheduler_future, secret_completer, &chx[i], &px[i], 1024 * 64, nstack);
            sched_queue.push_back(ntask);
        } else {
            ntask = make_shared_stack_task(&scheduler_future, secret_completer, &chx[i], &px[i], sstack);
        }
        make_future(ptrf, ntask, NULL, renablr);
        sched_queue.push_back(ntask);
        prev_future = ptrf;
    }
    prev_future->data = &completed_secret;
    notify_future(prev_future);
    scheduler_mainloop(&scheduler_future);
    REQUIRE(completed_secret == secret);
}