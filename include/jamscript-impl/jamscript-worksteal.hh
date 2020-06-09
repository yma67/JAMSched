#ifndef JAMSCRIPT_JAMSCRIPT_WORKSTEAL_HH
#define JAMSCRIPT_JAMSCRIPT_WORKSTEAL_HH
#include <core/scheduler/task.h>

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace JAMScript {
    class SporadicTaskManager;
    class Scheduler;
    class FutureWaitable {
    public:
        std::mutex futureMutex;
        std::vector<std::shared_ptr<SporadicTaskManager>> sporadicManagers;
        FutureWaitable(const std::vector<std::shared_ptr<SporadicTaskManager>>& managers)
            : sporadicManagers(managers) {}
    };

    class TaskStealWorker : public FutureWaitable {
    public:
        friend class Scheduler;
        void Run();
        CScheduler* GetInteractiveCScheduler() { return interactiveScheduler; }
        CScheduler* GetBatchCScheduler() { return batchScheduler; }
        TaskStealWorker(const std::vector<std::shared_ptr<SporadicTaskManager>>& managers);
        ~TaskStealWorker();

    protected:
        CScheduler *interactiveScheduler, *batchScheduler;
        std::vector<std::thread> thief;
        static CTask* NextTaskWorkStealBatch(CScheduler* scheduler);
        static CTask* NextTaskWorkStealInteractive(CScheduler* scheduler);
        static void AfterTaskWorkSteal(CTask* self);
        void Detach();
        TaskStealWorker(TaskStealWorker const&) = delete;
        TaskStealWorker(TaskStealWorker&&) = delete;
        TaskStealWorker& operator=(TaskStealWorker const&) = delete;
        TaskStealWorker& operator=(TaskStealWorker&&) = delete;
    };
}  // namespace JAMScript
#endif