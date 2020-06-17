#include "core/task/task.h"

#include "concurrency/notifier.h"
#include "scheduler/scheduler.h"
#include "scheduler/tasklocal.h"

namespace JAMScript {
    namespace ThisTask {
        thread_local TaskInterface* thisTask = nullptr;
        TaskInterface* Active() { return thisTask; }
        void SleepFor(Duration dt) {
            dynamic_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutFor(thisTask, dt);
        }
        void SleepUntil(TimePoint tp) {
            dynamic_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutUntil(thisTask, tp);
        }
        void SleepFor(Duration dt, std::unique_lock<SpinLock>& lk, Notifier* f) {
            dynamic_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutFor(thisTask, dt, lk, f);
        }
        void SleepUntil(TimePoint tp, std::unique_lock<SpinLock>& lk, Notifier* f) {
            dynamic_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutUntil(thisTask, tp, lk, f);
        }
        void Yield() {
            thisTask->scheduler->Enable(thisTask);
            thisTask->SwapOut();
        }
    }  // namespace ThisTask
    TaskInterface::TaskInterface(SchedulerBase* scheduler)
        : status(TASK_READY),
          isStealable(true),
          scheduler(scheduler),
          baseScheduler(scheduler),
          references(0),
          notifier(ThisTask::Active()),
          id(0),
          taskLocalStoragePool(*GetGlobalJTLSMap()),
          deadline(std::chrono::microseconds(0)),
          burst(std::chrono::microseconds(0)) {}
    void TaskInterface::ExecuteC(uint32_t tsLower, uint32_t tsHigher) {
        TaskInterface* task =
            reinterpret_cast<TaskInterface*>(tsLower | ((static_cast<uint64_t>(tsHigher) << 16) << 16));
        task->Execute();
        task->status = TASK_FINISHED;
        task->notifier.Notify();
        task->SwapOut();
    }
    TaskInterface::~TaskInterface() {}
    void TaskInterface::Join() { notifier.Join(); }
    void TaskInterface::Detach() { notifier.ownerTask = nullptr; }
}  // namespace JAMScript
