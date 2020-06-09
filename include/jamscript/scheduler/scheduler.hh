//
// Created by mayuxiang on 2020-05-15.
//

#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
#include <mutex>
#include <queue>
#include <tuple>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>
#include <utility>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <future/future.h>
#include <core/scheduler/task.h>
#include "jamscript/time/time.hh"
#include <xtask/shared-stack-task.h>
#include "jamscript/future/future.hh"
#include "jamscript/manager/batch.hh"
#include "jamscript/decider/decider.hh"
#include "jamscript/manager/realtime.hh"
#include "jamscript/manager/sporadic.hh"
#include "jamscript/manager/interactive.hh"
#include "jamscript/worksteal/worksteal.hh"

namespace JAMScript {

    struct RealTimeTaskScheduleEntry {
        uint64_t startTime, endTime;
        uint32_t taskId;
        RealTimeTaskScheduleEntry(uint64_t s, uint64_t e, uint32_t id)
            : startTime(s), endTime(e), taskId(id) {}
        bool inside(uint64_t timePoint) const {
            return ((startTime * 1000) <= timePoint) && (timePoint <= (endTime * 1000));
        }
    };

    /**
     * before task
     * start task executing clock
     */
    void BeforeEachJAMScriptImpl(CTask* self);

    /**
     * after task
     * @remark SetTaskReady task: increase clock, CreateRIBTask back to queue
     * @remark pending task: increase clock
     * @remark finished task: increase clock, free memory
     */
    void AfterEachJAMScriptImpl(CTask* self);

    /**
     * next task
     * @remark if the current time slot is RT, return RT
     * @remark if the current time slot is SS, if both queues empty, return nullptr, if batch queue
     * is empty we return one from interactive, and vice verca, otherwise, we DispatchTask according
     * to virtual clock value
     * @remark for batch task, we use a unbounded FIFO queue
     * @remark for interactive task, we use a priority queue to implement EDF, and another bounded
     * stack to store task with missing deadline
     * @remark when we DecideNextScheduleToRun to schedule interactive task, if there is any expired
     * task from priority queue, we put them into stack, if there is no task remaining after the
     * previous process, we pop the (last entered, latest) expired task from stack (LIFO) if no such
     * task exist in stack, return nullptr. if there is unexpired task from priority queue, we
     * return the task with earliest deadline (EDF)
     */
    CTask* NextTaskJAMScriptImpl(CScheduler* self);
    void IdleTaskJAMScriptImpl(CScheduler* self);
    void InteractiveTaskHandlePostCallback(CFuture* self);

#define GetCurrentSchedulerRunning()                 \
    (static_cast<JAMScript::Scheduler*>( \
        GetCurrentTaskRunning()->scheduler->GetSchedulerData(GetCurrentTaskRunning()->scheduler)))

    class ScheduleDecider;
    class BatchTaskManager;
    class RealTimeTaskManager;
    class InteractiveTaskManager;
    struct CTaskExtender;
    class JAMTimer;

    class Scheduler : public FutureWaitable {
    public:
        friend void AfterEachJAMScriptImpl(CTask*);
        friend void BeforeEachJAMScriptImpl(CTask*);
        friend void IdleTaskJAMScriptImpl(CScheduler*);
        friend CTask* NextTaskJAMScriptImpl(CScheduler*);
        friend void InteractiveTaskHandlePostCallback(CFuture*);
        friend void SleepFor(uint64_t dms);
        friend void SleepUntil(uint64_t dms);
        friend class ScheduleDecider;
        friend class BatchTaskManager;
        friend class SporadicTaskManager;
        friend class RealTimeTaskManager;
        friend class InteractiveTaskManager;
        friend class TaskStealWorker;
        void Run();
        void Exit();
        bool IsSchedulerRunning();
        uint32_t GetNumberOfCycleFinished();
        uint64_t GetCurrentTimepointInCycle();
        uint64_t GetCurrentTimepointInScheduler();
        uint64_t GetCurrentTimepointInTask();
        void RegisterNamedExecution(std::string name, void* fp);
        bool CreateRealTimeTask(uint32_t, void*, void (*)(CTask*, void*));
        bool CreateBatchTask(uint32_t burst, void* args, void (*f)(CTask*, void*));
        std::shared_ptr<CFuture> CreateInteractiveTask(uint64_t, uint64_t, void*,
                                                       void (*f)(CTask*, void*));
        Scheduler(std::vector<RealTimeTaskScheduleEntry> normalSchedule,
                  std::vector<RealTimeTaskScheduleEntry> greedySchedule, uint32_t deviceId,
                  uint32_t stackSize, void* localAppArgs, void (*localAppFunction)(CTask*, void*));
        ~Scheduler();

    private:
        JAMTimer jamscriptTimer;
        ScheduleDecider decider;
        CScheduler* cScheduler;
        CTask* cLocalAppTask;
        uint64_t virtualClock[2];
        RealTimeTaskManager realTimeTaskManager;
        std::atomic<uint32_t> multiplier;
        std::vector<long long> totalJitter;
        TaskStealWorker stealer;
        uint32_t currentScheduleSlot, deviceId;
        std::mutex namedExecMutex, timeMutex;
        std::vector<RealTimeTaskScheduleEntry>* currentSchedule;
        decltype(std::chrono::high_resolution_clock::now()) taskStartTime, cycleStartTime,
            schedulerStartTime;
        std::unordered_map<std::string, void*> local_function_map;
        std::vector<RealTimeTaskScheduleEntry> normalSchedule, greedySchedule;
        void DownloadSchedule();
        void MoveSchedulerSlot();
        std::vector<RealTimeTaskScheduleEntry>* DecideNextScheduleToRun();
        std::vector<RealTimeTaskScheduleEntry>* RandomDecide();
        Scheduler(Scheduler const&) = delete;
        Scheduler(Scheduler&&) = delete;
        Scheduler& operator=(Scheduler const&) = delete;
        Scheduler& operator=(Scheduler&&) = delete;

    public:
        template <typename Tr, typename Tf>
        static void LocalNamedTaskFunctionBatchRealtime(CTask* self, void* args) {
            {
                auto* exec_p = static_cast<std::pair<std::shared_ptr<Future<Tr>>, Tf>*>(args);
                exec_p->first->SetValue(std::move((exec_p->second)()));
                delete exec_p;
            }
            FinishTask(self, 0);
        }
        template <typename Tr, typename Tf>
        static void LocalNamedTaskFunctionInteractive(CTask* self, void* args) {
            {
                auto* self_cpp = static_cast<JAMScript::InteractiveTaskExtender*>(
                    self->taskFunctionVector->GetUserData(self));
                auto* exec_fp = static_cast<Tf*>(args);
                self_cpp->handle->data = new Tr(std::move((*exec_fp)()));
                self_cpp->handle->status = ACK_FINISHED;
                NotifyFinishOfFuture(self_cpp->handle.get());
                delete exec_fp;
            }
            FinishTask(self, 0);
        }
        template <typename Tr, typename... Args>
        std::shared_ptr<CFuture> CreateLocalNamedTaskAsync(uint64_t deadline, uint64_t duration,
                                                           std::string exec_name, Args&&... args) {
            if (local_function_map.find(exec_name) == local_function_map.end()) {
                return nullptr;
            }
            std::unique_lock<std::mutex> l(namedExecMutex);
            auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(local_function_map[exec_name]);
            l.unlock();
            auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
            return CreateInteractiveTask(deadline, duration,
                                         new decltype(exec_fp)(std::move(exec_fp)),
                                         LocalNamedTaskFunctionInteractive<Tr, decltype(exec_fp)>);
        }
        template <typename Tr, typename... Args>
        std::shared_ptr<Future<Tr>> CreateLocalNamedTaskAsync(uint32_t taskId,
                                                              std::string exec_name,
                                                              Args&&... args) {
            if (local_function_map.find(exec_name) == local_function_map.end()) {
                return nullptr;
            }
            std::unique_lock<std::mutex> l(namedExecMutex);
            auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(local_function_map[exec_name]);
            l.unlock();
            auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
            auto task_handle = std::make_shared<Future<Tr>>();
            CreateRealTimeTask(taskId,
                               new std::pair<std::shared_ptr<Future<Tr>>, decltype(exec_fp)>(
                                   task_handle, std::move(exec_fp)),
                               LocalNamedTaskFunctionBatchRealtime<Tr, decltype(exec_fp)>);
            return task_handle;
        }
        template <typename Tr, typename... Args>
        std::shared_ptr<Future<Tr>> CreateLocalNamedTaskAsync(uint64_t burst, std::string exec_name,
                                                              Args&&... args) {
            if (local_function_map.find(exec_name) == local_function_map.end()) {
                return nullptr;
            }
            std::unique_lock<std::mutex> l(namedExecMutex);
            auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(local_function_map[exec_name]);
            l.unlock();
            auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
            auto task_handle = std::make_shared<Future<Tr>>();
            CreateBatchTask(burst,
                            new std::pair<std::shared_ptr<Future<Tr>>, decltype(exec_fp)>(
                                task_handle, std::move(exec_fp)),
                            LocalNamedTaskFunctionBatchRealtime<Tr, decltype(exec_fp)>);
            return task_handle;
        }
    };

    inline void SleepFor(uint64_t dms) {
        GetCurrentSchedulerRunning()->jamscriptTimer.UpdateTimeout();
        GetCurrentSchedulerRunning()->jamscriptTimer.SetContinueOnTimeoutFor(GetCurrentTaskRunning(), dms * 1000);
    }

    inline void SleepUntil(uint64_t dms) {
        GetCurrentSchedulerRunning()->jamscriptTimer.SetContinueOnTimeoutUntil(GetCurrentTaskRunning(), dms);
    }

}  // namespace JAMScript

#endif  // JAMSCRIPT_JAMSCRIPT_SCHEDULER_H