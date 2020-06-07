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
#include "jamscript-impl/jamscript-interactive.hh"
#include "jamscript-impl/jamscript-tasktype.hh"
#include "jamscript-impl/jamscript-scheduler.hh"

jamscript::interactive_manager::
interactive_manager(c_side_scheduler* scheduler, uint32_t stack_size) :
sporadic_manager(scheduler, stack_size), interactive_queue(jamscript::edf_cmp) {

}

jamscript::interactive_manager::
~interactive_manager() {
    std::lock_guard<std::mutex> lock(m);
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
}

task_t*
jamscript::interactive_manager::
add(uint64_t burst, void *args, void (*func)(task_t *, void *)) {
    auto* int_task = new task_t;
    auto* int_task_stack = new unsigned char[stack_size];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        int_task->v_stack_id = VALGRIND_STACK_REGISTER(
            int_task_stack, 
            (void*)((uintptr_t)int_task_stack + stack_size)
        );
#endif
    make_task(int_task, scheduler->c_scheduler, func,
              args, stack_size, int_task_stack);
    return int_task;
}

task_t*
jamscript::interactive_manager::
add(task_t *parent, uint64_t deadline, uint64_t burst, 
    void *args, void (*func)(task_t *, void *)) {
    auto int_task_handle = std::make_shared<jamfuture_t>();
    make_future(int_task_handle.get(), parent, nullptr,
                jamscript::interactive_task_handle_post_callback);
    int_task_handle->lock_word = 0x0;
    auto* iext = 
    new jamscript::interactive_extender(burst, deadline, int_task_handle);
    task_t* int_task = add(burst, args, func);
    int_task->task_fv->
    set_user_data(int_task, iext);
    {
        std::lock_guard<std::mutex> lock(m);
        interactive_queue.push({ deadline, int_task });
    }
    scheduler->dec.record_interac({ *iext });
    return int_task;
}

task_t* 
jamscript::interactive_manager::dispatch() {
    std::lock_guard<std::mutex> lock(m);
    interactive_extender* to_cancel_ext;
    task_t* to_return;
    while (!interactive_queue.empty()) {
        to_return = interactive_queue.top().second;
        to_cancel_ext = static_cast<interactive_extender*>(
                    to_return->task_fv->get_user_data(to_return)
                );
        if ((to_cancel_ext->deadline - to_cancel_ext->burst) <= 
            scheduler->get_current_timepoint_in_scheduler() / 1000) {
            interactive_stack.push_back(to_return);
            while (interactive_stack.size() > 3) {
                task_t* to_drop = interactive_stack.front();
                auto* to_drop_ext = static_cast<interactive_extender*>(
                    to_drop->task_fv->get_user_data(to_drop)
                );
                to_drop_ext->handle->status = ack_cancelled;
                notify_future(to_drop_ext->handle.get());
                remove(to_drop);
                interactive_stack.pop_front();
            }
            interactive_queue.pop();
        } else {
            break;
        }
    }
    if (interactive_queue.empty()) {
        if (interactive_stack.empty()) {
            return nullptr;
        } else {
            to_return = interactive_stack.back();
            interactive_stack.pop_back();
            return to_return;
        }
    } else {
        to_return = interactive_queue.top().second;
        interactive_queue.pop();
        return to_return;
    }
}

const uint32_t jamscript::interactive_manager::size() const {
    return interactive_queue.size();
}

void 
jamscript::interactive_manager::pause(task_t* task) {
    std::lock_guard<std::mutex> lock(m);
    interactive_wait.insert(task);
}

bool 
jamscript::interactive_manager::ready(task_t* task) {
    auto* cpp_task_traits2 = static_cast<interactive_extender*>(
                task->task_fv->get_user_data(task)
            );
    std::lock_guard<std::mutex> lock(m);
    if (interactive_wait.find(task) != interactive_wait.end()) {
        interactive_wait.erase(task);
        interactive_queue.push({ cpp_task_traits2->deadline, task });
        return true;
    }
    return false;
}

void 
jamscript::interactive_manager::enable(task_t* task) {
    auto* cpp_task_traits2 = static_cast<interactive_extender*>(
                task->task_fv->get_user_data(task)
            );
    std::lock_guard<std::mutex> lock(m);
    interactive_queue.push({ cpp_task_traits2->deadline, task });
}

void
jamscript::interactive_manager::remove(task_t* to_remove) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(to_remove->v_stack_id);
#endif
    delete[] to_remove->stack;
    delete static_cast<interactive_extender*>(
            to_remove->task_fv->get_user_data(to_remove)
    );
    delete to_remove;
}

void 
jamscript::interactive_manager::
update_burst(task_t* task, uint64_t burst) {
    auto* traits = static_cast<interactive_extender*>(
        task->task_fv->get_user_data(task)
    );
    traits->burst -= burst;
}