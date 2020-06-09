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
#include "jamscript/tasktype/tasktype.hh"
#include "jamscript/manager/interactive.hh"
#include "jamscript/scheduler/scheduler.hh"
#include "jamscript/worksteal/worksteal.hh"

JAMScript::InteractiveTaskManager::InteractiveTaskManager(Scheduler *scheduler, uint32_t stackSize)
    : SporadicTaskManager(scheduler, stackSize), interactiveQueue(JAMScript::edf_cmp) {}

JAMScript::InteractiveTaskManager::~InteractiveTaskManager() { ClearAllTasks(); }

void JAMScript::InteractiveTaskManager::ClearAllTasks() {
    std::lock_guard<std::mutex> lock(m);
    while (!interactiveQueue.empty()) {
        RemoveTask(interactiveQueue.top().second);
        interactiveQueue.pop();
    }
    for (auto task : interactiveWait) {
        RemoveTask(task);
    }
    for (auto task : interactiveStack) {
        RemoveTask(task);
    }
    interactiveWait.clear();
    interactiveStack.clear();
    cv.notify_all();
}

CTask *JAMScript::InteractiveTaskManager::CreateRIBTask(uint64_t burst, void *args,
                                                        void (*func)(CTask *, void *)) {
    auto *interactiveTaskPtr = new CTask;
    auto *interactiveTaskStack = new unsigned char[stackSize];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    interactiveTaskPtr->v_stack_id = VALGRIND_STACK_REGISTER(
        interactiveTaskStack, (void *)((uintptr_t)interactiveTaskStack + stackSize));
#endif
    CreateTask(interactiveTaskPtr, scheduler->cScheduler, func, args, stackSize,
               interactiveTaskStack);
    return interactiveTaskPtr;
}

CTask *JAMScript::InteractiveTaskManager::CreateRIBTask(CTask *parent, uint64_t deadline,
                                                        uint64_t burst, void *args,
                                                        void (*func)(CTask *, void *)) {
    auto interactiveTaskHandle = std::make_shared<CFuture>();
    CreateFuture(interactiveTaskHandle.get(), parent, nullptr,
                 JAMScript::InteractiveTaskHandlePostCallback);
    interactiveTaskHandle->lockWord = 0x0;
    auto *iext = new JAMScript::InteractiveTaskExtender(burst, deadline, interactiveTaskHandle);
    CTask *interactiveTaskPtr = CreateRIBTask(burst, args, func);
    interactiveTaskPtr->taskFunctionVector->SetUserData(interactiveTaskPtr, iext);
    {
        std::lock_guard<std::mutex> lock(m);
        interactiveQueue.push({deadline, interactiveTaskPtr});
    }
    scheduler->decider.RecordInteractiveJobArrival({*iext});
    return interactiveTaskPtr;
}

CTask *JAMScript::InteractiveTaskManager::DispatchTask() {
    std::lock_guard<std::mutex> lock(m);
    InteractiveTaskExtender *to_cancel_ext;
    CTask *toReturn;
    while (!interactiveQueue.empty()) {
        toReturn = interactiveQueue.top().second;
        to_cancel_ext = static_cast<InteractiveTaskExtender *>(
            toReturn->taskFunctionVector->GetUserData(toReturn));
        if ((to_cancel_ext->deadline - to_cancel_ext->burst) <=
            scheduler->GetCurrentTimepointInScheduler() / 1000) {
            interactiveStack.push_back(toReturn);
            cv.notify_one();
            interactiveQueue.pop();
        } else {
            break;
        }
    }
    if (interactiveQueue.empty()) {
        if (interactiveStack.empty()) {
            return nullptr;
        } else {
            toReturn = interactiveStack.back();
            interactiveStack.pop_back();
            toReturn->actualScheduler = scheduler->cScheduler;
            return toReturn;
        }
    } else {
        toReturn = interactiveQueue.top().second;
        interactiveQueue.pop();
        toReturn->actualScheduler = scheduler->cScheduler;
        return toReturn;
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

CTask *JAMScript::InteractiveTaskManager::StealTask(TaskStealWorker *thief) {
    std::unique_lock<std::mutex> lock(m);
    while (interactiveStack.empty() && thief->GetInteractiveCScheduler()->isSchedulerContinue)
        cv.wait(lock);
    if (!thief->GetInteractiveCScheduler()->isSchedulerContinue)
        return nullptr;
    CTask *toReturn = interactiveStack.front();
    toReturn->actualScheduler = thief->GetInteractiveCScheduler();
    interactiveStack.pop_front();
    return toReturn;
}