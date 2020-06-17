#ifndef JAMSCRIPT_JAMSCRIPT_TASK_HH
#define JAMSCRIPT_JAMSCRIPT_TASK_HH
#include <concurrency/spinlock.h>
#include <time/time.h>

#include <any>
#include <atomic>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/treap_set.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/intrusive_ptr.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/coroutine/context.h"
#include "core/coroutine/task.h"
#ifdef JAMSCRIPT_ENABLE_VALGRIND
#include <valgrind/valgrind.h>
#endif
#include "concurrency/notifier.h"
namespace JAMScript {
    class TaskInterface;
    class Notifier;
    class RIBScheduler;
    class StealScheduler;
    struct EdfPriority;
    struct RealTimeIdKeyType;
    struct BIIdKeyType;
    namespace JAMHookTypes {
        struct ReadyBatchQueueTag;
        typedef boost::intrusive::list_member_hook<boost::intrusive::tag<ReadyBatchQueueTag>>
            ReadyBatchQueueHook;
        struct WaitBatchTag;
        typedef boost::intrusive::set_member_hook<boost::intrusive::tag<WaitBatchTag>> 
            WaitBatchHook;
        struct ReadyInteractiveStackTag;
        typedef boost::intrusive::slist_member_hook<boost::intrusive::tag<ReadyInteractiveStackTag>>
            ReadyInteractiveStackHook;
        struct ReadyInteractiveEdfTag;
        typedef boost::intrusive::bs_set_member_hook<boost::intrusive::tag<ReadyInteractiveEdfTag>>
            ReadyInteractiveEdfHook;
        struct WaitInteractiveTag;
        typedef boost::intrusive::set_member_hook<boost::intrusive::tag<WaitInteractiveTag>> 
            WaitInteractiveHook;
        struct RealTimeTag;
        typedef boost::intrusive::unordered_set_member_hook<boost::intrusive::tag<RealTimeTag>> 
            RealTimeHook;
    }  // namespace JAMHookTypes

    namespace ThisTask {
        extern thread_local TaskInterface* thisTask;
        TaskInterface* Active();
        void SleepFor(Duration dt);
        void SleepUntil(TimePoint tp);
        void SleepFor(Duration dt, std::unique_lock<SpinLock>& lk, Notifier* f);
        void SleepUntil(TimePoint tp, std::unique_lock<SpinLock>& lk, Notifier* f);
        void Yield();
    }  // namespace ThisTask

    class SchedulerBase {
    public:
        friend class TaskInterface;
        template <typename Fn, typename... Args>
        friend class TaskBase;
        template <typename Fn, typename... Args>
        friend class SharedCopyStackTask;
        template <typename Fn, typename... Args>
        friend class StandAloneStackTask;
        friend class Notifier;
        friend class RIBScheduler;
        friend class StealScheduler;
        TaskInterface* GetTaskRunning() { return taskRunning; }
        virtual TaskInterface* NextTask() { return nullptr; }
        virtual void Enable(TaskInterface* toEnable) {}
        virtual void Disable(TaskInterface* toDisable) {}
        virtual void ShutDown() {
            if (toContinue)
                toContinue = false;
        }
        SchedulerBase(uint32_t sharedStackSize)
            : sharedStackSizeActual(sharedStackSize), toContinue(true), sharedStackBegin(new uint8_t[sharedStackSize]) {
            uintptr_t u_p = (uintptr_t)(sharedStackSizeActual - (sizeof(void*) << 1) +
                                        (uintptr_t) reinterpret_cast<uintptr_t>(sharedStackBegin));
            u_p = (u_p >> 4) << 4;
            sharedStackAlignedEnd = reinterpret_cast<uint8_t*>(u_p);
#ifdef __x86_64__
            sharedStackAlignedEndAct = reinterpret_cast<uint8_t*>((u_p - sizeof(void*)));
            *(reinterpret_cast<void**>(sharedStackAlignedEndAct)) = nullptr;
#elif defined(__aarch64__) || defined(__arm__)
            sharedStackAlignedEndAct = (void*)(u_p);
#else
#error "not supported"
#endif
            sharedStackSizeAligned = sharedStackSizeActual - 16 - (sizeof(void*) << 1);
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            v_stack_id =
                VALGRIND_STACK_REGISTER(sharedStackBegin, (void*)((uintptr_t)sharedStackBegin + sharedStackSizeActual));
#endif
        }
        virtual ~SchedulerBase() {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            VALGRIND_STACK_DEREGISTER(v_stack_id);
#endif
            delete[] sharedStackBegin;
        }

    protected:
        bool toContinue;
        std::atomic<TaskInterface*> taskRunning;
        uint8_t *sharedStackBegin, *sharedStackAlignedEnd, *sharedStackAlignedEndAct;
        uint32_t sharedStackSizeActual;
        uint32_t sharedStackSizeAligned;
        JAMScriptUserContext schedulerContext;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        uint64_t v_stack_id;
#endif
    };

    class JTLSMap;
    using JTLSLocation = void**;

    enum TaskType { INTERACTIVE_TASK_T = 0, BATCH_TASK_T = 1, REAL_TIME_TASK_T = 2 };
    class TaskInterface {
    public:
        template <typename Fn, typename... Args>
        friend class TaskBase;
        template <typename Fn, typename... Args>
        friend class SharedCopyStackTask;
        template <typename Fn, typename... Args>
        friend class StandAloneStackTask;
        friend class Notifier;
        friend class RIBScheduler;
        friend class StealScheduler;
        friend struct EdfPriority;
        friend struct RealTimeIdKeyType;
        friend struct BIIdKeyType;
        friend TaskInterface* ThisTask::Active();
        friend void ThisTask::SleepFor(Duration dt);
        friend void ThisTask::SleepUntil(TimePoint tp);
        friend void ThisTask::SleepFor(Duration dt, std::unique_lock<SpinLock>& lk, Notifier* f);
        friend void ThisTask::SleepUntil(TimePoint tp, std::unique_lock<SpinLock>& lk, Notifier* f);
        friend void ThisTask::Yield();
        JAMScript::JAMHookTypes::ReadyBatchQueueHook rbQueueHook;
        JAMScript::JAMHookTypes::WaitBatchHook wbHook;
        JAMScript::JAMHookTypes::ReadyInteractiveStackHook riStackHook;
        JAMScript::JAMHookTypes::ReadyInteractiveEdfHook riEdfHook;
        JAMScript::JAMHookTypes::WaitInteractiveHook wiHook;
        boost::intrusive::unordered_set_member_hook<> rtHook;
        virtual void SwapOut() = 0;
        virtual void SwapIn() = 0;
        virtual void Execute() = 0;
        virtual bool Steal(SchedulerBase* scheduler) = 0;
        bool CanSteal() { return isStealable; }
        virtual ~TaskInterface();
        TaskType GetTaskType() { return taskType; }
        static void ExecuteC(uint32_t tsLower, uint32_t tsHigher);
        virtual void Join();
        virtual void Detach();
        std::unordered_map<JTLSLocation, std::any> taskLocalStoragePool;
        std::unordered_map<JTLSLocation, std::any>* GetTaskLocalStoragePool() { return &taskLocalStoragePool; }
        friend bool operator<(const TaskInterface& a, const TaskInterface& b) noexcept {
            if (a.deadline == b.deadline)
                return a.burst < b.burst;
            return a.deadline < b.deadline;
        }
        friend bool operator>(const TaskInterface& a, const TaskInterface& b) noexcept {
            if (a.deadline == b.deadline)
                return a.burst > b.burst;
            return a.deadline > b.deadline;
        }
        friend bool operator==(const TaskInterface& a, const TaskInterface& b) { return a.id == b.id; }
        friend std::size_t hash_value(const TaskInterface& value) { return std::size_t(value.id); }
        friend bool priority_order(const TaskInterface& a, const TaskInterface& b) {
            if (a.deadline == b.deadline)
                return a.burst < b.burst;
            return a.deadline < b.deadline;
        }
        friend bool priority_inverse_order(const TaskInterface& a, const TaskInterface& b) {
            if (a.deadline == b.deadline)
                return a.burst > b.burst;
            return a.deadline > b.deadline;
        }
        TaskInterface(SchedulerBase* scheduler);

    protected:
        TaskInterface() = delete;
        std::atomic<unsigned int> status;
        std::atomic<TaskType> taskType;
        std::atomic_bool isStealable;
        SchedulerBase *scheduler, *baseScheduler;
        JAMScriptUserContext uContext;
        long references;
        Duration deadline, burst;
        uint32_t id;
        Notifier notifier;
        std::function<void()> onCancel;
    };
    struct EdfPriority {
        bool operator()(const TaskInterface& a, const TaskInterface& b) const { return priority_order(a, b); }
    };
    struct RealTimeIdKeyType {
        typedef uint32_t type;
        const type operator()(const TaskInterface& v) const { return v.id; }
    };
    struct BIIdKeyType {
        typedef uintptr_t type;
        const type operator()(const TaskInterface& v) const { return reinterpret_cast<uintptr_t>(&v); }
    };
    template <typename Fn, typename... Args>
    class TaskAttr {
    public:
        template <typename Fnx, typename... Argx>
        friend class SharedCopyStackTask;
        template <typename Fna, typename... Argsa>
        friend class StandAloneStackTask;
        TaskAttr(Fn&& tf, Args&&... args) : tFunction(std::forward<Fn>(tf)), tArgs(std::forward<Args>(args)...) {}
        virtual ~TaskAttr() {}

    public:
        TaskAttr() = delete;
        typename std::decay<Fn>::type tFunction;
        std::tuple<Args...> tArgs;
    };
    template <typename Fn, typename... Args>
    class SharedCopyStackTask : public TaskInterface {
    public:
        void SwapOut() override {
            void* tos = nullptr;
#ifdef __x86_64__
            asm("movq %%rsp, %0" : "=rm"(tos));
#elif defined(__aarch64__) || defined(__arm__)
            asm("mov %[tosp], sp" : [ tosp ] "=r"(tos));
#else
#error "not supported"
#endif
            if ((uintptr_t)(tos) <= (uintptr_t)scheduler->sharedStackAlignedEndAct &&
                ((uintptr_t)(scheduler->sharedStackAlignedEnd) - (uintptr_t)(scheduler->sharedStackSizeActual)) <=
                    (uintptr_t)(tos)) {
                privateStackSize = (uintptr_t)(scheduler->sharedStackAlignedEndAct) - (uintptr_t)(tos);
                if (privateStackSizeUpperBound < privateStackSize) {
                    if (privateStack != nullptr)
                        delete[] privateStack;
                    privateStackSizeUpperBound = privateStackSize;
                    try {
                        privateStack = new uint8_t[privateStackSize];
                    } catch (...) {
                        status = TASK_FINISHED;
                        scheduler->ShutDown();
                        ThisTask::thisTask = nullptr;
                        SwapToContext(&uContext, &scheduler->schedulerContext);
                    }
                }
                memcpy(privateStack, tos, privateStackSize);
                ThisTask::thisTask = nullptr;
                this->status = TASK_PENDING;
                SwapToContext(&uContext, &scheduler->schedulerContext);
                return;
            }
        }
        void SwapIn() override {
            memcpy(scheduler->sharedStackAlignedEndAct - privateStackSize, privateStack, privateStackSize);
            ThisTask::thisTask = this;
            isStealable = false;
            this->status = TASK_RUNNING;
            SwapToContext(&scheduler->schedulerContext, &uContext);
        }
        bool Steal(SchedulerBase* scheduler) override {
            if (isStealable) {
                this->scheduler = scheduler;
                RefreshContext();
                return true;
            }
            return false;
        }
        void Execute() override { std::apply(std::move(valueStore.tFunction), std::move(valueStore.tArgs)); }
        SharedCopyStackTask(SchedulerBase* sched, Fn&& tf, Args... args)
            : TaskInterface(sched),
              valueStore(std::forward<Fn>(tf), std::forward<Args>(args)...),
              privateStack(nullptr),
              privateStackSize(0),
              privateStackSizeUpperBound(0) {
            RefreshContext();
        }

        ~SharedCopyStackTask() {
            if (privateStack != nullptr) {
                delete[] privateStack;
            }
        }

    public:
        SharedCopyStackTask() = delete;
        void RefreshContext() {
            memset(&uContext, 0, sizeof(uContext));
            auto valueThisPtr = reinterpret_cast<uintptr_t>(this);
            uContext.uc_stack.ss_sp = scheduler->sharedStackBegin;
            uContext.uc_stack.ss_size = scheduler->sharedStackSizeActual;
            CreateContext(&uContext, reinterpret_cast<void (*)(void)>(TaskInterface::ExecuteC), 2,
                          uint32_t(valueThisPtr), uint32_t((valueThisPtr >> 16) >> 16));
        }
        uint8_t* privateStack;
        uint32_t privateStackSize;
        uint32_t privateStackSizeUpperBound;
        TaskAttr<Fn, Args...> valueStore;
    };
    template <typename Fn, typename... Args>
    class StandAloneStackTask : public TaskInterface {
    public:
        void SwapOut() override {
            ThisTask::thisTask = nullptr;
            auto* prev_scheduler = scheduler;
            isStealable = true;
            scheduler->taskRunning = nullptr;
            this->status = TASK_PENDING;
            SwapToContext(&uContext, &prev_scheduler->schedulerContext);
        }
        void SwapIn() override {
            ThisTask::thisTask = this;
            isStealable = false;
            scheduler->taskRunning = this;
            this->status = TASK_RUNNING;
            SwapToContext(&scheduler->schedulerContext, &uContext);
        }

        bool Steal(SchedulerBase* scheduler) override {
            if (isStealable) {
                this->scheduler = scheduler;
                return true;
            }
            return false;
        }
        void Execute() override { std::apply(std::move(valueStore.tFunction), std::move(valueStore.tArgs)); }
        StandAloneStackTask(SchedulerBase* sched, uint32_t stackSize, Fn&& tf, Args... args)
            : TaskInterface(sched), valueStore(std::forward<Fn>(tf), std::forward<Args>(args)...) {
            InitStack(stackSize);
        }
        void InitStack(uint32_t stackSize) {
            auto valueThisPtr = reinterpret_cast<uintptr_t>(this);
            uContext.uc_stack.ss_sp = new uint8_t[stackSize];
            uContext.uc_stack.ss_size = stackSize;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            v_stack_id = VALGRIND_STACK_REGISTER(
                uContext.uc_stack.ss_sp, (void*)((uintptr_t)uContext.uc_stack.ss_sp + uContext.uc_stack.ss_size));
#endif
            CreateContext(&uContext, reinterpret_cast<void (*)(void)>(TaskInterface::ExecuteC), 2,
                          uint32_t(valueThisPtr), uint32_t((valueThisPtr >> 16) >> 16));
        }
        ~StandAloneStackTask() {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            VALGRIND_STACK_DEREGISTER(v_stack_id);
#endif
            delete[] reinterpret_cast<uint8_t*>(uContext.uc_stack.ss_sp);
        }

    public:
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        uint64_t v_stack_id;
#endif
        TaskAttr<Fn, Args...> valueStore;
    };
    namespace JAMStorageTypes {
        // Ready Queue
        // Interactive Priority Queue
        typedef boost::intrusive::treap_multiset<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::ReadyInteractiveEdfHook,
                &TaskInterface::riEdfHook
            >,
            boost::intrusive::priority<EdfPriority>
        >   InteractiveEdfPriorityQueueType;
        // Real Time Task Map
        typedef boost::intrusive::unordered_multiset<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                boost::intrusive::unordered_set_member_hook<>,
                &TaskInterface::rtHook
            >,
            boost::intrusive::key_of_value<RealTimeIdKeyType>
        >   RealTimeIdMultiMapType;
        // Batch Queue
        typedef boost::intrusive::list<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::ReadyBatchQueueHook,
                &TaskInterface::rbQueueHook>
        >   BatchQueueType;
        // Wait Queue
        // Interactive Wait Set
        typedef boost::intrusive::set<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::WaitInteractiveHook, 
                &TaskInterface::wiHook
            >,
            boost::intrusive::key_of_value<BIIdKeyType>
        >   InteractiveWaitSetType;
        // Batch Wait Set
        typedef boost::intrusive::set<
            TaskInterface,
            boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::WaitBatchHook, 
                &TaskInterface::wbHook
            >,
            boost::intrusive::key_of_value<BIIdKeyType>
        >   BatchWaitSetType;
        typedef boost::intrusive::slist<
            TaskInterface, 
                boost::intrusive::member_hook<
                TaskInterface, 
                JAMHookTypes::ReadyInteractiveStackHook,
            &TaskInterface::riStackHook>
        >   InteractiveReadyStackType;

    }  // namespace JAMStorageTypes
}  // namespace JAMScript
#endif