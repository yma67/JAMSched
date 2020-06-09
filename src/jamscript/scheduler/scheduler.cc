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
#include <thread>
#include "jamscript/future/future.hh"
#include "jamscript/scheduler/scheduler.hh"
#include "jamscript/time/time.hh"
#include "jamscript/worksteal/worksteal.hh"

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
      FutureWaitable({std::make_shared<InteractiveTaskManager>(this, stackSize),
                      std::make_shared<BatchTaskManager>(this, stackSize)}),
      stealer(this->sporadicManagers),
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
    auto *selfTask = static_cast<CTaskExtender *>(self->taskFunctionVector->GetUserData(self));
    auto *selfScheduler =
        static_cast<Scheduler *>(self->scheduler->GetSchedulerData(self->scheduler));
    selfScheduler->taskStartTime = std::chrono::high_resolution_clock::now();
    if (selfTask->taskType == JAMScript::REAL_TIME_TASK_T) {
        selfScheduler->totalJitter.push_back(std::abs(
            (long long)selfScheduler->GetCurrentTimepointInCycle() / 1000 -
            (long long)(selfScheduler->currentSchedule->at(selfScheduler->currentScheduleSlot)
                            .startTime)));
    }
}

void JAMScript::AfterEachJAMScriptImpl(CTask *self) {
    auto *traits = static_cast<CTaskExtender *>(self->taskFunctionVector->GetUserData(self));
    auto *schedulerPointer =
        static_cast<Scheduler *>(self->scheduler->GetSchedulerData(self->scheduler));
    auto actualExecutionTime = schedulerPointer->GetCurrentTimepointInTask();
    if (self->taskStatus == TASK_FINISHED) {
        if (self->returnValue == ERROR_TASK_STACK_OVERFLOW) {
            schedulerPointer->realTimeTaskManager.cSharedStack->isAllocatable = 0;
        }
        if (traits->taskType == JAMScript::REAL_TIME_TASK_T) {
            schedulerPointer->realTimeTaskManager.RemoveTask(self);
        } else {
            schedulerPointer->sporadicManagers[traits->taskType]->RemoveTask(self);
        }
        return;
    }
    if (traits->taskType != JAMScript::REAL_TIME_TASK_T) {
        schedulerPointer->virtualClock[traits->taskType] += actualExecutionTime / 1000;
        schedulerPointer->sporadicManagers[traits->taskType]->UpdateBurstToTask(
            self, actualExecutionTime / 1000);
        {
            std::lock_guard<std::mutex> l(schedulerPointer->futureMutex);
            TaskStatus st = __atomic_load_n(&(self->taskStatus), __ATOMIC_ACQUIRE);
            if (st == TASK_READY) {
                schedulerPointer->sporadicManagers[traits->taskType]->EnableTask(self);
            } else if (st == TASK_PENDING) {
                schedulerPointer->sporadicManagers[traits->taskType]->PauseTask(self);
            }
        }
    }
}

CTask *JAMScript::NextTaskJAMScriptImpl(CScheduler *selfCScheduler) {
    auto *self = static_cast<Scheduler *>(selfCScheduler->GetSchedulerData(selfCScheduler));
    self->MoveSchedulerSlot();
    self->jamscriptTimer.NotifyAllTimeouts();
    CTask *toDispatch = self->realTimeTaskManager.DispatchTask(
        self->currentSchedule->at(self->currentScheduleSlot).taskId);
    if (self->currentSchedule->at(self->currentScheduleSlot).taskId == 0x0 &&
        toDispatch == nullptr) {
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
    return toDispatch;
}

void JAMScript::IdleTaskJAMScriptImpl(CScheduler *selfCScheduler) {
    auto *self = static_cast<Scheduler *>(selfCScheduler->GetSchedulerData(selfCScheduler));
    uint64_t timeDiff = self->currentSchedule->at(self->currentScheduleSlot).endTime * 1000 -
                        self->GetCurrentTimepointInCycle();
    uint32_t id = self->currentSchedule->at(self->currentScheduleSlot).taskId;
    if (id != 0 && timeDiff > JAMSCRIPT_RT_SPIN_LIMIT_NS) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds(timeDiff - JAMSCRIPT_RT_SPIN_LIMIT_NS));
    } else if (id != 0) {
        self->realTimeTaskManager.SpinUntilEndOfCurrentInterval();
    }
}

void JAMScript::InteractiveTaskHandlePostCallback(CFuture *self) {
    auto *taskExtender = static_cast<CTaskExtender *>(
        self->ownerTask->taskFunctionVector->GetUserData(self->ownerTask));
    auto *futureWaitable = static_cast<FutureWaitable *>(
        self->ownerTask->scheduler->GetSchedulerData(self->ownerTask->scheduler));
    if (self->ownerTask->scheduler->taskRunning == self->ownerTask)
        std::lock_guard<std::mutex> lock(futureWaitable->futureMutex);
    futureWaitable->sporadicManagers[taskExtender->taskType]->SetTaskReady(self->ownerTask);
}

std::shared_ptr<CFuture> JAMScript::Scheduler::CreateInteractiveTask(
    uint64_t deadline, uint64_t burst, void *interactiveTaskArgs,
    void (*InteractiveTaskFunction)(CTask *, void *)) {
    if (!realTimeTaskManager.cSharedStack->isAllocatable)
        return nullptr;
    CTask *handle = sporadicManagers[INTERACTIVE_TASK_T]->CreateRIBTask(
        GetCurrentTaskRunning(), deadline, burst, interactiveTaskArgs, InteractiveTaskFunction);
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
                                              void (*RealTimeTaskFunction)(CTask *, void *)) {
    if (!realTimeTaskManager.cSharedStack->isAllocatable)
        return false;
    CTask *handle = realTimeTaskManager.CreateRIBTask(taskId, args, RealTimeTaskFunction);
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
        std::lock_guard<std::mutex> l(timeMutex);
        schedulerStartTime = std::chrono::high_resolution_clock::now();
        cycleStartTime = std::chrono::high_resolution_clock::now();
        jamscriptTimer.ZeroTimeout();
    }
#if defined(JAMSCRIPT_ENABLE_WORKSTEAL) && JAMSCRIPT_ENABLE_WORKSTEAL == 1
    stealer.Run();
#endif
    SchedulerMainloop(cScheduler);
}

bool JAMScript::Scheduler::IsSchedulerRunning() { return cScheduler->isSchedulerContinue != 0; }

void JAMScript::Scheduler::Exit() {
    if (GetCurrentTaskRunning()->scheduler != cScheduler)
        return;
    this->cScheduler->isSchedulerContinue = 0;
    stealer.interactiveScheduler->isSchedulerContinue = 0;
    stealer.batchScheduler->isSchedulerContinue = 0;
    for (auto &x : {INTERACTIVE_TASK_T, BATCH_TASK_T}) {
        sporadicManagers[x]->ClearAllTasks();
    }
}

uint32_t JAMScript::Scheduler::GetNumberOfCycleFinished() { return multiplier; }

void JAMScript::Scheduler::DownloadSchedule() {
    decider.NotifyChangeOfSchedule(normalSchedule, greedySchedule);
}

void JAMScript::Scheduler::MoveSchedulerSlot() {
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
    std::lock_guard<std::mutex> l(timeMutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - cycleStartTime)
        .count();
}

uint64_t JAMScript::Scheduler::GetCurrentTimepointInScheduler() {
    std::lock_guard<std::mutex> l(timeMutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - schedulerStartTime)
        .count();
}

uint64_t JAMScript::Scheduler::GetCurrentTimepointInTask() {
    std::lock_guard<std::mutex> l(timeMutex);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - taskStartTime)
        .count();
}