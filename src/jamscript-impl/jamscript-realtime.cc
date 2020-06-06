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
#include "jamscript-impl/jamscript-realtime.hh"
#include "jamscript-impl/jamscript-tasktype.hh"
#include "jamscript-impl/jamscript-scheduler.hh"
#include <cstring>

jamscript::realtime_manager::
realtime_manager(c_side_scheduler* scheduler, uint32_t stack_size) : 
scheduler(scheduler){
    c_shared_stack = make_shared_stack(stack_size, malloc, free, memcpy);
    task_map[0x0] = {};
}

jamscript::realtime_manager::
~realtime_manager(){
    std::lock_guard<std::mutex> lock(m);
    for (auto& [id, tasks]: task_map) {
        while (!tasks.empty()) {
            task_t* task = tasks.back();
            delete static_cast<real_time_extender*>(
                    task->task_fv->get_user_data(task)
            );
            destroy_shared_stack_task(task);
            tasks.pop_back();
        }
    }
    destroy_shared_stack(c_shared_stack);
}

task_t* 
jamscript::realtime_manager::
add(uint32_t id, void* args, void(*func)(task_t *, void*)) {
    if (c_shared_stack->is_allocatable) {
        task_t* new_xtask = make_shared_stack_task(scheduler->c_scheduler,
                                                   func, args,
                                                   c_shared_stack);
        if (new_xtask == nullptr) throw std::bad_alloc();
        new_xtask->task_fv->set_user_data(new_xtask, 
                                          new real_time_extender(id));
        {
            std::lock_guard<std::mutex> lock(m);
            task_map[id].push_back(new_xtask);
            return new_xtask;
        }
        delete static_cast<real_time_extender*>(
            new_xtask->task_fv->get_user_data(new_xtask)
        );
        destroy_shared_stack_task(new_xtask);
        return nullptr;
    }
    return nullptr;
}

void 
jamscript::realtime_manager::remove(task_t* to_remove) {
    auto* traits2 = static_cast<real_time_extender*>(
        to_remove->task_fv->get_user_data(to_remove)
    );
    delete traits2;
    destroy_shared_stack_task(to_remove);
}

task_t* 
jamscript::realtime_manager::dispatch(uint32_t id) {
    std::lock_guard<std::mutex> lock(m);
    if (task_map.find(id) != task_map.end()) {
        auto& l = task_map[id];
        if (l.empty()) return nullptr;
        task_t* to_dispatch = l.front();
        l.pop_front();
        return to_dispatch;
    }
    return nullptr;
}

void 
jamscript::realtime_manager::spin_until_end() {
    while (scheduler->get_current_timepoint_in_cycle() < 
           scheduler->current_schedule->
           at(scheduler->current_schedule_slot).end_time * 1000) {
#if defined(JAMSCRIPT_RT_SPINWAIT_DELTA) && JAMSCRIPT_RT_SPINWAIT_DELTA > 0
        std::this_thread::sleep_for(
            std::chrono::nanoseconds(JAMSCRIPT_RT_SPINWAIT_DELTA)
        );
#endif
    }
}