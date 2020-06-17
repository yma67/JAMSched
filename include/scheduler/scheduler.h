#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#include <time/time.h>

#include <boost/intrusive/unordered_set.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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
        struct ExecutionStats {
            std::vector<std::chrono::high_resolution_clock::duration> jitters;
            std::vector<ITaskEntry> iRecords;
        };
        Timer timer;
        uint32_t numberOfPeriods;
        ExecutionStats eStats;
        Duration vClockI, vClockB;
        SpinLock qMutexWithType[3];
        std::mutex sReadyRTSchedule;
        StealScheduler thief;
        Decider decider;
        std::condition_variable cvReadyRTSchedule;
        TimePoint schedulerStartTime, cycleStartTime;
        std::vector<RealTimeSchedule> rtScheduleNormal, rtScheduleGreedy;
        JAMStorageTypes::RealTimeIdMultiMapType::bucket_type bucket[200];
        JAMStorageTypes::RealTimeIdMultiMapType rtRegisterTable;
        JAMStorageTypes::InteractiveEdfPriorityQueueType iEDFPriorityQueue;
        JAMStorageTypes::InteractiveReadyStackType iCancelStack;
        JAMStorageTypes::InteractiveWaitSetType iWaitPool;
        JAMStorageTypes::BatchQueueType bQueue;
        JAMStorageTypes::BatchWaitSetType bWaitPool;
        RIBScheduler(uint32_t sharedStackSize)
            : SchedulerBase(sharedStackSize),
              timer(this),
              thief(this, sharedStackSize),
              vClockI(std::chrono::nanoseconds(0)),
              decider(this),
              vClockB(std::chrono::nanoseconds(0)),
              numberOfPeriods(0),
              rtRegisterTable(JAMStorageTypes::RealTimeIdMultiMapType::bucket_traits(bucket, 200)) {}
        ~RIBScheduler() override {
            auto dTaskInf = [](TaskInterface* t) { delete t; };
            rtRegisterTable.clear_and_dispose(dTaskInf);
            iEDFPriorityQueue.clear_and_dispose(dTaskInf);
            iCancelStack.clear_and_dispose(dTaskInf);
            iWaitPool.clear_and_dispose(dTaskInf);
            bQueue.clear_and_dispose(dTaskInf);
            bWaitPool.clear_and_dispose(dTaskInf);
        }
        const TimePoint& GetSchedulerStartTime() const { return schedulerStartTime; }
        const TimePoint& GetCycleStartTime() const { return cycleStartTime; }
        void ShutDown() {
            if (toContinue)
                toContinue = false;
            thief.ShutDown_();
        }
        void Enable(TaskInterface* toEnable) override;
        void Disable(TaskInterface* toDisable) override;
        void operator()();
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
    };
}  // namespace JAMScript

#endif