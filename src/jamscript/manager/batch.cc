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
#include "jamscript/manager/batch.hh"
#include "jamscript/scheduler/scheduler.hh"
#include "jamscript/tasktype/tasktype.hh"
#include "jamscript/worksteal/worksteal.hh"

JAMScript::BatchTaskManager::BatchTaskManager(Scheduler *scheduler, uint32_t stackSize)
    : SporadicTaskManager(scheduler, stackSize) {}

JAMScript::BatchTaskManager::~BatchTaskManager() { ClearAllTasks(); }

void JAMScript::BatchTaskManager::ClearAllTasks() {
    std::lock_guard<std::mutex> lock(m);
    for (auto task : batchQueue) {
        RemoveTask(task);
    }
    for (auto task : batchWait) {
        RemoveTask(task);
    }
    batchQueue.clear();
    batchWait.clear();
    cv.notify_all();
}

CTask *JAMScript::BatchTaskManager::CreateRIBTask(uint64_t burst, void *args,
                                                  void (*BatchTaskFunction)(CTask *, void *)) {
    auto *batchTask = new CTask;
    auto *batchTaskStack = new unsigned char[stackSize];
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    batchTask->v_stack_id =
        VALGRIND_STACK_REGISTER(batchTaskStack, (void *)((uintptr_t)batchTaskStack + stackSize));
#endif
    CreateTask(batchTask, scheduler->cScheduler, BatchTaskFunction, args, stackSize,
               batchTaskStack);
    batchTask->taskFunctionVector->SetUserData(batchTask, new BatchTaskExtender(burst));
    {
        std::unique_lock<std::mutex> lock(m);
        batchQueue.push_back(batchTask);
        cv.notify_one();
    }
    return batchTask;
}

CTask *JAMScript::BatchTaskManager::CreateRIBTask(CTask *parent, uint64_t deadline, uint64_t burst,
                                                  void *args, void (*func)(CTask *, void *)) {
    return CreateRIBTask(burst, args, func);
}

CTask *JAMScript::BatchTaskManager::DispatchTask() {
    std::lock_guard<std::mutex> lock(m);
    if (batchQueue.empty())
        return nullptr;
    CTask *toReturn = batchQueue.front();
    batchQueue.pop_front();
    toReturn->actualScheduler = scheduler->cScheduler;
    return toReturn;
}

const uint32_t JAMScript::BatchTaskManager::NumberOfTaskReady() const { return batchQueue.size(); }

void JAMScript::BatchTaskManager::PauseTask(CTask *task) {
    std::lock_guard<std::mutex> lock(m);
    batchWait.insert(task);
}

bool JAMScript::BatchTaskManager::SetTaskReady(CTask *task) {
    std::lock_guard<std::mutex> lock(m);
    if (batchWait.find(task) != batchWait.end()) {
        batchWait.erase(task);
        batchQueue.push_back(task);
        cv.notify_one();
        return true;
    }
    return false;
}

void JAMScript::BatchTaskManager::EnableTask(CTask *task) {
    std::lock_guard<std::mutex> lock(m);
    batchQueue.push_back(task);
    cv.notify_one();
}

void JAMScript::BatchTaskManager::RemoveTask(CTask *to_remove) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(to_remove->v_stack_id);
#endif
    delete[] to_remove->stack;
    delete static_cast<BatchTaskExtender *>(to_remove->taskFunctionVector->GetUserData(to_remove));
    delete to_remove;
}

void JAMScript::BatchTaskManager::UpdateBurstToTask(CTask *task, uint64_t burst) {
    auto *traits = static_cast<BatchTaskExtender *>(task->taskFunctionVector->GetUserData(task));
    traits->burst -= burst;
}

CTask *JAMScript::BatchTaskManager::StealTask(TaskStealWorker *thief) {
    std::unique_lock<std::mutex> lock(m);
    while (batchQueue.empty() && thief->GetBatchCScheduler()->isSchedulerContinue) cv.wait(lock);
    if (!thief->GetBatchCScheduler()->isSchedulerContinue)
        return nullptr;
    CTask *toReturn = batchQueue.back();
    toReturn->actualScheduler = thief->GetBatchCScheduler();
    batchQueue.pop_back();
    return toReturn;
}