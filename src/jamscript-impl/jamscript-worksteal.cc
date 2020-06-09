#include "jamscript-impl/jamscript-worksteal.hh"
#include "jamscript-impl/jamscript-scheduler.hh"
#include "jamscript-impl/jamscript-sporadic.hh"
#include "jamscript-impl/jamscript-tasktype.hh"
#include "core/scheduler/task.h"
#ifdef JAMSCRIPT_SCHED_AI_EXP
#include <iostream>
#endif

JAMScript::TaskStealWorker::TaskStealWorker(
    const std::vector<std::shared_ptr<SporadicTaskManager>> &managers)
    : FutureWaitable(managers) {
    interactiveScheduler = new CScheduler;
    CreateScheduler(interactiveScheduler, NextTaskWorkStealInteractive, EmptyFuncNextIdle,
                    EmptyFuncBeforeAfter, AfterTaskWorkSteal);
    interactiveScheduler->SetSchedulerData(interactiveScheduler, this);
    batchScheduler = new CScheduler;
    CreateScheduler(batchScheduler, NextTaskWorkStealBatch, EmptyFuncNextIdle, EmptyFuncBeforeAfter,
                    AfterTaskWorkSteal);
    batchScheduler->SetSchedulerData(batchScheduler, this);
}

JAMScript::TaskStealWorker::~TaskStealWorker() {
    delete interactiveScheduler;
    delete batchScheduler;
}

void JAMScript::TaskStealWorker::Run() {
    thief.push_back(std::thread([this]() { SchedulerMainloop(interactiveScheduler); }));
    thief.push_back(std::thread([this]() { SchedulerMainloop(batchScheduler); }));
    Detach();
}

void JAMScript::TaskStealWorker::Detach() {
    for (auto &t : thief) t.detach();
}

CTask *JAMScript::TaskStealWorker::NextTaskWorkStealBatch(CScheduler *scheduler) {
    auto *self = static_cast<TaskStealWorker *>(scheduler->GetSchedulerData(scheduler));
    return self->sporadicManagers[BATCH_TASK_T]->StealTask(self);
}

CTask *JAMScript::TaskStealWorker::NextTaskWorkStealInteractive(CScheduler *scheduler) {
    auto *self = static_cast<TaskStealWorker *>(scheduler->GetSchedulerData(scheduler));
    return self->sporadicManagers[INTERACTIVE_TASK_T]->StealTask(self);
}

void JAMScript::TaskStealWorker::AfterTaskWorkSteal(CTask *self) {
    auto *traits = static_cast<CTaskExtender *>(self->taskFunctionVector->GetUserData(self));
    auto *stealerPointer =
        static_cast<TaskStealWorker *>(self->scheduler->GetSchedulerData(self->scheduler));
    if (self->taskStatus == TASK_FINISHED) {
        stealerPointer->sporadicManagers[traits->taskType]->RemoveTask(self);
        return;
    }
    {
        std::lock_guard<std::mutex> l(stealerPointer->futureMutex);
        TaskStatus st = __atomic_load_n(&(self->taskStatus), __ATOMIC_ACQUIRE);
        if (st == TASK_READY) {
            stealerPointer->sporadicManagers[traits->taskType]->EnableTask(self);
        } else if (st == TASK_PENDING) {
            stealerPointer->sporadicManagers[traits->taskType]->PauseTask(self);
        }
    }
}