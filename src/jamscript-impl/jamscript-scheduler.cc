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
#include <memory>
#include <random>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#ifdef JAMSCRIPT_SCHED_AI_EXP
#include <iostream>
#endif
#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/helgrind.h>
#include <valgrind/valgrind.h>
#endif
#include "jamscript-impl/jamscript-scheduler.hh"

jamscript::c_side_scheduler::
c_side_scheduler(std::vector<task_schedule_entry> normal_schedule, 
                 std::vector<task_schedule_entry> greedy_schedule,
                 uint32_t device_id, uint32_t stack_size, void* local_app_args,
                 void (*local_app_fn)(task_t*, void*)) :
current_schedule_slot(0), multiplier(0), device_id(device_id), 
virtual_clock{ 0L, 0L }, rt_manager(this, stack_size),
task_start_time(std::chrono::high_resolution_clock::now()), 
scheduler_start_time(task_start_time), cycle_start_time(task_start_time), 
normal_schedule(std::move(normal_schedule)), 
greedy_schedule(std::move(greedy_schedule)), 
s_managers{ std::make_unique<interactive_manager>(this, stack_size), 
            std::make_unique<batch_manager>(this, stack_size) }, dec(this) {
    c_scheduler = new scheduler_t;
    make_scheduler(c_scheduler, next_task_jam_impl, idle_task_jam_impl,
                   before_each_jam_impl, after_each_jam_impl);
    c_scheduler->set_scheduler_data(c_scheduler, this);
    if (s_managers[batch_task_t]->add(std::numeric_limits<uint64_t>::max(), 
        local_app_args, local_app_fn) == nullptr) {
        throw std::bad_alloc();
    }
    download_schedule();
    current_schedule = decide();
}

jamscript::c_side_scheduler::~c_side_scheduler() {
    delete c_scheduler;
}

void 
jamscript::before_each_jam_impl(task_t *self) {
    auto* self_task = static_cast<c_side_task_extender*>(
        self->task_fv->get_user_data(self)
    );
    auto* self_sched = static_cast<c_side_scheduler*>(
        self->scheduler->get_scheduler_data(self->scheduler)
    );
    self_sched->task_start_time = std::chrono::high_resolution_clock::now();
    if (self_task->task_type == jamscript::real_time_task_t) {
        self_sched->total_jitter.push_back(std::abs(
            (long long)self_sched->get_current_timepoint_in_cycle() / 1000 - 
            (long long)(self_sched->current_schedule->
                        at(self_sched->current_schedule_slot).start_time)
        ));
    }
}

void 
jamscript::after_each_jam_impl(task_t *self) {
    auto* traits = static_cast<c_side_task_extender*>(
        self->task_fv->get_user_data(self)
    );
    auto* scheduler_ptr = static_cast<c_side_scheduler*>(
        self->scheduler->get_scheduler_data(self->scheduler)
    );
    auto actual_exec_time = scheduler_ptr->get_current_timepoint_in_task();
    if (traits->task_type != jamscript::real_time_task_t) {
        scheduler_ptr->virtual_clock[traits->task_type] += actual_exec_time / 1000;
        scheduler_ptr->s_managers[traits->task_type]->
        update_burst(self, actual_exec_time / 1000);
        {
            std::lock_guard<std::mutex> l(scheduler_ptr->future_mutex);
            task_status_t st = __atomic_load_n(&(self->task_status), 
                                            __ATOMIC_ACQUIRE);
            if (st == TASK_READY) {
                scheduler_ptr->s_managers[traits->task_type]->enable(self);
            } else if (st == TASK_PENDING) {
                scheduler_ptr->s_managers[traits->task_type]->pause(self);
            }
        }
    } else {
        scheduler_ptr->rt_manager.spin_until_end();
    }
    if (self->task_status == TASK_FINISHED) {
        if (self->return_value == ERROR_TASK_STACK_OVERFLOW) {
            scheduler_ptr->rt_manager.c_shared_stack->is_allocatable = 0;
        }
        if (traits->task_type == jamscript::real_time_task_t) {
            scheduler_ptr->rt_manager.remove(self);
        } else {
            scheduler_ptr->s_managers[traits->task_type]->remove(self);
        }
    }
}

task_t*
jamscript::next_task_jam_impl(scheduler_t *self_c) {
    auto* self = static_cast<c_side_scheduler*>(
        self_c->get_scheduler_data(self_c)
    );
    self->move_scheduler_slot();
    task_t* to_dispatch = self->rt_manager.dispatch(
        self->current_schedule->at(self->current_schedule_slot).task_id
    );
    if (to_dispatch == nullptr) {
        if (self->s_managers[interactive_task_t]->size() == 0 && 
            self->s_managers[batch_task_t]->size() == 0) {
            return nullptr;
        } else if (self->s_managers[interactive_task_t]->size() == 0 ||
                   self->s_managers[batch_task_t]->size() == 0) {
            if (self->s_managers[interactive_task_t]->size() > 
                self->s_managers[batch_task_t]->size()) {
                return self->s_managers[interactive_task_t]->dispatch();
            } else {
                return self->s_managers[batch_task_t]->dispatch();
            }
        } else {
            if (self->virtual_clock[interactive_task_t] < 
                self->virtual_clock[batch_task_t]) {
                return self->s_managers[interactive_task_t]->dispatch();
            } else {
                return self->s_managers[batch_task_t]->dispatch();
            }
        }
    }
    return to_dispatch;
}

void 
jamscript::idle_task_jam_impl(scheduler_t *) {

}

void 
jamscript::interactive_task_handle_post_callback(jamfuture_t *self) {
    auto* cpp_task_traits = static_cast<c_side_task_extender*>(
        self->owner_task->task_fv->get_user_data(self->owner_task)
    );
    auto* cpp_scheduler = static_cast<c_side_scheduler*>(
        self->owner_task->scheduler->get_scheduler_data(
            self->owner_task->scheduler
        )
    );
    std::lock_guard<std::mutex> lock(cpp_scheduler->future_mutex);
    cpp_scheduler->
    s_managers[cpp_task_traits->task_type]->ready(self->owner_task);
}

std::shared_ptr<jamfuture_t>
jamscript::c_side_scheduler::
add_interactive_task(task_t *parent_task, uint64_t deadline,
                     uint64_t burst, void *interactive_task_args,
                     void (*interactive_task_fn)(task_t *, void *)) {
    if (!rt_manager.c_shared_stack->is_allocatable) return nullptr;
    task_t* handle = s_managers[interactive_task_t]->
    add(parent_task, deadline, burst, 
        interactive_task_args, interactive_task_fn);
    if (handle == nullptr) {
        rt_manager.c_shared_stack->is_allocatable = 0;
        return nullptr;
    }
    return static_cast<interactive_extender*>(handle->task_fv->
            get_user_data(handle))->handle;
}

bool
jamscript::c_side_scheduler::
add_batch_task(uint32_t burst, void* args, void (*batch_fn)(task_t*, void*)) {
    if (!rt_manager.c_shared_stack->is_allocatable) return false;
    task_t* handle = s_managers[batch_task_t]->add(burst, args, batch_fn);
    if (handle == nullptr) {
        rt_manager.c_shared_stack->is_allocatable = 0;
        return false;
    }
    return true;
}

bool
jamscript::c_side_scheduler::
add_real_time_task(uint32_t task_id, void* args, 
                   void (*real_time_task_fn)(task_t *, void *)) {
    if (!rt_manager.c_shared_stack->is_allocatable) return false;
    task_t* handle = rt_manager.add(task_id, args, real_time_task_fn);
    if (handle == nullptr) {
        rt_manager.c_shared_stack->is_allocatable = 0;
        return false;
    }
    return true;
}

std::vector<jamscript::task_schedule_entry>* 
jamscript::c_side_scheduler::decide() {
    if (dec.decide()) {
        return &normal_schedule;
    } else {
        return &greedy_schedule;
    }
}

void 
jamscript::c_side_scheduler::run() {
    {
        std::lock_guard<std::recursive_mutex> l(time_mutex);
        scheduler_start_time = std::chrono::high_resolution_clock::now();
        cycle_start_time = std::chrono::high_resolution_clock::now();
    }
    scheduler_mainloop(c_scheduler);
}

bool 
jamscript::c_side_scheduler::is_running() {
    return c_scheduler->cont != 0;
}

void 
jamscript::c_side_scheduler::exit() {
    c_scheduler->cont = 0;
}

uint32_t 
jamscript::c_side_scheduler::get_num_cycle_finished() {
    return multiplier;
}

void 
jamscript::c_side_scheduler::download_schedule() {
    dec.schedule_change(normal_schedule, greedy_schedule);
}

void 
jamscript::c_side_scheduler::move_scheduler_slot() {
    std::lock_guard<std::recursive_mutex> l(time_mutex);
    while (!(current_schedule->at(current_schedule_slot)
           .inside(get_current_timepoint_in_cycle()))) {
        current_schedule_slot = current_schedule_slot + 1;
        if (current_schedule_slot >= current_schedule->size()) {
            current_schedule_slot = 0;
            current_schedule = decide();
            download_schedule();
            multiplier++;
#ifdef JAMSCRIPT_SCHED_AI_EXP
            long long jacc = 0;
            std::cout << "JITTERS: ";
            for (auto& j: total_jitter) {
                std::cout << j << " ";
                jacc += j;
            }
            std::cout << "AVG: " << double(jacc) / total_jitter.size() <<
            std::endl;
#endif
            total_jitter.clear();
            cycle_start_time = std::chrono::high_resolution_clock::now();
        }
    }
}

void 
jamscript::c_side_scheduler::
register_named_execution(std::string name, void* fp) {
    std::lock_guard<std::mutex> l(named_exec_mutex);
    local_function_map[name] = fp;
}

uint64_t 
jamscript::c_side_scheduler::get_current_timepoint_in_cycle() {
    std::lock_guard<std::recursive_mutex> l(time_mutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>
           (std::chrono::high_resolution_clock::now() - cycle_start_time)
           .count();
}

uint64_t 
jamscript::c_side_scheduler::get_current_timepoint_in_scheduler() {
    std::lock_guard<std::recursive_mutex> l(time_mutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>
           (std::chrono::high_resolution_clock::now() - scheduler_start_time)
           .count();
}

uint64_t 
jamscript::c_side_scheduler::get_current_timepoint_in_task() {
    std::lock_guard<std::recursive_mutex> l(time_mutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>
           (std::chrono::high_resolution_clock::now() - task_start_time)
           .count();
}