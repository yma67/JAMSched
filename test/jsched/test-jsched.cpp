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
#include <utility>
#include <iostream>
#include <thread>

int r1c, r2c, r3c, r4c;
bool b1c = false, i1c = false;
uint32_t sleep_time;

TEST_CASE("Scheduling-Paper-Sanity", "[jsched]") {
    r1c = r2c = r3c = r4c = 0;
    sleep_time = 1000;
    jamscript::c_side_scheduler jamc_sched(
        {   { 0 * 1000,  1 * 1000,  1 }, { 1 * 1000,  3 * 1000,  4 },
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
            { 26 * 1000, 27 * 1000, 1 }, { 27 * 1000, 30 * 1000, 0 }   },
        {   { 0 * 1000,  2 * 1000,  4 }, { 2 * 1000,  3 * 1000,  1 },
            { 3 * 1000,  4 * 1000,  3 }, { 4 * 1000,  5 * 1000,  2 },
            { 5 * 1000,  9 * 1000,  0 }, { 9 * 1000,  10 * 1000, 1 },
            { 10 * 1000, 12 * 1000, 4 }, { 12 * 1000, 13 * 1000, 1 },
            { 13 * 1000, 14 * 1000, 3 }, { 14 * 1000, 16 * 1000, 4 },
            { 16 * 1000, 17 * 1000, 2 }, { 17 * 1000, 18 * 1000, 1 },
            { 18 * 1000, 19 * 1000, 3 }, { 19 * 1000, 21 * 1000, 4 }, 
            { 21 * 1000, 22 * 1000, 2 }, { 22 * 1000, 23 * 1000, 1 }, 
            { 23 * 1000, 24 * 1000, 3 }, { 24 * 1000, 26 * 1000, 4 }, 
            { 26 * 1000, 27 * 1000, 1 }, { 27 * 1000, 30 * 1000, 0 }   }, 
        1024 * 256, nullptr, [] (task_t* self, void* args) {
        std::cout << "LOCAL START" << std::endl;
        yield_task(self);
        auto* scheduler_ptr = static_cast<jamscript::c_side_scheduler*>(
                self->scheduler->get_scheduler_data(self->scheduler)
            );
        /*for (int v = 0; v < 2; v++) {
            std::cout << "FINISHED PSEUDO PREEMPT A" << std::endl;
            std::shared_ptr<jamfuture_t> handle_interactive1 = scheduler_ptr->
            add_interactive_task(self, 30 * 1000, 500, &i1c, 
                                 [] (task_t* self, void* args) {
                {
                    auto* i1cp = static_cast<bool*>(args);
                    auto* self_cpp = static_cast<
                            jamscript::interactive_extender*
                        >(
                            self->task_fv->get_user_data(self)
                        );
                    *i1cp = true;
                    std::cout << "INTERAC" << std::endl;
                    for (int i = 0; i < 100; i++) {
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(5)
                        );
                        yield_task(self);
                    }
                    notify_future(self_cpp->handle.get());
                }
                finish_task(self, EXIT_SUCCESS);
            });
            std::cout << "FINISHED PSEUDO PREEMPT B" << std::endl;
            scheduler_ptr->add_batch_task(500, &sleep_time, 
                                          [] (task_t* self, void* args) {
                for (int i = 0; i < 100; i++) {
                    std::this_thread::sleep_for(std::chrono::microseconds(
                                *static_cast<uint32_t*>(args) / 200
                            ));
                    yield_task(self);
                }
                b1c = true;
                std::cout << "BATCH" << std::endl;
                yield_task(self);
            });
            get_future(handle_interactive1.get());
            std::cout << "GOT HANDLE" << std::endl;
            if (handle_interactive1->status == ack_cancelled) i1c = true;
        }*/
        while (scheduler_ptr->multiplier < 3) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            yield_task(self);
        }
        scheduler_ptr->exit();
        finish_task(self, EXIT_SUCCESS);
    });
    auto rt1 = [] (task_t* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, 
            jamscript::c_side_scheduler*>*>(args);
            std::this_thread::sleep_for(
                    std::chrono::microseconds(900)
                );
            std::cout << "TASK1 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->add_real_time_task(1, args, self->task_function);
        }
        finish_task(self, EXIT_SUCCESS);
    };
    auto rt2 = [] (task_t* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, 
            jamscript::c_side_scheduler*>*>(args);
            std::this_thread::sleep_for(
                    std::chrono::microseconds(900)
                );
            std::cout << "TASK2 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->add_real_time_task(2, args, self->task_function);
        }
        finish_task(self, EXIT_SUCCESS);
    };
    auto rt3 = [] (task_t* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, 
            jamscript::c_side_scheduler*>*>(args);
            std::this_thread::sleep_for(
                    std::chrono::microseconds(900)
                );
            std::cout << "TASK3 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->add_real_time_task(3, args, self->task_function);
        }
        finish_task(self, EXIT_SUCCESS);
    };
    auto rt4 = [] (task_t* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, 
            jamscript::c_side_scheduler*>*>(args);
            std::this_thread::sleep_for(
                    std::chrono::microseconds(1900)
                );
            std::cout << "TASK4 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->add_real_time_task(4, args, self->task_function);
        }
        finish_task(self, EXIT_SUCCESS);
    };
    std::pair<void*, jamscript::c_side_scheduler*> 
    pack1({ &r1c, &jamc_sched }), pack2({ &r2c, &jamc_sched }),
    pack3({ &r3c, &jamc_sched }), pack4({ &r4c, &jamc_sched });
    jamc_sched.add_real_time_task(1, &pack1, rt1);
    jamc_sched.add_real_time_task(2, &pack2, rt2);
    jamc_sched.add_real_time_task(3, &pack3, rt3);
    jamc_sched.add_real_time_task(4, &pack4, rt4);
    jamc_sched.run();
    REQUIRE(i1c);
#ifndef JAMSCRIPT_ENABLE_VALGRIND
    REQUIRE(b1c);
    REQUIRE(r1c >= 6);
    REQUIRE(r2c >= 3);
    REQUIRE(r3c >= 4);
    REQUIRE(r4c >= 5);
#endif
}