#ifndef JAMSCRIPT_TASKTHIEF_HH
#define JAMSCRIPT_TASKTHIEF_HH

#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <condition_variable>

#include "core/task/task.hpp"

namespace jamc
{
#if defined(__APPLE__) || defined(__linux__)
    class IOCPAgent;
#endif

    class StealScheduler : public SchedulerBase
    {
    public:

        friend class RIBScheduler;

        virtual uint64_t Size() const;
        virtual std::vector<TaskInterface *> Steal();

        void ShutDown() override;
        void RunSchedulerMainLoop() override;
        void StopSchedulerMainLoop();
        TaskInterface *GetNextTask() override;
        void EndTask(TaskInterface *ptrCurrTask) override;
        
        void Enable(TaskInterface *toEnable) override;
        void EnableImmediately(TaskInterface *toSteal) override;
#if defined(__APPLE__) || defined(__linux__)
        IOCPAgent *GetIOCPAgent() override;
#endif
        TimePoint GetSchedulerStartTime() const override;
        TimePoint GetCycleStartTime() const override;
        void SleepFor(TaskInterface* task, const Duration &dt) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) override;
        RIBScheduler *GetRIBScheduler() override { return victim; }
        void CancelTimeout(TaskInterface *) override;

        StealScheduler(RIBScheduler *victim, uint32_t ssz);
        ~StealScheduler() override;

    private:

        size_t StealFrom(StealScheduler *toSteal);
        std::atomic_uint64_t upCPUTime, sizeOfQueue;
        RIBScheduler *victim;
#if defined(__APPLE__) || defined(__linux__)
        IOCPAgent *evm;
#endif
        JAMStorageTypes::ThiefQueueType isReady;
        bool isRunning;

    };

} // namespace jamc
#endif
