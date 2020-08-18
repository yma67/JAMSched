#ifndef JAMSCRIPT_TASKTHIEF_HH
#define JAMSCRIPT_TASKTHIEF_HH
#include "core/task/task.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace JAMScript
{

    class StealScheduler : public SchedulerBase
    {
    public:

        friend class RIBScheduler;

        virtual const uint32_t Size() const;
        virtual void Steal(TaskInterface *toSteal);
        virtual size_t StealFrom(StealScheduler *toSteal);

        void ShutDown() override;
        void RunSchedulerMainLoop() override;
        virtual void StopSchedulerMainLoop();
        
        void Enable(TaskInterface *toEnable) override;
        void Disable(TaskInterface *toDisable) override;

        TimePoint GetSchedulerStartTime() const override;
        TimePoint GetCycleStartTime() const override;
        void SleepFor(TaskInterface* task, const Duration &dt) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) override;

        StealScheduler(RIBScheduler *victim, uint32_t ssz);
        virtual ~StealScheduler() override;

    private:

        bool isRunning;
        RIBScheduler *victim;
        JAMStorageTypes::ThiefQueueType isReady;

    };

} // namespace JAMScript
#endif