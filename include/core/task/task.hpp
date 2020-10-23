#ifndef JAMSCRIPT_JAMSCRIPT_TASK_HH
#define JAMSCRIPT_JAMSCRIPT_TASK_HH
#include <any>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <future>
#include <vector>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/intrusive/treap_set.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include "time/time.hpp"
#include "concurrency/spinlock.hpp"
#include "core/coroutine/context.h"

#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/valgrind.h>
#endif

namespace jamc
{

    class TaskInterface;
    class Notifier;
    class RIBScheduler;
    class StealScheduler;
    struct EdfPriority;
    struct RealTimeIdKeyType;
    struct BIIdKeyType;
    class Mutex;

    namespace JAMHookTypes
    {

        struct ReadyBatchQueueTag;
        typedef boost::intrusive::list_member_hook<
            boost::intrusive::tag<ReadyBatchQueueTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
            ReadyBatchQueueHook;

        struct ReadyInteractiveStackTag;
        typedef boost::intrusive::list_member_hook<
            boost::intrusive::tag<ReadyInteractiveStackTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
            ReadyInteractiveStackHook;

        struct ReadyInteractiveEdfTag;
        typedef boost::intrusive::bs_set_member_hook<
            boost::intrusive::tag<ReadyInteractiveEdfTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
            ReadyInteractiveEdfHook;

        struct RealTimeTag;
        typedef boost::intrusive::unordered_set_member_hook<
            boost::intrusive::tag<RealTimeTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
            RealTimeHook;

        struct WaitQueueTag;
        typedef boost::intrusive::list_member_hook<
            boost::intrusive::tag<WaitQueueTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
            WaitQueueHook;

        struct WaitSetTag;
        typedef boost::intrusive::list_member_hook<
            boost::intrusive::tag<WaitSetTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>
        >   WaitSetHook;

        struct ThiefQueueTag;
        typedef boost::intrusive::list_member_hook<
            boost::intrusive::tag<ThiefQueueTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
            ThiefQueueHook;

        struct ThiefSetTag;
        typedef boost::intrusive::set_member_hook<
            boost::intrusive::tag<ThiefSetTag>, boost::intrusive::link_mode<boost::intrusive::auto_unlink>
        >   ThiefSetHook;

    } // namespace JAMHookTypes

    /**
     * Generalized Scheduler Interface
     * 
     */
    class SchedulerBase
    {
    public:

        friend class TaskInterface;
        template <typename Fn, typename... Args>
        friend class TaskBase;
        template <typename Fn, typename... Args>
        friend class SharedCopyStackTask;
        template <typename Fn, typename... Args>
        friend class StandAloneStackTask;
        friend class Timer;
        friend class Notifier;
        friend class RIBScheduler;
        friend class StealScheduler;

        /**
         * Virtual function for shutting down a scheduler
         */
        virtual void ShutDown() { if (toContinue) toContinue = false; }

        /**
         * Virtual function for getting scheduler start time from a scheduler with timer
         */
        virtual TimePoint GetSchedulerStartTime() const;

        /**
         * Virtual function for getting cycle start time from a scheduler with timer 
         */
        virtual TimePoint GetCycleStartTime() const;

        /**
         * Virtual function for sleeping a task for a duration from a scheduler with timer
         */
        virtual void SleepFor(TaskInterface* task, const Duration &dt) {}

        /**
         * Virtual function for sleeping a task until a timepoint from a scheduler with timer
         */
        virtual void SleepUntil(TaskInterface* task, const TimePoint &tp) {}

        /**
         * Virtual function for sleeping a task for a duration from a scheduler with timer
         * with lock, for monitor use
         */
        virtual void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) {}

        /**
         * Virtual function for sleeping a task until a timepoint from a scheduler with timer
         * with lock, for monitor use
         */
        virtual void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) {}

        /**
         * Virtual function for sleeping a task for a duration from a scheduler with timer
         * with lock, for monitor use
         */
        virtual void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) {}

        /**
         * Virtual function for sleeping a task until a timepoint from a scheduler with timer
         * with lock, for monitor use
         */
        virtual void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) {}

        /**
         * Event Loop of the scheduler
         * - dispatch tasks
         * - cleanups
         */
        virtual void RunSchedulerMainLoop() = 0;
        virtual void Enable(TaskInterface *toEnable) = 0;
        virtual void EnableImmediately(TaskInterface *toEnable) = 0;

        virtual TaskInterface *GetNextTask() = 0;
        virtual void EndTask(TaskInterface *ptrCurrTask) = 0;

        virtual RIBScheduler *GetRIBScheduler() { return nullptr; }

        TaskInterface *GetTaskRunning() { return taskRunning; }
        SchedulerBase(uint32_t sharedStackSize);
        virtual ~SchedulerBase();

    protected:

        SchedulerBase(SchedulerBase const &) = delete;
        SchedulerBase(SchedulerBase &&) = delete;
        SchedulerBase &operator=(SchedulerBase const &) = delete;
        SchedulerBase &operator=(SchedulerBase &&) = delete;

        mutable SpinOnlyMutex qMutex;
        std::condition_variable_any cvQMutex;
        std::atomic<bool> toContinue;
        std::atomic<TaskInterface *> taskRunning;
        uint8_t *sharedStackBegin, *sharedStackAlignedEnd, *sharedStackAlignedEndAct;
        uint32_t sharedStackSizeActual;
        uint32_t sharedStackSizeAligned;
        JAMScriptUserContext schedulerContext;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        uint64_t v_stack_id;
#endif

    };

    class JTLSMap;
    using JTLSLocation = void **;

    enum TaskType
    {
        INTERACTIVE_TASK_T = 0,
        BATCH_TASK_T = 1,
        REAL_TIME_TASK_T = 2
    };

    enum TaskStatus
    {
        TASK_READY = 0,
        TASK_PENDING = 1,
        TASK_FINISHED = 2,
        TASK_RUNNING
    };

    enum TaskReturn
    {
        SUCCESS_TASK,
        ERROR_TASK_CONTEXT_INIT,
        ERROR_TASK_INVALID_ARGUMENT,
        ERROR_TASK_STACK_OVERFLOW,
        ERROR_TASK_CONTEXT_SWITCH,
        ERROR_TASK_WRONG_TYPE,
        ERROR_TASK_CANCELLED
    };

    class TaskInterface;

    class TaskHandle
    {
    public:

        void Join();
        void Detach();
        
        TaskHandle(std::shared_ptr<Notifier> h);

        TaskHandle(TaskHandle &&other);
        TaskHandle &operator=(TaskHandle &&other);
        TaskHandle &operator=(TaskHandle const &) = delete;
        TaskHandle(TaskHandle const &) = delete;

    private:

        std::shared_ptr<Notifier> n;

    };

    namespace ctask {

        /**
         * Exit current task
         * @warning possiblilty for memory leak, since this does not unwind the stack
         */
        void Exit();

        /**
         * Yield current task
         * @warning possiblilty for memory leak, since this does not unwind the stack
         * @note frequently yielding tasks would break cache locality due to cache replacement on SMP or NUMA
         * @note not suggested to use on NUMA
         */
        void Yield();

        /**
         * Sleep current task for a duration
         * @remark similar as std::this_thread::sleep_for
         */
        template <typename _Clock, typename _Dur>
        void SleepFor(const std::chrono::duration<_Clock, _Dur> &dt);

        /**
         * Sleep current task for until a timepoint
         * @remark similar as std::this_thread::sleep_until
         */
        template <typename _Clock, typename _Dur>
        void SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp);
        
        /**
         * Get relative time difference between now and start of RT schedule cycle
         */
        Duration GetTimeElapsedCycle();

        /**
         * Get relative time difference between now and start of scheduler
         */
        Duration GetTimeElapsedScheduler();

        /**
         * Create Batch Task
         * @ref RIBScheduler::CreateBatchTask
         */
        template <typename ...Args>
        TaskHandle CreateBatchTask(Args&&... args);

        /**
         * Create Interactive Task
         * @ref RIBScheduler::CreateInteractiveTask
         */
        template <typename ...Args>
        TaskHandle CreateInteractiveTask(Args&&... args);

        /**
         * Create Real Time Task
         * @ref RIBScheduler::CreateRealTimeTask
         */
        template <typename ...Args>
        TaskHandle CreateRealTimeTask(Args&&... args);

        /**
         * Create Local Exec Interactive Task
         * @ref RIBScheduler::CreateLocalNamedInteractiveExecution
         */
        template <typename T, typename ...Args>
        auto CreateLocalNamedInteractiveExecution(Args&&... args);

        /**
         * Create Local Exec Batch Task
         * @ref RIBScheduler::CreateLocalNamedBatchExecution
         */
        template <typename T, typename ...Args>
        auto CreateLocalNamedBatchExecution(Args&&... args);

        /**
         * Create Local Exec Batch Task
         * @ref RIBScheduler::CreateLocalNamedBatchExecution
         */
        template <typename ...Args>
        auto CreateRemoteExecAsyncMultiLevelAvecRappeler(Args&&... args);

        /**
         * Create Async Remote Execution to Multiple Levels
         * @ref RIBScheduler::CreateRemoteExecAsyncMultiLevel
         */
        template <typename ...Args>
        auto CreateRemoteExecAsyncMultiLevel(Args&&... args);

        /**
         * Create Async Remote Execution to Mutiple Levels with Callback
         * @ref RIBScheduler::CreateRemoteExecAsyncAvecRappeler
         */
        template <typename ...Args>
        auto CreateRemoteExecAsyncAvecRappeler(Args&&... args);

        /**
         * Create Async Remote Execution
         * @ref RIBScheduler::CreateRemoteExecAsync
         */
        template <typename ...Args>
        auto CreateRemoteExecAsync(Args&&... args);

        /**
         * Create Sync Remote Execution to Multiple Levels
         * @ref RIBScheduler::CreateRemoteExecSyncMultiLevel
         */
        template <typename ...Args>
        auto CreateRemoteExecSyncMultiLevel(Args&&... args);

        /**
         * Create Sync Remote Execution
         * @ref RIBScheduler::CreateRemoteExecSync
         */
        template <typename ...Args>
        auto CreateRemoteExecSync(Args&&... args);

        /**
         * Consume One Object from a designated Broadcast Stream
         * @ref RIBScheduler::ConsumeOneFromBroadcastStream
         */
        auto ConsumeOneFromBroadcastStream(const std::string &nameSpace, const std::string &variableName);

        /**
         * Produce One Object to a designated Broadcast Stream
         * @ref RIBScheduler::ProduceOneToLoggingStream
         */
        auto ProduceOneToLoggingStream(const std::string &nameSpace, const std::string &variableName, const nlohmann::json &value);

    }

    template <typename _Clock, typename _Duration>
    std::chrono::steady_clock::time_point convert(std::chrono::time_point<_Clock, _Duration> const &timeout_time)
    {
        return std::chrono::steady_clock::now() + (timeout_time - _Clock::now());
    }

    class TaskInterface
    {
    public:

        template <typename Fn, typename... Args>
        friend class TaskBase;
        template <typename Fn, typename... Args>
        friend class SharedCopyStackTask;
        template <typename Fn, typename... Args>
        friend class StandAloneStackTask;
        friend class Timer;
        friend class Notifier;
        friend class RIBScheduler;
        friend class StealScheduler;
        friend class SpinMutex;
        friend class ConditionVariableAny;
        friend struct EdfPriority;
        friend struct RealTimeIdKeyType;
        friend struct BIIdKeyType;

        template <typename T, typename... Args>
        friend T &GetByJTLSLocation(JTLSLocation location, Args &&... args);

        friend void ctask::Exit();

        friend void ctask::Yield();

        template <typename _Clock, typename _Dur>
        friend void ctask::SleepFor(const std::chrono::duration<_Clock, _Dur> &dt);

        template <typename _Clock, typename _Dur>
        friend void ctask::SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp);

        template <typename ...Args>
        friend TaskHandle ctask::CreateBatchTask(Args&&... args);

        template <typename ...Args>
        friend TaskHandle ctask::CreateInteractiveTask(Args&&... args);

        template <typename ...Args>
        friend TaskHandle ctask::CreateRealTimeTask(Args&&... args);

        template <typename T, typename ...Args>
        friend auto ctask::CreateLocalNamedInteractiveExecution(Args&&... args);

        template <typename T, typename ...Args>
        friend auto ctask::CreateLocalNamedBatchExecution(Args&&... args);

        template <typename ...Args>
        friend auto ctask::CreateRemoteExecAsyncMultiLevelAvecRappeler(Args&&... args);

        template <typename ...Args>
        friend auto ctask::CreateRemoteExecAsyncMultiLevel(Args&&... args);

        template <typename ...Args>
        friend auto ctask::CreateRemoteExecAsyncAvecRappeler(Args&&... args);

        template <typename ...Args>
        friend auto ctask::CreateRemoteExecAsync(Args&&... args);

        template <typename ...Args>
        friend auto ctask::CreateRemoteExecSyncMultiLevel(Args&&... args);

        template <typename ...Args>
        friend auto ctask::CreateRemoteExecSync(Args&&... args);

        friend auto ctask::ConsumeOneFromBroadcastStream(const std::string &nameSpace, const std::string &variableName);

        friend auto ctask::ProduceOneToLoggingStream(const std::string &nameSpace, const std::string &variableName, const nlohmann::json &value);

        friend Duration ctask::GetTimeElapsedCycle();
        
        friend Duration ctask::GetTimeElapsedScheduler();

        friend bool operator<(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend bool operator>(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend bool operator==(const TaskInterface &a, const TaskInterface &b) noexcept;
        
        friend std::size_t hash_value(const TaskInterface &value) noexcept;
        friend bool priority_order(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend bool priority_inverse_order(const TaskInterface &a, const TaskInterface &b) noexcept;

        template <typename _Clock, typename _Dur>
        void SleepFor(const std::chrono::duration<_Clock, _Dur> &dt) 
        {
            GetSchedulerValue()->SleepFor(this, std::chrono::duration_cast<Duration>(dt));
        }

        template <typename _Clock, typename _Dur>
        void SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp) 
        {
            GetSchedulerValue()->SleepUntil(this, convert(tp));
        }

        template <typename _Clock, typename _Dur>
        void SleepFor(const std::chrono::duration<_Clock, _Dur> &dt, std::unique_lock<SpinMutex> &lk) 
        {
            GetSchedulerValue()->SleepFor(this, std::chrono::duration_cast<Duration>(dt), lk);
        }

        template <typename _Clock, typename _Dur>
        void SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp, std::unique_lock<SpinMutex> &lk)
        {
            GetSchedulerValue()->SleepUntil(this, convert(tp), lk);
        }

        RIBScheduler *GetRIBScheduler() { return GetSchedulerValue()->GetRIBScheduler(); }

        jamc::JAMHookTypes::ReadyBatchQueueHook rbQueueHook;
        jamc::JAMHookTypes::ReadyInteractiveStackHook riStackHook;
        jamc::JAMHookTypes::ReadyInteractiveEdfHook riEdfHook;
        boost::intrusive::unordered_set_member_hook<> rtHook;
        jamc::JAMHookTypes::WaitSetHook wsHook;
        jamc::JAMHookTypes::ThiefQueueHook trHook;
        jamc::JAMHookTypes::ThiefSetHook twHook;

        virtual void Execute() = 0;
        virtual void SwapTo(TaskInterface *) = 0;
        virtual void SwapFrom(TaskInterface *) = 0;
        virtual bool Steal(SchedulerBase *scheduler) = 0;
        
        virtual const bool CanSteal() const { return false; }
        void Enable() { GetSchedulerValue()->Enable(this); }
        void EnableImmediately() { GetSchedulerValue()->EnableImmediately(this); }
        void SwapOut();

        const TaskType GetTaskType() const { return taskType; }
        static void ExecuteC(uint32_t tsLower, uint32_t tsHigher);

        std::unordered_map<JTLSLocation, std::any> taskLocalStoragePool;
        std::unordered_map<JTLSLocation, std::any> *GetTaskLocalStoragePool();

        TaskInterface(SchedulerBase *scheduler);
        virtual ~TaskInterface();

    private:

        TaskInterface() = delete;

        TaskInterface(TaskInterface const &) = delete;
        TaskInterface(TaskInterface &&) = delete;
        TaskInterface &operator=(TaskInterface const &) = delete;
        TaskInterface &operator=(TaskInterface &&) = delete;

        static TaskInterface* Active();
        static thread_local TaskInterface *thisTask, *prevTask;
        static void GarbageCollect();
        static void ResetTaskInfos();

        SchedulerBase *GetSchedulerValue() { return scheduler.load(); }

        std::atomic<SchedulerBase *> scheduler;
        std::atomic<TaskType> taskType;
        std::atomic<TaskStatus> status;
        std::atomic_bool isStealable;
        std::atomic_intptr_t cvStatus;
        uint32_t id;
        long references;
        std::shared_ptr<Notifier> notifier;
        std::unique_ptr<struct timeout> timeOut;
        Duration deadline, burst;
        std::function<void()> onCancel;
        JAMScriptUserContext uContext;

    };

    namespace ctask {

        template <typename _Clock, typename _Dur>
        void SleepFor(const std::chrono::duration<_Clock, _Dur> &dt)
        {
            auto sleepStartTime = Clock::now();
            TaskInterface::Active()->SleepFor(dt);
            BOOST_ASSERT_MSG(sleepStartTime + dt <= Clock::now(), "Early Wakeup?");
        }

        template <typename _Clock, typename _Dur>
        void SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp)
        {
            auto sleepStartTime = Clock::now();
            TaskInterface::Active()->SleepUntil(tp);
            BOOST_ASSERT_MSG(tp <= Clock::now(), "Early Wakeup?");
        }

    } // namespace ctask

    struct EdfPriority
    {
        bool operator()(const TaskInterface &a, const TaskInterface &b) const
        {
            return priority_order(a, b);
        }
    };

    struct RealTimeIdKeyType
    {
        typedef uint32_t type;
        const type operator()(const TaskInterface &v) const
        {
            return v.id;
        }
    };

    struct BIIdKeyType
    {
        typedef uintptr_t type;
        const type operator()(const TaskInterface &v) const
        {
            return reinterpret_cast<uintptr_t>(&v);
        }
    };

    template <typename Fn, typename... Args>
    class TaskAttr
    {
    public:

        template <typename Fnx, typename... Argx>
        friend class SharedCopyStackTask;
        template <typename Fna, typename... Argsa>
        friend class StandAloneStackTask;
        friend class RIBScheduler;
        TaskAttr(Fn &&tf, Args... args) : tFunction(std::forward<Fn>(tf)), tArgs(std::forward<Args>(args)...) {}
        virtual ~TaskAttr() {}

    private:

        TaskAttr() = delete;

        typename std::decay<Fn>::type tFunction;
        std::tuple<Args...> tArgs;

    };

    template <typename Fn, typename... Args>
    class SharedCopyStackTask : public TaskInterface
    {
    public:

        const bool CanSteal() const override { return false; }
        
        void SwapFrom(TaskInterface *tNext) override
        {
            auto tScheduler = GetSchedulerValue();
            memcpy(tScheduler->sharedStackAlignedEndAct - privateStackSize, privateStack, privateStackSize);
            if (tNext != nullptr && tNext != this)
            {
                SwapToContext(&(tNext->uContext), &uContext);
            }
            else if (tNext == nullptr)
            {
                SwapToContext(&tScheduler->schedulerContext, &uContext);
            }
        }

        void SwapTo(TaskInterface *tNext) override
        {
            if (tNext == this) 
            {
                return;
            }
            auto tScheduler = GetSchedulerValue();
            void *tos = nullptr;
#ifdef __x86_64__
            asm("movq %%rsp, %0"
                : "=rm"(tos));
#elif defined(__aarch64__) || defined(__arm__)
            asm("mov %[tosp], sp"
                : [ tosp ] "=r"(tos));
#else
#error "not supported"
#endif
            if ((uintptr_t)(tos) <= (uintptr_t)tScheduler->sharedStackAlignedEndAct &&
                ((uintptr_t)(tScheduler->sharedStackAlignedEnd) - (uintptr_t)(tScheduler->sharedStackSizeActual)) <=
                (uintptr_t)(tos))
            {
                if (status == TASK_FINISHED) goto END_COPYSTACK;
                privateStackSize = (uintptr_t)(tScheduler->sharedStackAlignedEndAct) - (uintptr_t)(tos);
                if (privateStackSizeUpperBound < privateStackSize)
                {
                    if (privateStack != nullptr) {
                        delete[] privateStack;
                        privateStack = nullptr;
                    }
                    privateStackSizeUpperBound = privateStackSize;
                    try
                    {
                        privateStack = new uint8_t[privateStackSize];
                    }
                    catch (...)
                    {
                        // TODO: Exit task
                        status = TASK_FINISHED;
                        tScheduler->ShutDown();
                        SwapToContext(&uContext, &tScheduler->schedulerContext);
                    }
                }
                memcpy(privateStack, tos, privateStackSize);
END_COPYSTACK:
                if (tNext == nullptr)
                {
                    SwapToContext(&uContext, &tScheduler->schedulerContext);
                }
                else
                {
                    tNext->SwapFrom(this);
                }
                return;
            }
        }

        bool Steal(SchedulerBase *scheduler) override
        {
            if (isStealable)
            {
                this->scheduler = scheduler;
                RefreshContext();
                return true;
            }
            return false;
        }

        void Execute() override
        {
            std::apply(std::move(valueStore.tFunction), std::move(valueStore.tArgs));
        }

        SharedCopyStackTask(SchedulerBase *sched, Fn &&tf, Args... args) : TaskInterface(sched), valueStore(std::forward<Fn>(tf), std::forward<Args>(args)...), privateStack(nullptr),
                                                                           privateStackSize(0), privateStackSizeUpperBound(0)
        {
            RefreshContext();
        }

        ~SharedCopyStackTask()
        {
            if (privateStack != nullptr)
            {
                delete[] privateStack;
            }
        }

    private:

        void RefreshContext()
        {
            auto tScheduler = GetSchedulerValue();
            memset(&uContext, 0, sizeof(uContext));
            auto valueThisPtr = reinterpret_cast<uintptr_t>(this);
            uContext.uc_stack.ss_sp = tScheduler->sharedStackBegin;
            uContext.uc_stack.ss_size = tScheduler->sharedStackSizeActual;
            CreateContext(&uContext, reinterpret_cast<void (*)(void)>(TaskInterface::ExecuteC), 2,
                          uint32_t(valueThisPtr), uint32_t((valueThisPtr >> 16) >> 16));
        }

        SharedCopyStackTask() = delete;
        uint8_t *privateStack;
        uint32_t privateStackSize;
        uint32_t privateStackSizeUpperBound;
        TaskAttr<Fn, Args...> valueStore;

    };

    template <typename Fn, typename... Args>
    class StandAloneStackTask : public TaskInterface
    {
    public:

        const bool CanSteal() const override { return true; }

        void SwapFrom(TaskInterface *tNext) override
        {
            auto tScheduler = GetSchedulerValue();
            if (tNext != nullptr && tNext != this)
            {
                SwapToContext(&(tNext->uContext), &uContext);
            }
            else if (tNext == nullptr)
            {
                SwapToContext(&tScheduler->schedulerContext, &uContext);
            }
        }

        void SwapTo(TaskInterface *tNext) override
        {
            if (tNext == this) return;
            else if (tNext == nullptr)
            {
                auto tScheduler = GetSchedulerValue();
                SwapToContext(&uContext, &tScheduler->schedulerContext);
            }
            else
            {
                tNext->SwapFrom(this);
            }
        }

        bool Steal(SchedulerBase *scheduler) override
        {
            if (isStealable)
            {
                this->scheduler = scheduler;
                return true;
            }
            return false;
        }

        void Execute() override
        {
            std::apply(std::move(valueStore.tFunction), std::move(valueStore.tArgs));
        }

        void InitStack(uint32_t stackSize)
        {
            memset(&uContext, 0, sizeof(uContext));
            auto valueThisPtr = reinterpret_cast<uintptr_t>(this);
            uContext.uc_stack.ss_sp = reinterpret_cast<std::uint8_t*>(aligned_alloc(16, stackSize));
            uContext.uc_stack.ss_size = stackSize;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            v_stack_id = VALGRIND_STACK_REGISTER(
                uContext.uc_stack.ss_sp, (void *)((uintptr_t)uContext.uc_stack.ss_sp + uContext.uc_stack.ss_size));
#endif
            CreateContext(&uContext, reinterpret_cast<void (*)(void)>(TaskInterface::ExecuteC), 2,
                          uint32_t(valueThisPtr), uint32_t((valueThisPtr >> 16) >> 16));
        }

        StandAloneStackTask(SchedulerBase *sched, uint32_t stackSize, Fn &&tf, Args... args)
            : TaskInterface(sched), valueStore(std::forward<Fn>(tf), std::forward<Args>(args)...)
        {
            InitStack(stackSize);
        }

        ~StandAloneStackTask()
        {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            VALGRIND_STACK_DEREGISTER(v_stack_id);
#endif
            free(reinterpret_cast<uint8_t *>(uContext.uc_stack.ss_sp));
        }

    private:

#ifdef JAMSCRIPT_ENABLE_VALGRIND
        uint64_t v_stack_id;
#endif
        TaskAttr<Fn, Args...> valueStore;
    };

    namespace JAMStorageTypes
    {

        // Ready Queue
        // Interactive Priority Queue
        typedef boost::intrusive::treap_multiset<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface,
                JAMHookTypes::ReadyInteractiveEdfHook,
                &TaskInterface::riEdfHook>,
            boost::intrusive::constant_time_size< false >, 
            boost::intrusive::priority<EdfPriority>>
            InteractiveEdfPriorityQueueType;

        // Real Time Task Map
        typedef boost::intrusive::unordered_multiset<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface,
                boost::intrusive::unordered_set_member_hook<>,
                &TaskInterface::rtHook>,
            boost::intrusive::constant_time_size< false >, 
            boost::intrusive::key_of_value<RealTimeIdKeyType>>
            RealTimeIdMultiMapType;

        // Batch Queue
        typedef boost::intrusive::list<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface,
                JAMHookTypes::ReadyBatchQueueHook,
                &TaskInterface::rbQueueHook>, boost::intrusive::constant_time_size< false >>
            BatchQueueType;
        
        typedef boost::intrusive::list<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface,
                JAMHookTypes::ReadyInteractiveStackHook,
                &TaskInterface::riStackHook>, boost::intrusive::constant_time_size< false >>
            InteractiveReadyStackType;

        // Wait Queue
        typedef boost::intrusive::list<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::WaitSetHook, 
                &TaskInterface::wsHook
            >,
            boost::intrusive::constant_time_size< false >>
            WaitListType;
        
        typedef boost::intrusive::list<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface,
                jamc::JAMHookTypes::ThiefQueueHook,
                &TaskInterface::trHook>, boost::intrusive::constant_time_size< false >>
            ThiefQueueType;
        
        typedef boost::intrusive::set<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::ThiefSetHook, 
                &TaskInterface::twHook
            >,
            boost::intrusive::constant_time_size< false >,
            boost::intrusive::key_of_value<BIIdKeyType>>
            ThiefSetType;
        

    } // namespace JAMStorageTypes

    bool operator<(const TaskInterface &a, const TaskInterface &b) noexcept;
    bool operator>(const TaskInterface &a, const TaskInterface &b) noexcept;
    bool operator==(const TaskInterface &a, const TaskInterface &b) noexcept;
    std::size_t hash_value(const TaskInterface &value) noexcept;
    bool priority_order(const TaskInterface &a, const TaskInterface &b) noexcept;
    bool priority_inverse_order(const TaskInterface &a, const TaskInterface &b) noexcept;

} // namespace jamc
#endif
