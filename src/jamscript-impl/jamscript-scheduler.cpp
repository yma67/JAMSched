/// Copyright 2020 Yuxiang Ma, Muthucumaru Maheswaran 
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
#include "jamscript-impl/jamscript-scheduler.h"
#include <memory>
#include <random>
#include <thread>
#include <cstring>
#include <iostream>
#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/helgrind.h>
#include <valgrind/valgrind.h>
#endif

void jamscript::before_each_jam_impl(task_t *self) {
    static_cast<c_side_scheduler*>(self->scheduler->
    get_scheduler_data(self->scheduler))->task_start_time =
            std::chrono::high_resolution_clock::now();
}

void jamscript::after_each_jam_impl(task_t *self) {
    auto* traits = static_cast<c_side_task_extender*>(
                self->task_fv->get_user_data(self)
            );
    auto* scheduler_ptr = static_cast<c_side_scheduler*>(
                self->scheduler->get_scheduler_data(self->scheduler)
            );
    auto task_current_time = std::chrono::high_resolution_clock::now();
    auto actual_exec_time = std::chrono::duration_cast<
                std::chrono::microseconds
            >(task_current_time - scheduler_ptr->task_start_time).count();
    auto current_time = std::chrono::duration_cast<
                std::chrono::microseconds
            >(task_current_time - scheduler_ptr->scheduler_start_time).count();
    if (traits->task_type == jamscript::real_time_task_t) {
        auto* cpp_task_traits2 = static_cast<real_time_extender*>(
                    self->task_fv->get_user_data(self)
                );
        while (current_time < cpp_task_traits2->deadline + 
               scheduler_ptr->current_schedule->back().end_time * 
               scheduler_ptr->multiplier) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            current_time = std::chrono::duration_cast<
                std::chrono::microseconds
            >(std::chrono::high_resolution_clock::now() - 
              scheduler_ptr->scheduler_start_time).count();
        }
    }
    if (traits->task_type == jamscript::interactive_task_t) {
        auto* cpp_task_traits2 = static_cast<interactive_extender*>(
                    self->task_fv->get_user_data(self)
                );
        scheduler_ptr->virtual_clock_interactive += actual_exec_time;
        cpp_task_traits2->burst -= actual_exec_time;
        if (self->task_status == TASK_READY) {
            scheduler_ptr->interactive_queue.push({
                cpp_task_traits2->deadline, self
            });
        } else if (self->task_status == TASK_PENDING) {
            scheduler_ptr->interactive_wait.insert(self);
        }
    }
    if (traits->task_type == jamscript::batch_task_t) {
        auto* cpp_task_traits2 = static_cast<batch_extender*>(
                    self->task_fv->get_user_data(self)
                );
        scheduler_ptr->virtual_clock_batch += actual_exec_time;
        cpp_task_traits2->burst -= actual_exec_time;
        if (self->task_status == TASK_READY) {
            std::unique_lock<std::mutex> lock(
                        scheduler_ptr->batch_tasks_mutex
                    );
            scheduler_ptr->batch_queue.push_back(self);
        } else if (self->task_status == TASK_PENDING) {
            scheduler_ptr->batch_wait.insert(self);
        }
    }
    if (self->task_status == TASK_FINISHED && 
        self != scheduler_ptr->c_local_app_task) {
        if (self->return_value == ERROR_TASK_STACK_OVERFLOW) {
            self->scheduler->cont = 0;
        }
        if (traits->task_type == jamscript::interactive_task_t ||
            traits->task_type == jamscript::batch_task_t) {
            // notify future in coroutine
            if (traits->task_type == jamscript::interactive_task_t) {
                delete static_cast<interactive_extender*>(
                            self->task_fv->get_user_data(self)
                        );
            } else {
                delete static_cast<batch_extender*>(
                            self->task_fv->get_user_data(self)
                        );
            }
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            VALGRIND_STACK_DEREGISTER(self->v_stack_id);
#endif
            delete[] self->stack;
            delete self;
        } else {
            auto* traits2 = static_cast<real_time_extender*>(
                        self->task_fv->get_user_data(self)
                    );
            delete traits2;
            destroy_shared_stack_task(self);
        }
    }
}

task_t *jamscript::next_task_jam_impl(scheduler_t *self_c) {
    auto* self = static_cast<c_side_scheduler*>(
                self_c->get_scheduler_data(self_c)
            );
    uint64_t current_time_point = std::chrono::duration_cast<
                std::chrono::microseconds
            >(std::chrono::high_resolution_clock::now() -
              self->scheduler_start_time).count();
    while (!(self->current_schedule->at(self->current_schedule_slot)
           .inside(current_time_point, self->current_schedule->back().end_time,
                   self->multiplier))) {
        self->current_schedule_slot = self->current_schedule_slot + 1;
        if (self->current_schedule_slot >= self->current_schedule->size()) {
            self->current_schedule_slot = 0;
            self->multiplier++;
            self->current_schedule = self->decide();
        }
        current_time_point = std::chrono::duration_cast<
            std::chrono::microseconds
        >(std::chrono::high_resolution_clock::now() -
            self->scheduler_start_time).count();
    }
    auto& current_tasks = self->real_time_tasks_map[
            self->current_schedule->at(
                self->current_schedule_slot
            ).task_id];
    if (current_tasks.empty()) {
        bool is_interactive;
        if (self->batch_queue.empty() && self->interactive_queue.empty()) {
            return nullptr;
        } else if (self->batch_queue.empty() ||
                   self->interactive_queue.empty()) {
            is_interactive = !self->interactive_queue.empty();
        } else {
            is_interactive = self->virtual_clock_batch >=
                             self->virtual_clock_interactive;
        }
        task_t* to_return;
        if (is_interactive) {
            interactive_extender* to_cancel_ext;
            while (!self->interactive_queue.empty()) {
                to_return = self->interactive_queue.top().second;
                to_cancel_ext = static_cast<interactive_extender*>(
                            to_return->task_fv->get_user_data(to_return)
                        );
                if ((to_cancel_ext->deadline - to_cancel_ext->burst) <= 
                     current_time_point) {
                    self->interactive_stack.push_back(to_return);
                    while (self->interactive_stack.size() > 3) {
                        task_t* to_drop = self->interactive_stack.front();
                        auto* to_drop_ext = static_cast<interactive_extender*>(
                            to_drop->task_fv->get_user_data(to_drop)
                        );
                        to_drop_ext->handle->status = ack_cancelled;
                        notify_future(to_drop_ext->handle.get());
                        delete[] to_drop->stack;
                        delete to_drop_ext;
                        delete to_drop;
                        self->interactive_stack.pop_front();
                    }
                    self->interactive_queue.pop();
                } else {
                    break;
                }
            }
            if (self->interactive_queue.empty()) {
                if (self->interactive_stack.empty()) {
                    return nullptr;
                } else {
                    to_return = self->interactive_stack.back();
                    self->interactive_stack.pop_back();
                    return to_return;
                }
            } else {
                self->interactive_queue.pop();
                return to_return;
            }
        } else {
            std::unique_lock<std::mutex> lock(self->batch_tasks_mutex);
            to_return = self->batch_queue.front();
            self->batch_queue.pop_front();
        }
        return to_return;
    }
    task_t* current_rt = current_tasks.back();
    current_tasks.pop_back();
    auto* current_rt_extender = static_cast<real_time_extender*>(
        current_rt->task_fv->get_user_data(current_rt)
    );
    current_rt_extender->start = self->current_schedule->at(
                self->current_schedule_slot
            ).start_time;
    current_rt_extender->deadline = self->current_schedule->at(
                self->current_schedule_slot
            ).end_time;
    return current_rt;
}

void jamscript::idle_task_jam_impl(scheduler_t *) {

}

void jamscript::interactive_task_handle_post_callback(jamfuture_t *self) {
    auto* cpp_task_traits = static_cast<c_side_task_extender*>(
                self->owner_task->task_fv->get_user_data(self->owner_task)
            );
    auto* cpp_scheduler = static_cast<c_side_scheduler*>(
                    self->owner_task->scheduler->get_scheduler_data(
                                self->owner_task->scheduler
                    )
                );
    if (cpp_task_traits->task_type == jamscript::interactive_task_t) {
        auto* cpp_task_traits2 = static_cast<interactive_extender*>(
                    self->owner_task->task_fv->get_user_data(self->owner_task)
                );
        cpp_scheduler->interactive_wait.erase(self->owner_task);
        cpp_scheduler->interactive_queue.push({
            cpp_task_traits2->deadline, self->owner_task
        });
    }
    if (cpp_task_traits->task_type == jamscript::batch_task_t) {
        cpp_scheduler->batch_wait.erase(self->owner_task);
        std::unique_lock<std::mutex> lock(cpp_scheduler->batch_tasks_mutex);
        cpp_scheduler->batch_queue.push_back(self->owner_task);
    }
}

jamscript::c_side_scheduler::c_side_scheduler(std::vector<task_schedule_entry>
                                              normal_schedule,
                                              std::vector<task_schedule_entry>
                                              greedy_schedule,
                                              uint32_t stack_size,
                                              void* local_app_args,
                                              void (*local_app_fn)(task_t *,
                                                                   void *)) :
                                              c_scheduler(nullptr),
                                              c_local_app_task(nullptr),
                                              c_shared_stack(nullptr),
                                              current_schedule_slot(0),
                                              current_schedule(nullptr),
                                              multiplier(0),
                                              scheduler_start_time(
                                                  std::chrono::
                                                  high_resolution_clock::
                                                  now()),
                                              task_start_time(
                                                  std::chrono::
                                                  high_resolution_clock::
                                                  now()),
                                              virtual_clock_batch(0L),
                                              virtual_clock_interactive(0L),
                                              interactive_queue(edf_cmp),
                                              normal_schedule(
                                                  std::move(normal_schedule)),
                                              greedy_schedule(
                                                  std::move(greedy_schedule)) {
    c_scheduler = new scheduler_t;
    make_scheduler(c_scheduler, next_task_jam_impl, idle_task_jam_impl,
                   before_each_jam_impl, after_each_jam_impl);
    c_scheduler->set_scheduler_data(c_scheduler, this);
    c_shared_stack = make_shared_stack(stack_size, malloc, free, memcpy);
    if (c_shared_stack == nullptr) {
        throw std::bad_alloc();
    }
    c_local_app_task = new task_t;
    auto* c_local_app_task_stack = new unsigned char[stack_size];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    c_local_app_task->v_stack_id = VALGRIND_STACK_REGISTER(
        c_local_app_task_stack, 
        (void*)((uintptr_t)c_local_app_task_stack + stack_size)
    );
#endif
    auto* c_local_app_task_extender = new batch_extender;
    c_local_app_task_extender->task_type = jamscript::batch_task_t;
    c_local_app_task_extender->burst = std::numeric_limits<uint64_t>::max();
    make_task(c_local_app_task, c_scheduler, local_app_fn, local_app_args,
              stack_size, c_local_app_task_stack);
    c_local_app_task->task_fv->set_user_data(c_local_app_task,
                                             c_local_app_task_extender);
    real_time_tasks_map[0x0] = {};
    current_schedule = decide();
    batch_queue.push_back(c_local_app_task);
}

std::shared_ptr<jamfuture_t>
jamscript::c_side_scheduler::add_interactive_task(task_t *parent_task,
                                                  uint64_t deadline,
                                                  uint64_t burst,
                                                  void *interactive_task_args,
                                                  void (*interactive_task_fn)(
                                                          task_t *, void *)) {
    auto int_task_handle = std::make_shared<jamfuture_t>();
    make_future(int_task_handle.get(), parent_task, nullptr,
                interactive_task_handle_post_callback);
    int_task_handle->lock_word = 0x0;
    auto* int_task = new task_t;
    auto* int_task_stack = new unsigned char[parent_task->stack_size];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    int_task->v_stack_id = VALGRIND_STACK_REGISTER(
        int_task_stack, 
        (void*)((uintptr_t)int_task_stack + parent_task->stack_size)
    );
#endif
    auto* int_task_extender = new interactive_extender;
    int_task_extender->task_type = jamscript::interactive_task_t;
    int_task_extender->deadline = deadline;
    int_task_extender->burst = burst;
    int_task_extender->handle = int_task_handle;
    make_task(int_task, parent_task->scheduler, interactive_task_fn,
              interactive_task_args, parent_task->stack_size, int_task_stack);
    int_task->task_fv->set_user_data(int_task, int_task_extender);
    interactive_queue.push({ deadline, int_task });
    return int_task_handle;
}

bool jamscript::c_side_scheduler::add_batch_task(uint32_t burst, void* args,
                                                 void (*batch_fn)(task_t *,
                                                                  void *)) {
    if (c_shared_stack->is_allocatable) {
        auto* batch_task = new task_t;
        auto* batch_task_stack = new unsigned char[
                    c_shared_stack->__shared_stack_size
                ];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        batch_task->v_stack_id = VALGRIND_STACK_REGISTER(
            batch_task_stack, 
            (void*)((uintptr_t)batch_task_stack + 
                    c_shared_stack->__shared_stack_size)
        );
#endif
        auto* batch_task_extender = new batch_extender;
        batch_task_extender->task_type = jamscript::batch_task_t;
        batch_task_extender->burst = burst;
        make_task(batch_task, c_scheduler, batch_fn, args,
                  c_shared_stack->__shared_stack_size, batch_task_stack);
        batch_task->task_fv->set_user_data(batch_task, batch_task_extender);
        {
            std::unique_lock<std::mutex> lock(batch_tasks_mutex);
            batch_queue.push_back(batch_task);
        }
    }
    return false;
}

bool jamscript::c_side_scheduler::add_real_time_task(uint32_t task_id,
                                                     void* args,
                                                     void (*real_time_task_fn)(
                                                           task_t *, void *)) {
    if (c_shared_stack->is_allocatable) {
        task_t* new_xtask = make_shared_stack_task(c_scheduler,
                                                   real_time_task_fn, args,
                                                   c_shared_stack);
        if (new_xtask == nullptr) {
            throw std::bad_alloc();
        }
        auto* new_xextender = new real_time_extender;
        new_xextender->task_type = jamscript::real_time_task_t;
        new_xextender->id = task_id;
        new_xtask->task_fv->set_user_data(new_xtask, new_xextender);
        {
            std::unique_lock<std::mutex> lock(real_time_tasks_mutex);
            real_time_tasks_map[task_id].push_back(new_xtask);
            return true;
        }
    }
    return false;
}

jamscript::c_side_scheduler::~c_side_scheduler() {
    while (!interactive_queue.empty()) {
        auto [ddl, task] = interactive_queue.top();
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        delete[] task->stack;
        delete static_cast<interactive_extender*>(
                task->task_fv->get_user_data(task)
        );
        delete task;
        interactive_queue.pop();
    }
    for (auto task: interactive_wait) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        delete[] task->stack;
        delete static_cast<interactive_extender*>(
                task->task_fv->get_user_data(task)
        );
        delete task;
    }
    for (auto task: interactive_stack) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        delete[] task->stack;
        delete static_cast<interactive_extender*>(
                task->task_fv->get_user_data(task)
        );
        delete task;
    }
    for (auto& [id, tasks]: real_time_tasks_map) {
        while (!tasks.empty()) {
            task_t* task = tasks.back();
            delete static_cast<real_time_extender*>(
                    task->task_fv->get_user_data(task)
            );
            destroy_shared_stack_task(task);
            tasks.pop_back();
        }
    }
    for (auto task: batch_queue) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        if (task == c_local_app_task) continue;
        delete[] task->stack;
        delete static_cast<batch_extender*>(
                task->task_fv->get_user_data(task)
        );
        delete task;
    }
    for (auto task: batch_wait) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        if (task == c_local_app_task) continue;
        delete[] task->stack;
        delete static_cast<batch_extender*>(
                task->task_fv->get_user_data(task)
        );
        delete task;
    }
    destroy_shared_stack(c_shared_stack);
    delete[] c_local_app_task->stack;
    delete static_cast<batch_extender*>(c_local_app_task->task_fv->
                                        get_user_data(c_local_app_task));
    delete c_local_app_task;
    delete c_scheduler;
}

std::vector<jamscript::task_schedule_entry> *
jamscript::c_side_scheduler::decide() {
    if (rand() % 2 == 0) {
        std::cout << "NORMAL" << std::endl;
        return &normal_schedule;
    } else {
        std::cout << "GREEDY" << std::endl;
        return &greedy_schedule;
    } 
}

void jamscript::c_side_scheduler::run() {
    scheduler_start_time = std::chrono::high_resolution_clock::now();
    scheduler_mainloop(c_scheduler);
}

bool jamscript::c_side_scheduler::is_running() {
    return c_scheduler->cont != 0;
}

void jamscript::c_side_scheduler::exit() {
    c_scheduler->cont = 0;
}