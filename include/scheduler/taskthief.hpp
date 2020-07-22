#ifndef JAMSCRIPT_TASKTHIEF_HH
#define JAMSCRIPT_TASKTHIEF_HH
#include <concurrency/spinlock.hpp>
#include <core/task/task.hpp>

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

        virtual void Steal(TaskInterface *toSteal);
        virtual size_t StealFrom(StealScheduler *toSteal);
        virtual const uint32_t Size() const;
        void ShutDown() override;
        void RunSchedulerMainLoop() override;
        void Enable(TaskInterface *toEnable) override;
        void Disable(TaskInterface *toDisable) override;
        void SleepFor(TaskInterface* task, const Duration &dt) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) override;

        StealScheduler(RIBScheduler *victim, uint32_t ssz);
        virtual ~StealScheduler();

    private:

        std::atomic<unsigned int> rCount = 0, iCount = 0;
        bool isRunning;
        std::thread t;
        RIBScheduler *victim;
        JAMStorageTypes::ThiefQueueType isReady;

    };

} // namespace JAMScript
#endif