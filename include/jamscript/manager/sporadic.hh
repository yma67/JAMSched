#ifndef JAMSCRIPT_JAMSCRIPT_SPORADIC_H
#define JAMSCRIPT_JAMSCRIPT_SPORADIC_H
#include <mutex>
#include <future/future.h>
#include <condition_variable>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include "jamscript/tasktype/tasktype.hh"

namespace JAMScript {

    class Scheduler;
    class TaskStealWorker;

    class SporadicTaskManager {
    public:
        friend class Scheduler;
        friend class TaskStealWorker;
        friend void BeforeEachJAMScriptImpl(CTask*);
        friend void AfterEachJAMScriptImpl(CTask*);
        friend CTask* NextTaskJAMScriptImpl(CScheduler*);
        friend void IdleTaskJAMScriptImpl(CScheduler*);
        friend void InteractiveTaskHandlePostCallback(CFuture*);
        virtual CTask* DispatchTask() = 0;
        virtual CTask* StealTask(TaskStealWorker* thief) = 0;
        virtual void PauseTask(CTask* task) = 0;
        virtual bool SetTaskReady(CTask* task) = 0;
        virtual void RemoveTask(CTask* task) = 0;
        virtual void EnableTask(CTask* task) = 0;
        virtual const uint32_t NumberOfTaskReady() const = 0;
        virtual void ClearAllTasks() = 0;
        virtual void UpdateBurstToTask(CTask* task, uint64_t burst) = 0;
        virtual CTask* CreateRIBTask(uint64_t burst, void* args, void (*func)(CTask*, void*)) = 0;
        virtual CTask* CreateRIBTask(CTask* parent, uint64_t deadline, uint64_t burst, void* args,
                                     void (*func)(CTask*, void*)) = 0;
        SporadicTaskManager(Scheduler* scheduler, uint32_t stackSize)
            : scheduler(scheduler), stackSize(stackSize) {}
        virtual ~SporadicTaskManager() {}

    protected:
        std::mutex m;
        std::condition_variable cv;
        Scheduler* scheduler;
        uint32_t stackSize;
        SporadicTaskManager(SporadicTaskManager const&) = delete;
        SporadicTaskManager(SporadicTaskManager&&) = delete;
        SporadicTaskManager& operator=(SporadicTaskManager const&) = delete;
        SporadicTaskManager& operator=(SporadicTaskManager&&) = delete;
    };

}  // namespace JAMScript
#endif