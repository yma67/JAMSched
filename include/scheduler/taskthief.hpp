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

        void Steal(TaskInterface *toSteal);
        size_t StealFrom(StealScheduler *toSteal);
        void Enable(TaskInterface *toEnable) override;
        void Disable(TaskInterface *toDisable) override;
        const uint32_t Size() const;
        void ShutDown() override;
        void RunSchedulerMainLoop() override;

        StealScheduler(RIBScheduler *victim, uint32_t ssz);
        ~StealScheduler();

    private:
        void ShutDown_();
        std::atomic<unsigned int> rCount = 0, iCount = 0;
        bool isRunning;
        std::condition_variable_any cv;
        std::thread t;
        RIBScheduler *victim;
        JAMStorageTypes::ThiefQueueType isReady;

    };

} // namespace JAMScript
#endif