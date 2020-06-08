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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#ifdef JAMSCRIPT_SCHED_AI_EXP
#include <iostream>
#endif
#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/helgrind.h>
#include <valgrind/valgrind.h>
#endif
#include "jamscript-impl/jamscript-future.hh"
#include "jamscript-impl/jamscript-scheduler.hh"
#include "jamscript-impl/jamscript-time.hh"
JAMScript::Scheduler::Scheduler(std::vector<RealTimeTaskScheduleEntry> normalSchedule,
                                std::vector<RealTimeTaskScheduleEntry> greedySchedule,
                                uint32_t deviceId, uint32_t stackSize, void *localAppArgs,
                                void (*localAppFunction)(CTask *, void *))
    : currentScheduleSlot(0),
      multiplier(0),
      deviceId(deviceId),
      virtualClock{0L, 0L},
      realTimeTaskManager(this, stackSize),
      taskStartTime(std::chrono::high_resolution_clock::now()),
      jamscriptTimer(this),
      schedulerStartTime(taskStartTime),
      cycleStartTime(taskStartTime),
      normalSchedule(std::move(normalSchedule)),
      greedySchedule(std::move(greedySchedule)),
      sporadicManagers{std::make_unique<InteractiveTaskManager>(this, stackSize),
                       std::make_unique<BatchTaskManager>(this, stackSize)},
      decider(this) {
    cScheduler = new CScheduler;
    CreateScheduler(cScheduler, NextTaskJAMScriptImpl, IdleTaskJAMScriptImpl,
                    BeforeEachJAMScriptImpl, AfterEachJAMScriptImpl);
    cScheduler->SetSchedulerData(cScheduler, this);
    if (sporadicManagers[BATCH_TASK_T]->CreateRIBTask(std::numeric_limits<uint64_t>::max(),
                                                      localAppArgs, localAppFunction) == nullptr) {
        throw std::bad_alloc();
    }
    DownloadSchedule();
    currentSchedule = DecideNextScheduleToRun();
}

JAMScript::Scheduler::~Scheduler() { delete cScheduler; }

void JAMScript::BeforeEachJAMScriptImpl(CTask *self) {
    auto *self_task = static_cast<CTaskExtender *>(self->taskFunctionVector->GetUserData(self));
    auto *self_sched = static_cast<Scheduler *>(self->scheduler->GetSchedulerData(self->scheduler));
    self_sched->taskStartTime = std::chrono::high_resolution_clock::now();
    if (self_task->taskType == JAMScript::REAL_TIME_TASK_T) {
        self_sched->totalJitter.push_back(
            std::abs((long long)self_sched->GetCurrentTimepointInCycle() / 1000 -
                     (long long)(self_sched->currentSchedule->at(self_sched->currentScheduleSlot)
                                     .startTime)));
    }
}

void JAMScript::AfterEachJAMScriptImpl(CTask *self) {
    auto *traits = static_cast<CTaskExtender *>(self->taskFunctionVector->GetUserData(self));
    auto *scheduler_ptr =
        static_cast<Scheduler *>(self->scheduler->GetSchedulerData(self->scheduler));
    auto actual_exec_time = scheduler_ptr->GetCurrentTimepointInTask();
    if (traits->taskType != JAMScript::REAL_TIME_TASK_T) {
        scheduler_ptr->virtualClock[traits->taskType] += actual_exec_time / 1000;
        scheduler_ptr->sporadicManagers[traits->taskType]->UpdateBurstToTask(
            self, actual_exec_time / 1000);
        {
            std::lock_guard<std::mutex> l(scheduler_ptr->futureMutex);
            TaskStatus st = __atomic_load_n(&(self->taskStatus), __ATOMIC_ACQUIRE);
            if (st == TASK_READY) {
                scheduler_ptr->sporadicManagers[traits->taskType]->EnableTask(self);
            } else if (st == TASK_PENDING) {
                scheduler_ptr->sporadicManagers[traits->taskType]->PauseTask(self);
            }
        }
    } else {
        scheduler_ptr->realTimeTaskManager.SpinUntilEndOfCurrentInterval();
    }
    if (self->taskStatus == TASK_FINISHED) {
        if (self->returnValue == ERROR_TASK_STACK_OVERFLOW) {
            scheduler_ptr->realTimeTaskManager.cSharedStack->isAllocatable = 0;
        }
        if (traits->taskType == JAMScript::REAL_TIME_TASK_T) {
            scheduler_ptr->realTimeTaskManager.RemoveTask(self);
        } else {
            scheduler_ptr->sporadicManagers[traits->taskType]->RemoveTask(self);
        }
    }
}

CTask *JAMScript::NextTaskJAMScriptImpl(CScheduler *self_c) {
    auto *self = static_cast<Scheduler *>(self_c->GetSchedulerData(self_c));
    self->MoveSchedulerSlot();
    self->jamscriptTimer.NotifyAllTimeouts();
    CTask *to_dispatch = self->realTimeTaskManager.DispatchTask(
        self->currentSchedule->at(self->currentScheduleSlot).taskId);
    if (to_dispatch == nullptr) {
        if (self->sporadicManagers[INTERACTIVE_TASK_T]->NumberOfTaskReady() == 0 &&
            self->sporadicManagers[BATCH_TASK_T]->NumberOfTaskReady() == 0) {
            return nullptr;
        } else if (self->sporadicManagers[INTERACTIVE_TASK_T]->NumberOfTaskReady() == 0 ||
                   self->sporadicManagers[BATCH_TASK_T]->NumberOfTaskReady() == 0) {
            return self
                ->sporadicManagers[!(
                    self->sporadicManagers[INTERACTIVE_TASK_T]->NumberOfTaskReady() >
                    self->sporadicManagers[BATCH_TASK_T]->NumberOfTaskReady())]
                ->DispatchTask();
        } else {
            return self
                ->sporadicManagers[!(self->virtualClock[INTERACTIVE_TASK_T] <
                                     self->virtualClock[BATCH_TASK_T])]
                ->DispatchTask();
        }
    }
    return to_dispatch;
}

void JAMScript::IdleTaskJAMScriptImpl(CScheduler *) {}

void JAMScript::InteractiveTaskHandlePostCallback(CFuture *self) {
    auto *cpp_task_traits = static_cast<CTaskExtender *>(
        self->ownerTask->taskFunctionVector->GetUserData(self->ownerTask));
    auto *cpp_scheduler = static_cast<Scheduler *>(
        self->ownerTask->scheduler->GetSchedulerData(self->ownerTask->scheduler));
    std::lock_guard<std::mutex> lock(cpp_scheduler->futureMutex);
    cpp_scheduler->sporadicManagers[cpp_task_traits->taskType]->SetTaskReady(self->ownerTask);
}

std::shared_ptr<CFuture> JAMScript::Scheduler::CreateInteractiveTask(
    uint64_t deadline, uint64_t burst, void *interactive_task_args,
    void (*interactive_task_fn)(CTask *, void *)) {
    if (!realTimeTaskManager.cSharedStack->isAllocatable)
        return nullptr;
    CTask *handle = sporadicManagers[INTERACTIVE_TASK_T]->CreateRIBTask(
        ThisTask(), deadline, burst, interactive_task_args, interactive_task_fn);
    if (handle == nullptr) {
        realTimeTaskManager.cSharedStack->isAllocatable = 0;
        return nullptr;
    }
    return static_cast<InteractiveTaskExtender *>(handle->taskFunctionVector->GetUserData(handle))
        ->handle;
}

bool JAMScript::Scheduler::CreateBatchTask(uint32_t burst, void *args,
                                           void (*BatchTaskFunction)(CTask *, void *)) {
    if (!realTimeTaskManager.cSharedStack->isAllocatable)
        return false;
    CTask *handle = sporadicManagers[BATCH_TASK_T]->CreateRIBTask(burst, args, BatchTaskFunction);
    if (handle == nullptr) {
        realTimeTaskManager.cSharedStack->isAllocatable = 0;
        return false;
    }
    return true;
}

bool JAMScript::Scheduler::CreateRealTimeTask(uint32_t taskId, void *args,
                                              void (*real_time_task_fn)(CTask *, void *)) {
    if (!realTimeTaskManager.cSharedStack->isAllocatable)
        return false;
    CTask *handle = realTimeTaskManager.CreateRIBTask(taskId, args, real_time_task_fn);
    if (handle == nullptr) {
        realTimeTaskManager.cSharedStack->isAllocatable = 0;
        return false;
    }
    return true;
}

std::vector<JAMScript::RealTimeTaskScheduleEntry> *JAMScript::Scheduler::DecideNextScheduleToRun() {
    if (decider.DecideNextScheduleToRun()) {
        return &normalSchedule;
    } else {
        return &greedySchedule;
    }
}

void JAMScript::Scheduler::Run() {
    {
        std::lock_guard<std::recursive_mutex> l(timeMutex);
        schedulerStartTime = std::chrono::high_resolution_clock::now();
        cycleStartTime = std::chrono::high_resolution_clock::now();
        jamscriptTimer.ZeroTimeout();
    }
    SchedulerMainloop(cScheduler);
}

bool JAMScript::Scheduler::IsSchedulerRunning() { return cScheduler->isSchedulerContinue != 0; }

void JAMScript::Scheduler::Exit() { cScheduler->isSchedulerContinue = 0; }

uint32_t JAMScript::Scheduler::GetNumberOfCycleFinished() { return multiplier; }

void JAMScript::Scheduler::DownloadSchedule() {
    decider.NotifyChangeOfSchedule(normalSchedule, greedySchedule);
}

void JAMScript::Scheduler::MoveSchedulerSlot() {
    std::lock_guard<std::recursive_mutex> l(timeMutex);
    while (!(currentSchedule->at(currentScheduleSlot).inside(GetCurrentTimepointInCycle()))) {
        currentScheduleSlot = currentScheduleSlot + 1;
        if (currentScheduleSlot >= currentSchedule->size()) {
            currentScheduleSlot = 0;
            currentSchedule = DecideNextScheduleToRun();
            DownloadSchedule();
            multiplier++;
#ifdef JAMSCRIPT_SCHED_AI_EXP
            long long jacc = 0;
            std::cout << "JITTERS: ";
            for (auto &j : totalJitter) {
                std::cout << j << " ";
                jacc += j;
            }
            std::cout << "AVG: " << double(jacc) / totalJitter.size() << std::endl;
#endif
            totalJitter.clear();
            cycleStartTime = std::chrono::high_resolution_clock::now();
        }
    }
}

void JAMScript::Scheduler::RegisterNamedExecution(std::string name, void *fp) {
    std::lock_guard<std::mutex> l(namedExecMutex);
    local_function_map[name] = fp;
}

uint64_t JAMScript::Scheduler::GetCurrentTimepointInCycle() {
    std::lock_guard<std::recursive_mutex> l(timeMutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - cycleStartTime)
        .count();
}

uint64_t JAMScript::Scheduler::GetCurrentTimepointInScheduler() {
    std::lock_guard<std::recursive_mutex> l(timeMutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - schedulerStartTime)
        .count();
}

uint64_t JAMScript::Scheduler::GetCurrentTimepointInTask() {
    std::lock_guard<std::recursive_mutex> l(timeMutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - taskStartTime)
        .count();
}