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
    class IOCPAgent;

    class StealScheduler : public SchedulerBase
    {
    public:

        friend class RIBScheduler;

        virtual const uint64_t Size() const;
        virtual size_t StealFrom(StealScheduler *toSteal);

        void ShutDown() override;
        void RunSchedulerMainLoop() override;
        virtual void StopSchedulerMainLoop();
        virtual TaskInterface *GetNextTask() override;
        virtual void EndTask(TaskInterface *ptrCurrTask) override;
        
        void Enable(TaskInterface *toEnable) override;
        void EnableImmediately(TaskInterface *toSteal) override;
#ifdef __APPLE__
        IOCPAgent *GetIOCPAgent() override { return evm; }
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

        StealScheduler(RIBScheduler *victim, uint32_t ssz);
        virtual ~StealScheduler() override;

    private:

        std::atomic_uint64_t upCPUTime, sizeOfQueue;
        bool isRunning;
        RIBScheduler *victim;
        IOCPAgent *evm;
        JAMStorageTypes::ThiefQueueType isReady;

    };

} // namespace jamc
#endif
