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

#include "jamscript-impl/jamscript-scheduler.hh"
#include "jamscript-impl/jamscript-tasktype.hh"

JAMScript::InteractiveTaskManager::InteractiveTaskManager(Scheduler *scheduler, uint32_t stackSize)
    : SporadicTaskManager(scheduler, stackSize), interactiveQueue(JAMScript::edf_cmp) {}

JAMScript::InteractiveTaskManager::~InteractiveTaskManager() {
    std::lock_guard<std::mutex> lock(m);
    while (!interactiveQueue.empty()) {
        auto [ddl, task] = interactiveQueue.top();
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        delete[] task->stack;
        delete static_cast<InteractiveTaskExtender *>(task->taskFunctionVector->GetUserData(task));
        delete task;
        interactiveQueue.pop();
    }
    for (auto task : interactiveWait) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        delete[] task->stack;
        delete static_cast<InteractiveTaskExtender *>(task->taskFunctionVector->GetUserData(task));
        delete task;
    }
    for (auto task : interactiveTask) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        VALGRIND_STACK_DEREGISTER(task->v_stack_id);
#endif
        delete[] task->stack;
        delete static_cast<InteractiveTaskExtender *>(task->taskFunctionVector->GetUserData(task));
        delete task;
    }
}

CTask *JAMScript::InteractiveTaskManager::CreateRIBTask(uint64_t burst, void *args,
                                                        void (*func)(CTask *, void *)) {
    auto *int_task = new CTask;
    auto *int_task_stack = new unsigned char[stackSize];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    int_task->v_stack_id =
        VALGRIND_STACK_REGISTER(int_task_stack, (void *)((uintptr_t)int_task_stack + stackSize));
#endif
    CreateTask(int_task, scheduler->cScheduler, func, args, stackSize, int_task_stack);
    return int_task;
}

CTask *JAMScript::InteractiveTaskManager::CreateRIBTask(CTask *parent, uint64_t deadline,
                                                        uint64_t burst, void *args,
                                                        void (*func)(CTask *, void *)) {
    auto int_task_handle = std::make_shared<CFuture>();
    CreateFuture(int_task_handle.get(), parent, nullptr,
                 JAMScript::InteractiveTaskHandlePostCallback);
    int_task_handle->lockWord = 0x0;
    auto *iext = new JAMScript::InteractiveTaskExtender(burst, deadline, int_task_handle);
    CTask *int_task = CreateRIBTask(burst, args, func);
    int_task->taskFunctionVector->SetUserData(int_task, iext);
    {
        std::lock_guard<std::mutex> lock(m);
        interactiveQueue.push({deadline, int_task});
    }
    scheduler->decider.RecordInteractiveJobArrival({*iext});
    return int_task;
}

CTask *JAMScript::InteractiveTaskManager::DispatchTask() {
    std::lock_guard<std::mutex> lock(m);
    InteractiveTaskExtender *to_cancel_ext;
    CTask *to_return;
    while (!interactiveQueue.empty()) {
        to_return = interactiveQueue.top().second;
        to_cancel_ext = static_cast<InteractiveTaskExtender *>(
            to_return->taskFunctionVector->GetUserData(to_return));
        if ((to_cancel_ext->deadline - to_cancel_ext->burst) <=
            scheduler->GetCurrentTimepointInScheduler() / 1000) {
            interactiveTask.push_back(to_return);
            while (interactiveTask.size() > 3) {
                CTask *to_drop = interactiveTask.front();
                auto *to_drop_ext = static_cast<InteractiveTaskExtender *>(
                    to_drop->taskFunctionVector->GetUserData(to_drop));
                to_drop_ext->handle->status = ACK_CANCELLED;
                NotifyFinishOfFuture(to_drop_ext->handle.get());
                RemoveTask(to_drop);
                interactiveTask.pop_front();
            }
            interactiveQueue.pop();
        } else {
            break;
        }
    }
    if (interactiveQueue.empty()) {
        if (interactiveTask.empty()) {
            return nullptr;
        } else {
            to_return = interactiveTask.back();
            interactiveTask.pop_back();
            return to_return;
        }
    } else {
        to_return = interactiveQueue.top().second;
        interactiveQueue.pop();
        return to_return;
    }
}

const uint32_t JAMScript::InteractiveTaskManager::NumberOfTaskReady() const {
    return interactiveQueue.size();
}

void JAMScript::InteractiveTaskManager::PauseTask(CTask *task) {
    std::lock_guard<std::mutex> lock(m);
    interactiveWait.insert(task);
}

bool JAMScript::InteractiveTaskManager::SetTaskReady(CTask *task) {
    auto *taskTraits2 =
        static_cast<InteractiveTaskExtender *>(task->taskFunctionVector->GetUserData(task));
    std::lock_guard<std::mutex> lock(m);
    if (interactiveWait.find(task) != interactiveWait.end()) {
        interactiveWait.erase(task);
        interactiveQueue.push({taskTraits2->deadline, task});
        return true;
    }
    return false;
}

void JAMScript::InteractiveTaskManager::EnableTask(CTask *task) {
    auto *taskTraits2 =
        static_cast<InteractiveTaskExtender *>(task->taskFunctionVector->GetUserData(task));
    std::lock_guard<std::mutex> lock(m);
    interactiveQueue.push({taskTraits2->deadline, task});
}

void JAMScript::InteractiveTaskManager::RemoveTask(CTask *to_remove) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(to_remove->v_stack_id);
#endif
    delete[] to_remove->stack;
    delete static_cast<InteractiveTaskExtender *>(
        to_remove->taskFunctionVector->GetUserData(to_remove));
    delete to_remove;
}

void JAMScript::InteractiveTaskManager::UpdateBurstToTask(CTask *task, uint64_t burst) {
    auto *traits =
        static_cast<InteractiveTaskExtender *>(task->taskFunctionVector->GetUserData(task));
    traits->burst -= burst;
}