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

JAMScript::RealTimeTaskManager::RealTimeTaskManager(Scheduler *scheduler, uint32_t stackSize)
    : scheduler(scheduler) {
    cSharedStack = CreateSharedStack(stackSize, malloc, free, memcpy);
    taskMap[0x0] = {};
}

JAMScript::RealTimeTaskManager::~RealTimeTaskManager() {
    std::lock_guard<std::mutex> lock(m);
    for (auto &[id, tasks] : taskMap) {
        while (!tasks.empty()) {
            RemoveTask(tasks.back());
            tasks.pop_back();
        }
    }
    DestroySharedStack(cSharedStack);
}

CTask *JAMScript::RealTimeTaskManager::CreateRIBTask(uint32_t id, void *args,
                                                     void (*func)(CTask *, void *)) {
    if (cSharedStack->isAllocatable) {
        CTask *sharedTask = CreateSharedStackTask(scheduler->cScheduler, func, args, cSharedStack);
        if (sharedTask == nullptr)
            throw std::bad_alloc();
        sharedTask->taskFunctionVector->SetUserData(sharedTask, new RealTimeTaskExtender(id));
        {
            std::lock_guard<std::mutex> lock(m);
            taskMap[id].push_back(sharedTask);
            return sharedTask;
        }
        delete static_cast<RealTimeTaskExtender *>(
            sharedTask->taskFunctionVector->GetUserData(sharedTask));
        DestroySharedStackTask(sharedTask);
        return nullptr;
    }
    return nullptr;
}

void JAMScript::RealTimeTaskManager::RemoveTask(CTask *to_remove) {
    delete static_cast<RealTimeTaskExtender *>(
        to_remove->taskFunctionVector->GetUserData(to_remove));
    DestroySharedStackTask(to_remove);
}

CTask *JAMScript::RealTimeTaskManager::DispatchTask(uint32_t id) {
    std::lock_guard<std::mutex> lock(m);
    if (taskMap.find(id) != taskMap.end()) {
        auto &l = taskMap[id];
        if (l.empty())
            return nullptr;
        CTask *toDispatch = l.front();
        l.pop_front();
        return toDispatch;
    }
    return nullptr;
}

void JAMScript::RealTimeTaskManager::SpinUntilEndOfCurrentInterval() {
    while (scheduler->GetCurrentTimepointInCycle() <
           scheduler->currentSchedule->at(scheduler->currentScheduleSlot).endTime * 1000) {
#if defined(JAMSCRIPT_RT_SPINWAIT_DELTA) && JAMSCRIPT_RT_SPINWAIT_DELTA > 0
        std::this_thread::sleep_for(std::chrono::nanoseconds(JAMSCRIPT_RT_SPINWAIT_DELTA));
#endif
    }
}