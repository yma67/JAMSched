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
#include "jamscript-impl/jamscript-batch.hh"
#include "jamscript-impl/jamscript-tasktype.hh"
#include "jamscript-impl/jamscript-scheduler.hh"

jamscript::batch_manager::
batch_manager(c_side_scheduler* scheduler, uint32_t stack_size) : 
sporadic_manager(scheduler, stack_size) {

}

jamscript::batch_manager::
~batch_manager() {
    std::lock_guard<std::mutex> lock(m);
    for (auto task: batch_queue) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
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
        delete[] task->stack;
        delete static_cast<batch_extender*>(
                task->task_fv->get_user_data(task)
        );
        delete task;
    }
}

task_t*
jamscript::batch_manager::
add(uint64_t burst, void* args, void (*batch_fn)(task_t*, void*)) {
    auto* batch_task = new task_t;
    auto* batch_task_stack = new unsigned char[stack_size];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    batch_task->v_stack_id = VALGRIND_STACK_REGISTER(
        batch_task_stack, 
        (void*)((uintptr_t)batch_task_stack + stack_size)
    );
#endif
    make_task(batch_task, scheduler->c_scheduler, batch_fn,
              args, stack_size, batch_task_stack);
    batch_task->task_fv->set_user_data(batch_task, new batch_extender(burst));
    {
        std::unique_lock<std::mutex> lock(m);
        batch_queue.push_back(batch_task);
    }
    return batch_task;
}

task_t* 
jamscript::batch_manager::
add(task_t *parent, uint64_t deadline, uint64_t burst,
    void *args, void (*func)(task_t *, void *)) {
    return add(burst, args, func);
}

task_t* 
jamscript::batch_manager::dispatch() {
    std::lock_guard<std::mutex> lock(m);
    if (batch_queue.empty()) return nullptr;
    task_t* to_return = batch_queue.front();
    batch_queue.pop_front();
    return to_return;
}

const uint32_t 
jamscript::batch_manager::size() const {
    return batch_queue.size();
}

void 
jamscript::batch_manager::pause(task_t* task) {
    std::lock_guard<std::mutex> lock(m);
    batch_wait.insert(task);
}

bool 
jamscript::batch_manager::ready(task_t* task) {
    auto* cpp_task_traits2 = static_cast<interactive_extender*>(
                task->task_fv->get_user_data(task)
            );
    std::lock_guard<std::mutex> lock(m);
    if (batch_wait.find(task) != batch_wait.end()) {
        batch_wait.erase(task);
        batch_queue.push_back(task);
        return true;
    }
    return false;
}

void 
jamscript::batch_manager::enable(task_t* task) {
    std::lock_guard<std::mutex> lock(m);
    batch_queue.push_back(task);
}

void
jamscript::batch_manager::remove(task_t* to_remove) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(to_remove->v_stack_id);
#endif
    delete[] to_remove->stack;
    delete static_cast<batch_extender*>(
            to_remove->task_fv->get_user_data(to_remove)
    );
    delete to_remove;
}

void 
jamscript::batch_manager::
update_burst(task_t* task, uint64_t burst) {
    auto* traits = static_cast<batch_extender*>(
        task->task_fv->get_user_data(task)
    );
    traits->burst -= burst;
}