#include <catch2/catch.hpp>
#include <core/scheduler/task.h>
#include <future/future.h>
#include <cstring>
#include <chrono>
#include <pthread.h>
#include <thread>
#include <iostream>

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


task_t* schedule_next_future() {
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
        REQUIRE((__atomic_load_n(&(future.lock_word), __ATOMIC_ACQUIRE) & 0x80000000) != 0);
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

void test_future_idle() {
    this_thread::sleep_for(chrono::nanoseconds(1));
}

TEST_CASE("Performance", "[future]") {
    pthread_barrier_init(&barrier, NULL, 2);
    make_scheduler(&scheduler_future, schedule_next_future, empty_func_next_idle, before_each_future, empty_func_before_after, memset);
    make_task(&listener, &scheduler_future, test_future_task, memset, NULL, NULL, 256 * 1024, listener_stack);
    thread lstn(test_listener);
    placeholder = &listener;
    scheduler_mainloop(&scheduler_future);
    lstn.join();
    WARN("total elapse " << total_elapse << " ns, average elapse " << total_elapse / niter << " ns");
}