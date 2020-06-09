#ifndef JAMSCRIPT_JAMSCRIPT_INTERACTIVE_H
#define JAMSCRIPT_JAMSCRIPT_INTERACTIVE_H
#include <mutex>
#include <queue>
#include <memory>
#include <functional>
#include <unordered_set>
#include <future/future.h>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include "jamscript/manager/sporadic.hh"

namespace JAMScript {

    static std::function<bool(const std::pair<uint64_t, CTask*>&,
                              const std::pair<uint64_t, CTask*>&)>
        edf_cmp = [](const std::pair<uint64_t, CTask*>& p1, const std::pair<uint64_t, CTask*>& p2) {
            return p1.first > p2.first;
        };

    class Scheduler;
    class RealTimeTaskScheduleEntry;

    class InteractiveTaskManager : public SporadicTaskManager {
    public:
        friend class Scheduler;
        friend class TaskStealWorker;
        CTask* DispatchTask() override;
        CTask* StealTask(TaskStealWorker* thief) override;
        CTask* StealTaskFromPriorityQueue(TaskStealWorker* thief);
        void PauseTask(CTask* task) override;
        bool SetTaskReady(CTask* task) override;
        void RemoveTask(CTask* task) override;
        void EnableTask(CTask* task) override;
        void ClearAllTasks() override;
        const uint32_t NumberOfTaskReady() const override;
        void UpdateBurstToTask(CTask* task, uint64_t burst) override;
        CTask* CreateRIBTask(uint64_t burst, void* args, void (*func)(CTask*, void*)) override;
        CTask* CreateRIBTask(CTask* parent, uint64_t deadline, uint64_t burst, void* args,
                             void (*func)(CTask*, void*)) override;
        InteractiveTaskManager(Scheduler* scheduler, uint32_t stackSize);
        ~InteractiveTaskManager() override;

    private:
        std::deque<CTask*> interactiveStack;
        std::unordered_set<CTask*> interactiveWait;
        std::priority_queue<std::pair<uint64_t, CTask*>, std::vector<std::pair<uint64_t, CTask*>>,
                            decltype(edf_cmp)>
            interactiveQueue;
    };

}  // namespace JAMScript
#endif