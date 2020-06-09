#ifndef JAMSCRIPT_JAMSCRIPT_REALTIME_H
#define JAMSCRIPT_JAMSCRIPT_REALTIME_H
#include <mutex>
#include <queue>
#include <memory>
#include <unordered_map>
#include <future/future.h>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>

namespace JAMScript {

    class Scheduler;

    class RealTimeTaskManager {
    public:
        friend class Scheduler;
        friend void BeforeEachJAMScriptImpl(CTask*);
        friend void AfterEachJAMScriptImpl(CTask*);
        friend CTask* NextTaskJAMScriptImpl(CScheduler*);
        friend void IdleTaskJAMScriptImpl(CScheduler*);
        friend void InteractiveTaskHandlePostCallback(CFuture*);
        void SpinUntilEndOfCurrentInterval();
        CTask* DispatchTask(uint32_t id);
        void RemoveTask(CTask* to_remove);
        CTask* CreateRIBTask(uint32_t id, void* args, void (*func)(CTask*, void*));
        RealTimeTaskManager(Scheduler* scheduler, uint32_t stackSize);
        ~RealTimeTaskManager();

    private:
        std::mutex m;
        Scheduler* scheduler;
        CSharedStack* cSharedStack;
        std::unordered_map<uint32_t, std::deque<CTask*>> taskMap;
        RealTimeTaskManager() = delete;
        RealTimeTaskManager(RealTimeTaskManager const&) = delete;
        RealTimeTaskManager(RealTimeTaskManager&&) = delete;
        RealTimeTaskManager& operator=(RealTimeTaskManager const&) = delete;
        RealTimeTaskManager& operator=(RealTimeTaskManager&&) = delete;
    };
}  // namespace JAMScript

#endif