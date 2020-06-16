#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#include <boost/intrusive/unordered_set.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <time/time.hh>
#include <vector>

#include "core/task/task.hh"

namespace JAMScript {
    namespace JAMStorageTypes {
        // Ready Queue
        // Interactive Priority Queue
        typedef boost::intrusive::treap_multiset<
            TaskInterface,
            boost::intrusive::member_hook<TaskInterface, JAMHookTypes::ReadyInteractiveEdfHook,
                                          &TaskInterface::riEdfHook>,
            boost::intrusive::priority<EdfPriority> >
            InteractiveEdfPriorityQueueType;
        // Real Time Task Map
        typedef boost::intrusive::unordered_multiset<
            TaskInterface,
            boost::intrusive::member_hook<TaskInterface,
                                          boost::intrusive::unordered_set_member_hook<>,
                                          &TaskInterface::rtHook>,
            boost::intrusive::key_of_value<RealTimeIdKeyType> >
            RealTimeIdMultiMapType;
        // Batch Queue
        typedef boost::intrusive::list<
            TaskInterface,
            boost::intrusive::member_hook<TaskInterface, JAMHookTypes::ReadyBatchQueueHook,
                                          &TaskInterface::rbQueueHook> >
            BatchQueueType;
        // Wait Queue
        // Interactive Wait Set
        typedef boost::intrusive::set<
            TaskInterface,
            boost::intrusive::member_hook<TaskInterface, JAMHookTypes::WaitInteractiveHook,
                                          &TaskInterface::wiHook>,
            boost::intrusive::key_of_value<BIIdKeyType> >
            InteractiveWaitSetType;
        // Batch Wait Set
        typedef boost::intrusive::set<
            TaskInterface,
            boost::intrusive::member_hook<TaskInterface, JAMHookTypes::WaitBatchHook,
                                          &TaskInterface::wbHook>,
            boost::intrusive::key_of_value<BIIdKeyType> >
            BatchWaitSetType;
        typedef boost::intrusive::slist<
            TaskInterface,
            boost::intrusive::member_hook<TaskInterface, JAMHookTypes::ReadyInteractiveStackHook,
                                          &TaskInterface::riStackHook> >
            InteractiveReadyStackType;
    }  // namespace JAMStorageTypes
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
    };
    class RIBScheduler : public SchedulerBase {
    public:
        struct ExecutionStats {
            std::vector<std::chrono::high_resolution_clock::duration> jitters;
        };
        ExecutionStats eStats;
        std::mutex sReadyRTSchedule;
        std::condition_variable cvReadyRTSchedule;
        std::vector<RealTimeSchedule> rtScheduleNormal, rtScheduleGreedy;
        Duration vClockI, vClockB;
        TimePoint schedulerStartTime, cycleStartTime;
        Timer timer;
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
              vClockI(std::chrono::nanoseconds(0)),
              vClockB(std::chrono::nanoseconds(0)),
              rtRegisterTable(JAMStorageTypes::RealTimeIdMultiMapType::bucket_traits(bucket, 200)) {
        }
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
        void Enable(TaskInterface* toEnable) override;
        void Disable(TaskInterface* toDisable) override;
        void operator()();
        template <typename Fn, typename... Args>
        TaskInterface* CreateInteractiveTask(StackTraits stackTraits, Duration deadline,
                                             Duration burst, Fn&& tf, Args... args) {
            TaskInterface* fn = nullptr;
            if (stackTraits.useSharedStack) {
                fn = new SharedCopyStackTask(this, std::move(tf), std::move(args)...);
            } else {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::move(tf),
                                             std::move(args)...);
            }
            fn->taskType = INTERACTIVE_TASK_T;
            fn->burst = burst;
            fn->deadline = deadline;
            std::lock_guard<SpinLock> lock(qMutexWithType[INTERACTIVE_TASK_T]);
            iEDFPriorityQueue.insert(*fn);
            return fn;
        }
        template <typename Fn, typename... Args>
        TaskInterface* CreateBatchTask(StackTraits stackTraits, Duration burst, Fn&& tf,
                                       Args... args) {
            TaskInterface* fn = nullptr;
            if (stackTraits.useSharedStack) {
                fn = new SharedCopyStackTask(this, std::move(tf), std::move(args)...);
            } else {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::move(tf),
                                             std::move(args)...);
            }
            fn->taskType = BATCH_TASK_T;
            fn->burst = burst;
            std::lock_guard<SpinLock> lock(qMutexWithType[BATCH_TASK_T]);
            bQueue.push_back(*fn);
            return fn;
        }
        template <typename Fn, typename... Args>
        TaskInterface* CreateRealTimeTask(StackTraits stackTraits, uint32_t id, Fn&& tf,
                                          Args... args) {
            TaskInterface* fn = nullptr;
            if (stackTraits.useSharedStack) {
                fn = new SharedCopyStackTask(this, std::move(tf), std::move(args)...);
            } else {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::move(tf),
                                             std::move(args)...);
            }
            fn->taskType = REAL_TIME_TASK_T;
            fn->id = id;
            std::lock_guard<SpinLock> lock(qMutexWithType[REAL_TIME_TASK_T]);
            rtRegisterTable.insert(*fn);
            return fn;
        }
    };
}  // namespace JAMScript

#endif