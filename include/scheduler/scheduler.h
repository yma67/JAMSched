#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <boost/intrusive/unordered_set.hpp>

#include "time/time.h"
#include "core/task/task.h"
#include "scheduler/decider.h"
#include "scheduler/taskthief.h"

namespace JAMScript {

    struct RealTimeSchedule {
        std::chrono::high_resolution_clock::duration sTime, eTime;
        uint32_t taskId;
        RealTimeSchedule(const std::chrono::high_resolution_clock::duration& s,
                         const std::chrono::high_resolution_clock::duration& e, uint32_t id)
            : sTime(s), eTime(e), taskId(id) {}
    };

    struct StackTraits {
        bool useSharedStack;
        uint32_t stackSize;
        bool canSteal;
        StackTraits(bool ux, uint32_t ssz) : useSharedStack(ux), stackSize(ssz), canSteal(true) {}
        StackTraits(bool ux, uint32_t ssz, bool cs) : useSharedStack(ux), stackSize(ssz), canSteal(cs) {}
    };

    class RIBScheduler : public SchedulerBase {
    public:

        friend class StealScheduler;
        friend class Decider;

        friend TaskInterface* ThisTask::Active();
        friend void ThisTask::SleepFor(Duration dt);
        friend void ThisTask::SleepUntil(TimePoint tp);
        friend void ThisTask::SleepFor(Duration dt, std::unique_lock<SpinLock>& lk, Notifier* f);
        friend void ThisTask::SleepUntil(TimePoint tp, std::unique_lock<SpinLock>& lk, Notifier* f);
        friend void ThisTask::Yield();

        JAMStorageTypes::BatchQueueType bQueue;
        JAMStorageTypes::BatchWaitSetType bWaitPool;
        JAMStorageTypes::InteractiveWaitSetType iWaitPool;
        JAMStorageTypes::InteractiveReadyStackType iCancelStack;
        JAMStorageTypes::RealTimeIdMultiMapType::bucket_type bucket[200];
        JAMStorageTypes::RealTimeIdMultiMapType rtRegisterTable;
        JAMStorageTypes::InteractiveEdfPriorityQueueType iEDFPriorityQueue;
        
        void Enable(TaskInterface* toEnable) override;
        void Disable(TaskInterface* toDisable) override;

        const TimePoint& GetSchedulerStartTime() const;
        const TimePoint& GetCycleStartTime() const;
        void SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy);
        void ShutDown() override;
        void Run();
        
        template <typename Fn, typename... Args>
        TaskInterface* CreateInteractiveTask(StackTraits stackTraits, Duration deadline, Duration burst,
                                             std::function<void()> onCancel, Fn&& tf, Args... args) {
            TaskInterface* fn = nullptr;
            if (stackTraits.useSharedStack) {
                fn = new SharedCopyStackTask(this, std::move(tf), std::move(args)...);
            } else {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::move(tf), std::move(args)...);
            }
            fn->taskType = INTERACTIVE_TASK_T;
            fn->burst = burst;
            fn->deadline = deadline;
            fn->onCancel = onCancel;
            fn->isStealable = stackTraits.canSteal;
            std::lock_guard<SpinLock> lock(qMutexWithType[INTERACTIVE_TASK_T]);
            decider.RecordInteractiveJobArrival(
                {std::chrono::duration_cast<std::chrono::microseconds>(deadline).count(),
                 std::chrono::duration_cast<std::chrono::microseconds>(burst).count()});
            iEDFPriorityQueue.insert(*fn);
            return fn;
        }

        template <typename Fn, typename... Args>
        TaskInterface* CreateBatchTask(StackTraits stackTraits, Duration burst, Fn&& tf, Args... args) {
            TaskInterface* fn = nullptr;
            if (stackTraits.useSharedStack) {
                fn = new SharedCopyStackTask(this, std::move(tf), std::move(args)...);
            } else {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::move(tf), std::move(args)...);
            }
            fn->taskType = BATCH_TASK_T;
            fn->burst = burst;
            fn->isStealable = stackTraits.canSteal;
            std::lock_guard<SpinLock> lock(qMutexWithType[BATCH_TASK_T]);
            bQueue.push_back(*fn);
            return fn;
        }

        template <typename Fn, typename... Args>
        TaskInterface* CreateRealTimeTask(StackTraits stackTraits, uint32_t id, Fn&& tf, Args... args) {
            TaskInterface* fn = nullptr;
            if (stackTraits.useSharedStack) {
                fn = new SharedCopyStackTask(this, std::move(tf), std::move(args)...);
            } else {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::move(tf), std::move(args)...);
            }
            fn->taskType = REAL_TIME_TASK_T;
            fn->id = id;
            fn->isStealable = stackTraits.canSteal;
            std::lock_guard<SpinLock> lock(qMutexWithType[REAL_TIME_TASK_T]);
            rtRegisterTable.insert(*fn);
            return fn;
        }

        RIBScheduler(uint32_t sharedStackSize);
        ~RIBScheduler() override;

    protected:

        struct ExecutionStats {
            std::vector<std::chrono::high_resolution_clock::duration> jitters;
            std::vector<ITaskEntry> iRecords;
        };

        Timer timer;
        Decider decider;
        StealScheduler thief;
        ExecutionStats eStats;
        uint32_t numberOfPeriods;
        Duration vClockI, vClockB;
        SpinLock qMutexWithType[3];
        std::mutex sReadyRTSchedule;
        std::condition_variable cvReadyRTSchedule;
        TimePoint schedulerStartTime, cycleStartTime;
        std::vector<RealTimeSchedule> rtScheduleNormal, rtScheduleGreedy;

    };

}  // namespace JAMScript

#endif