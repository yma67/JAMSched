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
#include "core/scheduler/task.h"

#include <stdint.h>

#define TASK_STACK_MIN 256
#ifndef NULL
#define NULL ((void*)0)
#endif

__thread CTask* currentTask;

void EmptyFuncNextIdle(CScheduler* self) {}
void EmptyFuncBeforeAfter(CTask* self) {}
void* GetUserData(CTask* t) { return t->userData; }
void SetUserData(CTask* t, void* pudata) { t->userData = pudata; }
void* GetSchedulerData(CScheduler* s) { return s->schedulerData; }
void SetSchedulerData(CScheduler* s, void* psdata) { s->schedulerData = psdata; }

static void StartTask(unsigned int taskAddressLower32Bits, unsigned int taskAddressUpper32Bits) {
    CTask* task =
        (CTask*)(taskAddressLower32Bits | (((unsigned long)taskAddressUpper32Bits << 16) << 16));
    task->TaskFunction(task, task->taskArgs);
    task->taskStatus = TASK_FINISHED;
    TaskYield(task);
}

void ResumeRegularTask(CTask* self) {
    currentTask = self;
    self->actualScheduler->taskRunning = self;
    SwapToContext(&self->actualScheduler->schedulerContext, &self->context);
}

void YieldRegularTask(CTask* task) {
    if (task == NULL)
        return;
    task->actualScheduler->taskRunning = NULL;
    currentTask = NULL;
    SwapToContext(&task->context, &task->actualScheduler->schedulerContext);
}

TaskFunctions regular_task_fv = {.TaskResume = ResumeRegularTask,
                                 .TaskYield_ = YieldRegularTask,
                                 .GetUserData = GetUserData,
                                 .SetUserData = SetUserData};

TaskReturn CreateTask(CTask* taskBytes, CScheduler* scheduler, void (*TaskFunction)(CTask*, void*),
                      void* taskArgs, unsigned int stackSize, unsigned char* stack) {
    // init task injected
    if (taskBytes == NULL || scheduler == NULL || TaskFunction == NULL) {
        return ERROR_TASK_INVALID_ARGUMENT;
    }
    taskBytes->stackSize = stackSize;
    taskBytes->TaskFunction = TaskFunction;
    taskBytes->taskArgs = taskArgs;
    taskBytes->scheduler = scheduler;
    taskBytes->actualScheduler = scheduler;
    taskBytes->stack = stack;
    taskBytes->taskFunctionVector = &regular_task_fv;
    taskBytes->context.uc_stack.ss_sp = taskBytes->stack;
    taskBytes->context.uc_stack.ss_size = taskBytes->stackSize;
    CreateContext(&taskBytes->context, (void (*)())StartTask, 2, (uint32_t)((uintptr_t)taskBytes),
                  (uint32_t)(((uintptr_t)taskBytes >> 16) >> 16));
    taskBytes->taskStatus = TASK_READY;
    return SUCCESS_TASK;
}

TaskReturn CreateScheduler(CScheduler* schedulerBytes, CTask* (*NextTask)(CScheduler* self),
                           void (*IdleTask)(CScheduler* self), void (*BeforeEach)(CTask*),
                           void (*AfterEach)(CTask*)) {
    if (schedulerBytes == NULL || NextTask == NULL || IdleTask == NULL || BeforeEach == NULL ||
        AfterEach == NULL)
        return ERROR_TASK_INVALID_ARGUMENT;
    schedulerBytes->NextTask = NextTask;
    schedulerBytes->IdleTask = IdleTask;
    schedulerBytes->BeforeEach = BeforeEach;
    schedulerBytes->AfterEach = AfterEach;
    schedulerBytes->isSchedulerContinue = 1;
    schedulerBytes->GetSchedulerData = GetSchedulerData;
    schedulerBytes->SetSchedulerData = SetSchedulerData;
    return SUCCESS_TASK;
}

TaskReturn ShutdownScheduler(CScheduler* scheduler) {
    if (scheduler == NULL)
        return ERROR_TASK_INVALID_ARGUMENT;
    scheduler->isSchedulerContinue = 0;
    return SUCCESS_TASK;
}

void SchedulerMainloop(CScheduler* scheduler) {
    if (scheduler == NULL)
        return;
    while (scheduler->isSchedulerContinue) {
        CTask* to_run = scheduler->NextTask(scheduler);
        if (to_run != NULL && to_run->taskStatus == TASK_READY) {
            scheduler->BeforeEach(to_run);
            to_run->taskFunctionVector->TaskResume(to_run);
            scheduler->AfterEach(to_run);
        } else {
            scheduler->IdleTask(scheduler);
        }
    }
}
