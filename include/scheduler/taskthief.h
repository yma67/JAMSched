#ifndef JAMSCRIPT_TASKTHIEF_HH
#define JAMSCRIPT_TASKTHIEF_HH
#include <concurrency/spinlock.h>
#include <core/task/task.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace JAMScript {

    class StealScheduler : public SchedulerBase {
    public:

        friend class RIBScheduler;
        void Steal(TaskInterface* toSteal);
        void Enable(TaskInterface* toEnable) override;
        void Disable(TaskInterface* toDisable) override;
        const uint32_t Size() const;
        void ShutDown_();
        void ShutDown();
        void operator()();
        StealScheduler(SchedulerBase* victim, uint32_t ssz);
        ~StealScheduler();

    protected:

        std::mutex m;
        int stealCount = 0;
        std::condition_variable cv;
        std::vector<std::thread> tx;
        SchedulerBase* victim;
        JAMStorageTypes::InteractiveWaitSetType isWait;
        JAMStorageTypes::BatchQueueType isReady;

    };
    
}  // namespace JAMScript
#endif