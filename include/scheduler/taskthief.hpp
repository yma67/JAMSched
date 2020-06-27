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
        void Enable(TaskInterface *toEnable) override;
        void Disable(TaskInterface *toDisable) override;
        const uint32_t Size() const;
        void ShutDown_();
        void ShutDown() override;
        void RunSchedulerMainLoop();

        StealScheduler(RIBScheduler *victim, uint32_t ssz);
        ~StealScheduler();

    protected:

        mutable SpinMutex m;
        std::atomic<unsigned int> rCount = 0;
        bool isRunning;
        std::condition_variable_any cv;
        std::vector<std::thread> tx;
        std::thread t;
        RIBScheduler *victim;
        JAMStorageTypes::ThiefQueueType isReady;

    };

} // namespace JAMScript
#endif