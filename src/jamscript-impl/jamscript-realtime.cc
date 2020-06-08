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

#include <cstring>

#include "jamscript-impl/jamscript-scheduler.hh"
#include "jamscript-impl/jamscript-tasktype.hh"

JAMScript::RealTimeTaskManager::RealTimeTaskManager(Scheduler *scheduler, uint32_t stackSize)
    : scheduler(scheduler) {
    cSharedStack = CreateSharedStack(stackSize, malloc, free, memcpy);
    taskMap[0x0] = {};
}

JAMScript::RealTimeTaskManager::~RealTimeTaskManager() {
    std::lock_guard<std::mutex> lock(m);
    for (auto &[id, tasks] : taskMap) {
        while (!tasks.empty()) {
            CTask *task = tasks.back();
            delete static_cast<RealTimeTaskExtender *>(task->taskFunctionVector->GetUserData(task));
            DestroySharedStackTask(task);
            tasks.pop_back();
        }
    }
    DestroySharedStack(cSharedStack);
}

CTask *JAMScript::RealTimeTaskManager::CreateRIBTask(uint32_t id, void *args,
                                                     void (*func)(CTask *, void *)) {
    if (cSharedStack->isAllocatable) {
        CTask *new_xtask = CreateSharedStackTask(scheduler->cScheduler, func, args, cSharedStack);
        if (new_xtask == nullptr)
            throw std::bad_alloc();
        new_xtask->taskFunctionVector->SetUserData(new_xtask, new RealTimeTaskExtender(id));
        {
            std::lock_guard<std::mutex> lock(m);
            taskMap[id].push_back(new_xtask);
            return new_xtask;
        }
        delete static_cast<RealTimeTaskExtender *>(
            new_xtask->taskFunctionVector->GetUserData(new_xtask));
        DestroySharedStackTask(new_xtask);
        return nullptr;
    }
    return nullptr;
}

void JAMScript::RealTimeTaskManager::RemoveTask(CTask *to_remove) {
    auto *traits2 =
        static_cast<RealTimeTaskExtender *>(to_remove->taskFunctionVector->GetUserData(to_remove));
    delete traits2;
    DestroySharedStackTask(to_remove);
}

CTask *JAMScript::RealTimeTaskManager::DispatchTask(uint32_t id) {
    std::lock_guard<std::mutex> lock(m);
    if (taskMap.find(id) != taskMap.end()) {
        auto &l = taskMap[id];
        if (l.empty())
            return nullptr;
        CTask *to_dispatch = l.front();
        l.pop_front();
        return to_dispatch;
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