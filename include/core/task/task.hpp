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
#include <nlohmann/json.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/treap_set.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include "time/time.hpp"
#include "concurrency/notifier.hpp"
#include "concurrency/spinlock.hpp"
#include "core/coroutine/context.h"

#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/valgrind.h>
#endif

namespace JAMScript
{

    class TaskInterface;
    class Notifier;
    class RIBScheduler;
    class StealScheduler;
    struct EdfPriority;
    struct RealTimeIdKeyType;
    struct BIIdKeyType;

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

    namespace ThisTask
    {

        extern thread_local TaskInterface *thisTask;

        TaskInterface *Active();
        void SleepFor(Duration dt);
        void SleepUntil(TimePoint tp);
        void SleepFor(Duration dt, std::unique_lock<SpinMutex> &lk, TaskInterface *f);
        void SleepUntil(TimePoint tp, std::unique_lock<SpinMutex> &lk, TaskInterface *f);
        void Yield();

    } // namespace ThisTask

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

        virtual TaskInterface *NextTask() { return nullptr; }
        virtual void Enable(TaskInterface *toEnable) {}
        virtual void Disable(TaskInterface *toEnable) {}
        virtual void ShutDown()
        {
            if (toContinue)
                toContinue = false;
        }

        TaskInterface *GetTaskRunning() { return taskRunning; }
        SchedulerBase(uint32_t sharedStackSize);
        virtual ~SchedulerBase();

    protected:

        SchedulerBase(SchedulerBase const &) = delete;
        SchedulerBase(SchedulerBase &&) = delete;
        SchedulerBase &operator=(SchedulerBase const &) = delete;
        SchedulerBase &operator=(SchedulerBase &&) = delete;

        bool toContinue;
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
        TaskHandle(std::shared_ptr<Notifier> h) : n(std::move(h)) {}
    private:
        std::shared_ptr<Notifier> n;
    };

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
        friend struct EdfPriority;
        friend struct RealTimeIdKeyType;
        friend struct BIIdKeyType;

        friend TaskInterface *ThisTask::Active();
        friend void ThisTask::SleepFor(Duration dt);
        friend void ThisTask::SleepUntil(TimePoint tp);
        friend void ThisTask::SleepFor(Duration dt, std::unique_lock<SpinMutex> &lk, TaskInterface *f);
        friend void ThisTask::SleepUntil(TimePoint tp, std::unique_lock<SpinMutex> &lk, TaskInterface *f);
        friend void ThisTask::Yield();

        friend bool operator<(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend bool operator>(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend bool operator==(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend std::size_t hash_value(const TaskInterface &value) noexcept;
        friend bool priority_order(const TaskInterface &a, const TaskInterface &b) noexcept;
        friend bool priority_inverse_order(const TaskInterface &a, const TaskInterface &b) noexcept;

        JAMScript::JAMHookTypes::ReadyBatchQueueHook rbQueueHook;
        JAMScript::JAMHookTypes::ReadyInteractiveStackHook riStackHook;
        JAMScript::JAMHookTypes::ReadyInteractiveEdfHook riEdfHook;
        boost::intrusive::unordered_set_member_hook<> rtHook;
        JAMScript::JAMHookTypes::WaitSetHook wsHook;
        JAMScript::JAMHookTypes::ThiefQueueHook trHook;
        JAMScript::JAMHookTypes::ThiefSetHook twHook;
        TaskStatus status;

        virtual void SwapOut() = 0;
        virtual void SwapIn() = 0;
        virtual void Execute() = 0;
        virtual bool Steal(SchedulerBase *scheduler) = 0;
        bool CanSteal() { return isStealable; }
        void Disable() { scheduler->Disable(this); }
        void Enable() { scheduler->Enable(this); }

        const TaskType GetTaskType() const { return taskType; }
        static void ExecuteC(uint32_t tsLower, uint32_t tsHigher);

        std::unordered_map<JTLSLocation, std::any> taskLocalStoragePool;
        std::unordered_map<JTLSLocation, std::any> *GetTaskLocalStoragePool();

        TaskInterface(SchedulerBase *scheduler);
        virtual ~TaskInterface();

    protected:

        TaskInterface() = delete;

        TaskInterface(TaskInterface const &) = delete;
        TaskInterface(TaskInterface &&) = delete;
        TaskInterface &operator=(TaskInterface const &) = delete;
        TaskInterface &operator=(TaskInterface &&) = delete;

        
        std::atomic<TaskType> taskType;
        std::atomic_bool isStealable;
        SchedulerBase *scheduler, *baseScheduler;
        JAMScriptUserContext uContext;
        long references;
        Duration deadline, burst;
        uint32_t id;
        std::shared_ptr<Notifier> notifier;
        std::function<void()> onCancel;

    };

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
        TaskAttr(Fn &&tf, Args &&... args) : tFunction(std::forward<Fn>(tf)), tArgs(std::forward<Args>(args)...) {}
        virtual ~TaskAttr() {}

    protected:

        TaskAttr() = delete;
        typename std::decay<Fn>::type tFunction;
        std::tuple<Args...> tArgs;

    };

    template <typename Fn, typename... Args>
    class SharedCopyStackTask : public TaskInterface
    {
    public:

        void SwapOut() override
        {
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
            if ((uintptr_t)(tos) <= (uintptr_t)scheduler->sharedStackAlignedEndAct &&
                ((uintptr_t)(scheduler->sharedStackAlignedEnd) - (uintptr_t)(scheduler->sharedStackSizeActual)) <=
                    (uintptr_t)(tos))
            {
                privateStackSize = (uintptr_t)(scheduler->sharedStackAlignedEndAct) - (uintptr_t)(tos);
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
                        status = TASK_FINISHED;
                        scheduler->ShutDown();
                        ThisTask::thisTask = nullptr;
                        SwapToContext(&uContext, &scheduler->schedulerContext);
                    }
                }
                memcpy(privateStack, tos, privateStackSize);
                ThisTask::thisTask = nullptr;
                SwapToContext(&uContext, &scheduler->schedulerContext);
                return;
            }
        }

        void SwapIn() override
        {
            memcpy(scheduler->sharedStackAlignedEndAct - privateStackSize, privateStack, privateStackSize);
            ThisTask::thisTask = this;
            isStealable = false;
            this->status = TASK_RUNNING;
            SwapToContext(&scheduler->schedulerContext, &uContext);
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

    public:

        void RefreshContext()
        {
            memset(&uContext, 0, sizeof(uContext));
            auto valueThisPtr = reinterpret_cast<uintptr_t>(this);
            uContext.uc_stack.ss_sp = scheduler->sharedStackBegin;
            uContext.uc_stack.ss_size = scheduler->sharedStackSizeActual;
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

        void SwapOut() override
        {
            ThisTask::thisTask = nullptr;
            auto *prev_scheduler = scheduler;
            scheduler->taskRunning = nullptr;
            SwapToContext(&uContext, &prev_scheduler->schedulerContext);
        }

        void SwapIn() override
        {
            ThisTask::thisTask = this;
            isStealable = false;
            scheduler->taskRunning = this;
            this->status = TASK_RUNNING;
            SwapToContext(&scheduler->schedulerContext, &uContext);
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
            auto valueThisPtr = reinterpret_cast<uintptr_t>(this);
            uContext.uc_stack.ss_sp = new uint8_t[stackSize];
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
            delete[] reinterpret_cast<uint8_t *>(uContext.uc_stack.ss_sp);
        }

    protected:

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
                JAMScript::JAMHookTypes::ThiefQueueHook,
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

} // namespace JAMScript
#endif