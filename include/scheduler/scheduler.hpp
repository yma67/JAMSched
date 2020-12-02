#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_HH
#include <any>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include "time/time.hpp"
#include "remote/remote.hpp"
#include "core/task/task.hpp"
#include "scheduler/decider.hpp"
#include "scheduler/taskthief.hpp"
#include "scheduler/stacktraits.hpp"

#ifndef RT_MMAP_BUCKET_SIZE
#define RT_MMAP_BUCKET_SIZE 200
#endif

namespace jamc
{

    struct RealTimeSchedule
    {
        std::chrono::steady_clock::duration sTime, eTime;
        uint32_t taskId;
        RealTimeSchedule(const std::chrono::steady_clock::duration &s,
                         const std::chrono::steady_clock::duration &e, uint32_t id)
            : sTime(s), eTime(e), taskId(id) {}
    };

    struct RedisState;
    class BroadcastManager;
    class LogManager;
    class Timer;

    class RIBScheduler : public SchedulerBase
    {
    public:

        friend class ConditionVariableAny;
        friend class BroadcastManager;
        friend class StealScheduler;        
        friend class LogManager;
        friend class Decider;
        friend class Remote;
        friend class Timer;

        friend void ctask::Yield();

        void ShutDown() override;
        void RunSchedulerMainLoop() override;
        void Enable(TaskInterface *toEnable) override;
        void EnableImmediately(TaskInterface *toEnable) override;
        TimePoint GetSchedulerStartTime() const override;
        TimePoint GetCycleStartTime() const override;
        void SleepFor(TaskInterface* task, const Duration &dt) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) override;
        void CancelTimeout(TaskInterface *) override;

        /**
         * Set Schedule 
         * @param normal schedule
         * @param greedy schedule
         * @remark normal and greedy should have the same period, i.e. normal.back().eTime == normal.back().eTime
         */
        void SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy);

        /**
         * Register Remote Procedure Call
         * @param fName function name
         * @param stackTrait stack configuration to be used when launching the RPC
         * @param fn function itself, can be lambda, function pointer, ....
         * @warning thread non-safe, all invocation to this method should before the invocation of RIBScheduler::RunSchedulerMainLoop()
         */
        template <typename Fn>
        void RegisterRPCall(const std::string &fName, StackTraits stackTrait, Fn && fn) 
        {
            localFuncMap[fName] = RExecDetails::CreateRemoteRoutine(std::function(fn));
            localFuncStackTraitsMap[fName] = stackTrait;
        }
        
        /**
         * Register Remote Procedure Call With Default stack configuration
         * @param fName function name
         * @param fn function itself, can be lambda, function pointer, ....
         * @remark default stack configuration: standalone fixed-size 4kb stack, can be stolen/migrating
         * @warning thread non-safe, all invocation to this method should before the invocation of RIBScheduler::RunSchedulerMainLoop()
         */
        template <typename Fn>
        void RegisterRPCall(const std::string &fName, Fn && fn) 
        {
            localFuncMap[fName] = RExecDetails::CreateRemoteRoutine(std::function(fn));
            localFuncStackTraitsMap[fName] = StackTraits(true, 0, true);
        }

        /**
         * Execute Function By JSON
         * @param rpcAttr json containing function name as "actname", arguments (in json list) as "args"
         * @return json containing result of function execution
         */
        nlohmann::json CreateJSONBatchCall(nlohmann::json rpcAttr) 
        {
            if (rpcAttr.contains("actname") && rpcAttr.contains("args") && 
                localFuncMap.find(rpcAttr["actname"].get<std::string>()) != localFuncMap.end()) 
            {
#ifdef __CUDACC__
                auto fu = new promise<nlohmann::json>();
#else
                auto fu = std::make_unique<promise<nlohmann::json>>();
#endif
                auto fut = fu->get_future();
                auto fName = rpcAttr["actname"].get<std::string>();
                if (localFuncStackTraitsMap.find(fName) == localFuncStackTraitsMap.end())
                {
                    return false;
                }
                CreateBatchTask(StackTraits(localFuncStackTraitsMap[fName] ), Clock::duration::max(),
#ifdef __CUDACC__
                    [this, fu, fName, rpcAttr] ()
#else
                [this, fu { std::move(fu) }, fName { std::move(fName) }, rpcAttr(std::move(rpcAttr))] ()
#endif
                {
                    nlohmann::json jxe(localFuncMap[fName](rpcAttr["args"]));
                    fu->set_value(std::move(jxe));
#ifdef __CUDACC__
                    delete fu;
#endif
                }).Detach();
                return fut.get();
            }
            return {};
        }

        bool CreateRPBatchCall(CloudFogInfo *execRemote, nlohmann::json rpcAttr) 
        {
            if (rpcAttr.contains("actname") && rpcAttr.contains("args") && 
                localFuncMap.find(rpcAttr["actname"].get<std::string>()) != localFuncMap.end() &&
                execRemote != nullptr && rpcAttr.contains("actid") && rpcAttr.contains("cmd")) 
            {
                auto fName = rpcAttr["actname"].get<std::string>();
                StackTraits localFuncStackTraits(true, 0, true);
                auto itLocalFuncStackTraits = localFuncStackTraitsMap.find(fName);
                if (itLocalFuncStackTraits != localFuncStackTraitsMap.end())
                {
                    localFuncStackTraits = itLocalFuncStackTraits->second;
                }
                CreateBatchTask(StackTraits(localFuncStackTraits), Clock::duration::max(),
                [this, execRemote, rpcAttr]() 
                {
                    nlohmann::json jResult(localFuncMap[rpcAttr["actname"].get<std::string>()](rpcAttr["args"]));
                    jResult["actid"] = rpcAttr["actid"].get<int>();
                    jResult["cmd"] = "REXEC-RES";
                    auto vReq = nlohmann::json::to_cbor(jResult.dump());
#ifdef __CUDACC__
                    Remote::callbackThreadPool.enqueue([execRemote, vReq] () mutable {
#else
                    Remote::callbackThreadPool.enqueue([execRemote, vReq { std::move(vReq) }] () mutable {
#endif
                        std::shared_lock lk(Remote::mCallback);
                        for (int i = 0; i < 3; i++)
                        {
                            if (Remote::isValidConnection.find(execRemote) != Remote::isValidConnection.end())
                            {
                                if (mqtt_publish(execRemote->mqttAdapter, const_cast<char *>("/replies/up"), nvoid_new(vReq.data(), vReq.size())))
                                {
                                    lk.unlock();
                                    break;
                                }
                            }
                            else
                            {
                                lk.unlock();
                                break;
                            }
                        }
                    });
                }).Detach();
                return true;
            }
            return false;
        }

        /**
         * Create Interactive Task
         * @param stackTraits stack configuration
         * @param deadline soft relative deadline of the task with respect to start of cycle
         * @param burst burst duration of the task
         * @param onCancel callback to invoke if Interactive Task is cancelled
         * @param tf function of the task
         * @param args arguments of tf
         * @return taskhandle that can join or detach such task
         * @remark interactive task will be executed on main thread if not expired
         * @remark even if missed deadline, if the task chooses to be stolen, it could be distributed to a executor thread
         * @remark otherwise, it will be added to a LIFO stack, and it is subject to cancel, or could be executed if main thread has extra sporadic server
         */
        template <typename Fn, typename... Args>
        TaskHandle CreateInteractiveTask(const StackTraits &stackTraits, Duration deadline, Duration burst,
                                         std::function<void()> onCancel, Fn &&tf, Args &&... args)
        {
            TaskInterface *fn;
            if (stackTraits.useSharedStack)
            {
                fn = new SharedCopyStackTask(this, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            else
            {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            fn->taskType = INTERACTIVE_TASK_T;
            fn->burst = burst;
            fn->deadline = deadline;
            fn->onCancel = std::move(onCancel);
            fn->isStealable = stackTraits.canSteal;
            fn->enableImmediately = stackTraits.launchImmediately;
            std::lock_guard lock(qMutex);
            decider.RecordInteractiveJobArrival(
                {std::chrono::duration_cast<std::chrono::microseconds>(deadline).count(),
                 std::chrono::duration_cast<std::chrono::microseconds>(burst).count()});
            iEDFPriorityQueue.insert(*fn);
            cvQMutex.notify_one();
            return TaskHandle(fn->notifier);
        }
        
        /**
         * Create Batch Task
         * @param stackTraits stack configuration
         * @param burst burst duration of the task
         * @param tf function of the task
         * @param args arguments of tf
         * @return taskhandle that can join or detach such task
         * @remark if task is stealable, it will be distributed to the executor kernel thread with least amout of tasks in its ready queue
         * @remark if task is not stealable, it will be distributed to the main (real time) thread
         * @remark if the task choose to pin core and the core is available, the it will be added to the specified executor
         */
        template <typename Fn, typename... Args>
        TaskHandle CreateBatchTask(const StackTraits &stackTraits, Duration burst, Fn &&tf, Args &&... args)
        {
            TaskInterface *fn, *ptrCetteTache = TaskInterface::Active();
            if (stackTraits.useSharedStack)
            {
                fn = new SharedCopyStackTask(this, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            else
            {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            fn->taskType = BATCH_TASK_T;
            fn->burst = burst;
            fn->isStealable = stackTraits.canSteal;
            fn->enableImmediately = stackTraits.launchImmediately;
            auto ptrTaskHandle = fn->notifier;
            if (fn->isStealable)
            {
                StealScheduler *pNextThief;
                if (stackTraits.pinCore > -1 && 
                    stackTraits.pinCore < thiefs.size() && 
                    thiefs[stackTraits.pinCore] != nullptr) 
                {
                    pNextThief = thiefs[stackTraits.pinCore].get();
                } 
                else 
                {
                    pNextThief = GetMinThief();
                    if (ptrCetteTache != nullptr && this != ptrCetteTache->scheduler &&
                        pNextThief != nullptr && pNextThief->Size() > 0)
                    {
                        pNextThief = static_cast<StealScheduler *>(ptrCetteTache->GetSchedulerValue());
                    }
                    else if (pNextThief == nullptr || pNextThief->Size() > 0)
                    {
                        pNextThief = thiefs[rand() % thiefs.size()].get();
                    }
                }
                fn->Steal(pNextThief);
                if (stackTraits.directSwap && ptrCetteTache != nullptr && 
                    fn->GetSchedulerValue() == ptrCetteTache->GetSchedulerValue())
                {
                    ptrCetteTache->RendementALaTache(fn);
                }
                else
                {
                    fn->Enable();
                }
            }
            else 
            {
                std::lock_guard lock(qMutex);
                bQueue.push_back(*fn);
                cvQMutex.notify_one();
            }
            return TaskHandle(ptrTaskHandle);
        }

        /**
         * Create Real Time Task
         * @param stackTraits stack configuration
         * @param id identifier of the real time task to map to a real time slot with id
         * @param tf function of the task
         * @param args arguments of tf
         * @return taskhandle that can join or detach such task
         * @remark there is no guarantee that the first task added will be the first one to execute in the same slot with same id
         */
        template <typename Fn, typename... Args>
        TaskHandle CreateRealTimeTask(const StackTraits &stackTraits, uint32_t id, Fn &&tf, Args &&... args)
        {
            TaskInterface *fn;
            if (stackTraits.useSharedStack)
            {
                fn = new SharedCopyStackTask(this, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            else
            {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            fn->taskType = REAL_TIME_TASK_T;
            fn->id = id;
            fn->isStealable = false;
            std::lock_guard lock(qMutex);
            rtRegisterTable.insert(*fn);
            cvQMutex.notify_one();
            return TaskHandle(fn->notifier);
        }

        /**
         * Create Interactive Task By String Name
         * @param stackTraits stack configuration
         * @param deadline soft relative deadline of the task with respect to start of cycle
         * @param burst burst duration of the task
         * @param eName function name of the task
         * @param eArgs arguments of tf
         * @return future of the value
         * @remark interactive task will be executed on main thread if not expired
         * @remark even if missed deadline, if the task chooses to be stolen, it could be distributed to a executor thread
         * @remark otherwise, it will be added to a LIFO stack, and it is subject to cancel, or could be executed if main thread has extra sporadic server
         * @warning future may have exception that causes stack unwinding, so be careful when using this in main thread, this may break RealTime nature of main thread
         * @warning may throw exception if function is not registered
         */
        template <typename T, typename... Args>
        future<T> CreateLocalNamedInteractiveExecution(const StackTraits &stackTraits, Duration deadline, Duration burst, 
                                                       const std::string &eName, Args ... eArgs) 
        {
            auto tAttr = std::make_unique<TaskAttr<std::function<T(Args...)>, Args...>>(
                std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            auto pf = std::make_shared<promise<T>>();
            auto fu = pf->get_future();
            CreateInteractiveTask(stackTraits, std::forward<Duration>(deadline), std::forward<Duration>(burst), [pf]() 
            {
                pf->set_exception(std::make_exception_ptr(InvalidArgumentException("Local Named Execution Cancelled")));
            }
            , [pf, tAttr { std::move(tAttr) }]() 
            {
                try 
                {
                    if constexpr(std::is_same<T, void>::value)
                    {
                        std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs));
                        pf->set_value();
                    }
                    else
                    {
                        pf->set_value(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                    }
                } 
                catch (const std::exception& e) 
                {
                    pf->set_exception(std::make_exception_ptr(e));
                }
            }).Detach();
            return fu;
        }

        /**
         * Create Batch Task By String Name
         * @param stackTraits stack configuration
         * @param burst burst duration of the task
         * @param eName function of the task
         * @param eArgs arguments of tf
         * @return future of the value
         * @remark if task is stealable, it will be distributed to the executor kernel thread with least amout of tasks in its ready queue
         * @remark if task is not stealable, it will be distributed to the main (real time) thread
         * @remark if the task choose to pin core and the core is available, the it will be added to the specified executor
         * @warning future may have exception that causes stack unwinding, so be careful when using this in main thread, this may break RealTime nature of main thread
         * @warning may throw exception if function is not registered
         */
        template <typename T, typename... Args>
        future<T> CreateLocalNamedBatchExecution(const StackTraits &stackTraits, Duration burst, const std::string &eName, Args ... eArgs) 
        {
            auto tAttr = std::make_unique<TaskAttr<std::function<T(Args...)>, Args...>>(
                std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            auto pf = std::make_unique<promise<T>>();
            auto fu = pf->get_future();
            CreateBatchTask(stackTraits, std::forward<Duration>(burst),
            [pf { std::move(pf) }, tAttr { std::move(tAttr) }]() 
            {
                try 
                {
                    if constexpr(std::is_same<T, void>::value)
                    {
                        std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs));
                        pf->set_value();
                    }
                    else
                    {
                        pf->set_value(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                    }
                } 
                catch (const std::exception& e) 
                {
                    pf->set_exception(std::make_exception_ptr(e));
                }
            }).Detach();
            return fu;
        }

        /**
         * Create Real-Time Task By String Name
         * @param stackTraits stack configuration
         * @param id burst duration of the task
         * @param eName function of the task
         * @param eArgs arguments of tf
         * @return future of the value
         * @ref jamc::RIBScheduler::CreateRealTimeTask
         * @warning may throw exception if function is not registered
         */
        template <typename T, typename... Args>
        future<T> CreateLocalNamedRealTimeExecution(const StackTraits &stackTraits, uint32_t id, const std::string &eName, Args ... eArgs) 
        {
            auto tAttr = std::make_unique<TaskAttr<std::function<T(Args...)>, Args...>>(
                std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            auto pf = std::make_unique<promise<T>>();
            auto fu = pf->get_future();
            CreateRealTimeTask(stackTraits, id, [pf { std::move(pf) }, tAttr { std::move(tAttr) }]() 
            {
                try 
                {
                    if constexpr(std::is_same<T, void>::value)
                    {
                        std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs));
                        pf->set_value();
                    }
                    else
                    {
                        pf->set_value(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                    }
                } 
                catch (const std::exception& e) 
                {
                    pf->set_exception(std::make_exception_ptr(e));
                }
            }).Detach();
            return fu;
        }

        /**
         * Register Local Procedure Call With Name
         * @param eName function name
         * @param fn function itself, can be lambda, function pointer, ....
         * @warning thread non-safe, all invocation to this method should before the invocation of RIBScheduler::RunSchedulerMainLoop()
         */
        template<typename Fn>
        void RegisterLocalExecution(const std::string &eName, Fn && fn) 
        {
            lexecFuncMap[eName] = std::function(std::forward<Fn>(fn));
        }

        /**
         * Create Asynchronous Remote Execution, with failure Callback
         * @param eName name of such execution
         * @param condstr javascript predicate expression in C++ string
         * @param condvec reference value used to compare with evaluation of condstr in javascript
         * @param callBack callback invoked after failure
         * @param eArgs argument of execution
         * @remark callBack guaranteed to be invoked exactly once if all connections failed
         */
        template <typename... Args>
        void CreateRemoteExecAsyncMultiLevelAvecRappeler(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                                         std::function<void()> &&callBackSuccess,
                                                         std::function<void(std::error_condition)> &&callBackFailed, Args &&... eArgs) 
        {
            BOOST_ASSERT(remote != nullptr);
            remote->CreateRExecAsyncWithCallbackToEachConnection(eName, condstr, condvec, std::forward<std::function<void()>>(callBackSuccess),
                                                                 std::forward<std::function<void(std::error_condition)>>(callBackFailed), 
                                                                 std::forward<Args>(eArgs)...);
        }
        
        /**
         * Create Asynchronous Remote Execution to multiple connections, without failure Callback
         * @param eName name of such execution
         * @param condstr javascript predicate expression in C++ string
         * @param condvec reference value used to compare with evaluation of condstr in javascript
         * @param eArgs argument of execution
         */
        template <typename... Args>
        void CreateRemoteExecAsyncMultiLevel(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs) 
        {
            BOOST_ASSERT(remote != nullptr);
            remote->CreateRExecAsyncWithCallbackToEachConnection(eName, condstr, condvec, []{}, [](std::error_condition){}, 
                                                                 std::forward<Args>(eArgs)...);
        }

        /**
         * Create Asynchronous Remote Execution, with callback
         * @param eName name of such execution
         * @param condstr javascript predicate expression in C++ string
         * @param condvec reference value used to compare with evaluation of condstr in javascript
         * @param callBack callback invoked after failure
         * @param eArgs argument of execution
         * @remark callBack guaranteed to be invoked exactly once if connection failed
         */
        template <typename... Args>
        void CreateRemoteExecAsyncAvecRappeler(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                               std::function<void()> &&callBackSuccess,
                                               std::function<void(std::error_condition)> &&callBackFailed, Args &&... eArgs) 
        {
            BOOST_ASSERT(remote != nullptr);
            remote->CreateRExecAsyncWithCallback(eName, condstr, condvec, std::forward<std::function<void()>>(callBackSuccess),
                                                 std::forward<std::function<void(std::error_condition)>>(callBackFailed), 
                                                 std::forward<Args>(eArgs)...);
        }

        /**
         * Create Asynchronous Remote Execution
         * @param eName name of such execution
         * @param condstr javascript predicate expression in C++ string
         * @param condvec reference value used to compare with evaluation of condstr in javascript
         * @param eArgs argument of execution
         */
        template <typename... Args>
        void CreateRemoteExecAsync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs) 
        {
            BOOST_ASSERT(remote != nullptr);
            remote->CreateRExecAsyncWithCallback(eName, condstr, condvec, []{}, 
                                                 [](std::error_condition){},  std::forward<Args>(eArgs)...);
        }

        /**
         * Send C2J Synchronous Remote Call to Every Connections
         * @param eName name of execution
         * @param condstr javascript predicate expression in C++ string
         * @param condvec reference value used to compare with evaluation of condstr in javascript
         * @param timeOut an "exception" entry will be set in the json if the current task has been waiting for timeOut amount of time
         * @param eArgs argument of execution
         * @remark an "exception" entry will be set in the json if all connections failed their executions
         * @remark the value will be set after there exist one connection successfully sent its result back
         * @remark block until result return or timed out
         */
        template <typename... Args>
        nlohmann::json CreateRemoteExecSyncMultiLevel(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                                      Duration timeOut, Args &&... eArgs)
        {
            BOOST_ASSERT(remote != nullptr);
            return remote->CreateRExecSyncToEachConnection(eName, condstr, condvec, timeOut, std::forward<Args>(eArgs)...);
        }

        /**
         * Send C2J Synchronous Remote Call
         * @param eName name of execution
         * @param condstr javascript predicate expression in C++ string
         * @param condvec reference value used to compare with evaluation of condstr in javascript
         * @param timeOut an "exception" entry will be set in the json if the current task has been waiting for timeOut amount of time
         * @param eArgs argument of execution
         * @remark an "exception" entry will be set in the json if all connections failed their executions
         * @remark the value will be set after there exist one connection successfully sent its result back
         * @remark block until result return or timed out
         */
        template <typename... Args>
        nlohmann::json CreateRemoteExecSync(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                            Duration timeOut, Args &&... eArgs)
        {
            BOOST_ASSERT(remote != nullptr);
            return remote->CreateRExecSync(eName, condstr, condvec, timeOut, std::forward<Args>(eArgs)...);
        }

        /**
         * Consume One Object from a designated Broadcast Stream
         * @param nameSpace namespace of the designated broadcast stream
         * @param variableName variable name of the designated broadcast stream
         * @remark block until one value is available for this invocation
         */
        nlohmann::json ConsumeOneFromBroadcastStream(const std::string &nameSpace, const std::string &variableName);

        /**
         * Produce One Object to a designated Broadcast Stream
         * @param nameSpace namespace of the designated broadcast stream
         * @param variableName variable name of the designated broadcast stream
         * @param value value to be posted
         */
        void ProduceOneToLoggingStream(const std::string &nameSpace, const std::string &variableName, const nlohmann::json &value);

        template <typename T>
        T ExtractRemote(future<nlohmann::json>& future) 
        {
            return future.get().get<T>();
        }

        void SetStealers(std::vector<std::unique_ptr<StealScheduler>> tfs)
        { 
            this->thiefs = std::move(tfs);
        }

        using JAMDataKeyType = std::pair<std::string, std::string>;

        RIBScheduler *GetRIBScheduler() override { return this; }
        Timer &GetTimer() { return timer; }
        TaskInterface *GetNextTask() override;
        void EndTask(TaskInterface *ptrCurrTask) override;
        explicit RIBScheduler(uint32_t sharedStackSize);
        RIBScheduler(uint32_t sharedStackSize, uint32_t nThiefs);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName, 
                     RedisState redisState, std::vector<JAMDataKeyType> variableInfo);
        ~RIBScheduler() override;

    private:

        struct ExecutionStats
        {
            std::vector<std::chrono::steady_clock::duration> jitters;
            std::vector<ITaskEntry> iRecords;
        };

        Timer timer;
        Decider decider;
        uint32_t cThief;
        ExecutionStats eStats;
        uint32_t numberOfPeriods;
        Duration vClockI, vClockB;
        std::once_flag ribSchedulerShutdownFlag;

        std::unique_ptr<Remote> remote;
        std::unique_ptr<LogManager> logManager;
        std::unique_ptr<BroadcastManager> broadcastManger;

        std::mutex sReadyRTSchedule;
        std::thread tLogManger, tBroadcastManager;
        std::condition_variable cvReadyRTSchedule;

        TimePoint currentTime, schedulerStartTime, cycleStartTime, taskStartTime;

        std::vector<std::unique_ptr<StealScheduler>> thiefs;
        std::vector<RealTimeSchedule> rtScheduleNormal, rtScheduleGreedy;
        std::size_t idxRealTimeTask;

        std::unordered_map<std::string, std::any> lexecFuncMap;
        std::unordered_map<std::string, std::function<nlohmann::json(nlohmann::json)>> localFuncMap;
        std::unordered_map<std::string, StackTraits> localFuncStackTraitsMap;

        JAMStorageTypes::BatchQueueType bQueue;
        JAMStorageTypes::InteractiveReadyStackType iCancelStack;
        JAMStorageTypes::RealTimeIdMultiMapType::bucket_type bucket[RT_MMAP_BUCKET_SIZE];
        JAMStorageTypes::RealTimeIdMultiMapType rtRegisterTable;
        JAMStorageTypes::InteractiveEdfPriorityQueueType iEDFPriorityQueue;

    private:

        void ShutDownRunOnce();
        uint32_t GetThiefSizes();
        StealScheduler* GetMinThief();
        bool TryExecuteAnInteractiveBatchTask(std::unique_lock<decltype(qMutex)> &lock);
    };

    namespace ctask
    {
        
        template <typename ...Args>
        TaskHandle CreateBatchTask(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateBatchTask(std::forward<Args>(args)...);
        }
        
        template <typename ...Args>
        TaskHandle CreateInteractiveTask(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateInteractiveTask(std::forward<Args>(args)...);
        }
        
        template <typename ...Args>
        TaskHandle CreateRealTimeTask(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRealTimeTask(std::forward<Args>(args)...);
        }

        template <typename T, typename ...Args>
        auto CreateLocalNamedInteractiveExecution(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateLocalNamedInteractiveExecution<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename ...Args>
        auto CreateLocalNamedBatchExecution(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateLocalNamedBatchExecution<T>(std::forward<Args>(args)...);
        }

        template <typename ...Args>
        auto CreateRemoteExecAsyncMultiLevelAvecRappeler(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRemoteExecAsyncMultiLevelAvecRappeler(std::forward<Args>(args)...);
        }

        template <typename ...Args>
        auto CreateRemoteExecAsyncMultiLevel(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRemoteExecAsyncMultiLevel(std::forward<Args>(args)...);
        }

        template <typename ...Args>
        auto CreateRemoteExecAsyncAvecRappeler(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRemoteExecAsyncAvecRappeler(std::forward<Args>(args)...);
        }

        template <typename ...Args>
        auto CreateRemoteExecAsync(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRemoteExecAsync(std::forward<Args>(args)...);
        }

        template <typename ...Args>
        auto CreateRemoteExecSyncMultiLevel(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRemoteExecSyncMultiLevel(std::forward<Args>(args)...);
        }

        template <typename ...Args>
        auto CreateRemoteExecSync(Args&&... args)
        {
            BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
            return TaskInterface::Active()->GetRIBScheduler()->CreateRemoteExecSync(std::forward<Args>(args)...);
        }

    }

} // namespace jamc

#endif
