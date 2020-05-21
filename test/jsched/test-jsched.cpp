//
// Created by mayuxiang on 2020-05-20.
//
#include <catch2/catch.hpp>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <jamscript-impl/jamscript-scheduler.h>
#include <cstring>
#include <cstdlib>
#include <sys/resource.h>
#include <vector>
#include <iostream>
#include <thread>

int r1c, r2c, r3c, r4c;
bool b1c = false, i1c = false;

TEST_CASE("Scheduling-Paper-Sanity", "[jsched]") {
    r1c = r2c = r3c = r4c = 0;
    jamscript::c_side_scheduler jamc_sched({ { 0 * 1000,  1 * 1000,  1 }, { 1 * 1000,  3 * 1000,  4 },
                                             { 3 * 1000,  4 * 1000,  3 }, { 4 * 1000,  5 * 1000,  2 },
                                             { 5 * 1000,  6 * 1000,  1 }, { 6 * 1000,  8 * 1000,  4 },
                                             { 8 * 1000,  9 * 1000,  3 }, { 9 * 1000,  10 * 1000, 0 },
                                             { 10 * 1000, 11 * 1000, 1 }, { 11 * 1000, 12 * 1000, 2 },
                                             { 12 * 1000, 14 * 1000, 4 }, { 14 * 1000, 15 * 1000, 0 },
                                             { 15 * 1000, 16 * 1000, 1 }, { 16 * 1000, 17 * 1000, 3 },
                                             { 17 * 1000, 18 * 1000, 0 }, { 18 * 1000, 20 * 1000, 4 }, 
                                             { 20 * 1000, 21 * 1000, 1 }, { 21 * 1000, 22 * 1000, 2 }, 
                                             { 22 * 1000, 22500,     0 }, { 22500,     23500,     3 },
                                             { 23500,     24 * 1000, 0 }, { 24 * 1000, 26 * 1000, 4 }, 
                                             { 26 * 1000, 27 * 1000, 1 }, { 27 * 1000, 30 * 1000, 0 } },
                                           { { 0 * 1000,  2 * 1000,  4 }, { 2 * 1000,  3 * 1000,  1 },
                                             { 3 * 1000,  4 * 1000,  3 }, { 4 * 1000,  5 * 1000,  2 },
                                             { 5 * 1000,  9 * 1000,  0 }, { 9 * 1000,  10 * 1000, 1 },
                                             { 10 * 1000, 12 * 1000, 4 }, { 12 * 1000, 13 * 1000, 1 },
                                             { 13 * 1000, 14 * 1000, 3 }, { 14 * 1000, 16 * 1000, 4 },
                                             { 16 * 1000, 17 * 1000, 2 }, { 17 * 1000, 18 * 1000, 1 },
                                             { 18 * 1000, 19 * 1000, 3 }, { 19 * 1000, 21 * 1000, 4 }, 
                                             { 21 * 1000, 22 * 1000, 2 }, { 22 * 1000, 23 * 1000, 1 }, 
                                             { 23 * 1000, 24 * 1000, 3 }, { 24 * 1000, 26 * 1000, 4 }, 
                                             { 26 * 1000, 27 * 1000, 1 }, { 27 * 1000, 30 * 1000, 0 } }, 
                                            1024 * 256, nullptr, [] (task_t* self, void* args) {
        auto* scheduler_ptr = static_cast<jamscript::c_side_scheduler*>(self->scheduler->get_scheduler_data(self->scheduler));
        for (int v = 0; v < 2; v++) {
            for (int i = 0; i < 10; i++) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                yield_task(self);
            }
            std::cout << "FINISHED PSEUDO PREEMPT A" << std::endl;
            std::shared_ptr<jamfuture_t> handle_interactive1 = scheduler_ptr->add_interactive_task(self, 100 * 1000, 1000, &i1c, [] (task_t* self, void* args) {
                {
                    auto* i1cp = static_cast<bool*>(args);
                    auto* self_cpp = static_cast<jamscript::interactive_extender*>(self->task_fv->get_user_data(self));
                    *i1cp = true;
                    std::cout << "INTERAC" << std::endl;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    yield_task(self);
                    notify_future(self_cpp->handle.get());
                }
                finish_task(self, EXIT_SUCCESS);
            });
            std::cout << "GOT HANDLE" << std::endl;
            for (int i = 0; i < 10; i++) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                yield_task(self);
            }
            std::cout << "FINISHED PSEUDO PREEMPT B" << std::endl;
            scheduler_ptr->add_batch_task(1000, nullptr, [] (task_t* self, void* args) {
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
                b1c = true;
                std::cout << "BATCH" << std::endl;
                yield_task(self);
            });
            get_future(handle_interactive1.get());
            for (int i = 0; i < 10; i++) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                yield_task(self);
            }
            if (handle_interactive1->status == ack_cancelled) i1c = true;
            
        }
        scheduler_ptr->exit();
        finish_task(self, EXIT_SUCCESS);
    });
    std::thread ar1([&] () {
        while (jamc_sched.is_running()) {
            jamc_sched.add_real_time_task(1, &r1c, [] (task_t* self, void* args) {
                {
                    auto* r1cp = static_cast<int*>(args);
                    std::this_thread::sleep_for(std::chrono::microseconds(1000));
                    std::cout << "TASK1 EXEC" << std::endl;
                    (*r1cp)++;
                }
                finish_task(self, EXIT_SUCCESS);
            });
            std::this_thread::sleep_for(std::chrono::microseconds(5000));
        }
    });
    std::thread ar2([&] () {
        while (jamc_sched.is_running()) {
            jamc_sched.add_real_time_task(2, &r2c, [] (task_t* self, void* args) {
                {
                    auto* r2cp = static_cast<int*>(args);
                    std::this_thread::sleep_for(std::chrono::microseconds(1000));
                    std::cout << "TASK2 EXEC" << std::endl;
                    (*r2cp)++;
                }
                finish_task(self, EXIT_SUCCESS);
            });
            std::this_thread::sleep_for(std::chrono::microseconds(10000));
        }
    });
    std::thread ar3([&] () {
        while (jamc_sched.is_running()) {
            jamc_sched.add_real_time_task(3, &r3c, [] (task_t* self, void* args) {
                {
                    auto* r3cp = static_cast<int*>(args);
                    std::this_thread::sleep_for(std::chrono::microseconds(1000));
                    std::cout << "TASK3 EXEC" << std::endl;
                    (*r3cp)++;
                }
                finish_task(self, EXIT_SUCCESS);
            });
            std::this_thread::sleep_for(std::chrono::microseconds(7500));
        }
    });
    std::thread ar4([&] () {
        while (jamc_sched.is_running()) {
            jamc_sched.add_real_time_task(4, &r4c, [] (task_t* self, void* args) {
                {
                    auto* r4cp = static_cast<int*>(args);
                    std::this_thread::sleep_for(std::chrono::microseconds(2000));
                    std::cout << "TASK4 EXEC" << std::endl;
                    (*r4cp)++;
                }
                finish_task(self, EXIT_SUCCESS);
            });
            std::this_thread::sleep_for(std::chrono::microseconds(6000));
        }
    });
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
    jamc_sched.run();
    ar1.join();
    ar2.join();
    ar3.join();
    ar4.join();
    REQUIRE(i1c);
#ifndef JAMSCRIPT_ENABLE_VALGRIND
    REQUIRE(b1c);
    REQUIRE(r1c >= 6);
    REQUIRE(r2c >= 3);
    REQUIRE(r3c >= 4);
    REQUIRE(r4c >= 5);
#endif
}