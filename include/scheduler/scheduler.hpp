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

#ifndef RT_MMAP_BUCKET_SIZE
#define RT_MMAP_BUCKET_SIZE 200
#endif

namespace JAMScript
{

    struct RealTimeSchedule
    {
        std::chrono::steady_clock::duration sTime, eTime;
        uint32_t taskId;
        RealTimeSchedule(const std::chrono::steady_clock::duration &s,
                         const std::chrono::steady_clock::duration &e, uint32_t id)
            : sTime(s), eTime(e), taskId(id) {}
    };

    struct StackTraits
    {
        bool useSharedStack, canSteal;
        uint32_t stackSize;
        int pinCore;
        StackTraits(bool ux, uint32_t ssz) : useSharedStack(ux), stackSize(ssz), canSteal(true), pinCore(-1) {}
        StackTraits(bool ux, uint32_t ssz, bool cs) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1) {}
        StackTraits(bool ux, uint32_t ssz, bool cs, int pc) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(pc) {}
    };

    class LogManager;
    struct RedisState;
    class JAMDataKey;
    class BroadcastManager;

    class RIBScheduler : public SchedulerBase
    {
    public:

        friend class ConditionVariableAny;
        friend class BroadcastManager;
        friend class StealScheduler;        
        friend class LogManager;
        friend class Decider;
        friend class Remote;
        friend class Time;

        friend void ThisTask::Yield();

        void ShutDown() override;
        void RunSchedulerMainLoop() override;

        void Enable(TaskInterface *toEnable) override;
        void Disable(TaskInterface *toEnable) override;

        TimePoint GetSchedulerStartTime() const override;
        TimePoint GetCycleStartTime() const override;
        void SleepFor(TaskInterface* task, const Duration &dt) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) override;
        void SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) override;
        void SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) override;

        void SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy);

        template <typename Fn>
        void RegisterRPCall(const std::string &fName, Fn && fn) 
        {
            localFuncMap[fName] = std::make_unique<RExecDetails::RoutineRemote<decltype(std::function(fn))>>(std::function(fn));
        }

        // Not using const ref for memory safety
        nlohmann::json CreateJSONBatchCall(nlohmann::json rpcAttr) 
        {
            if (rpcAttr.contains("actname") && rpcAttr.contains("args") && 
                localFuncMap.find(rpcAttr["actname"].get<std::string>()) != localFuncMap.end()) 
            {
                auto fu = std::make_unique<Promise<nlohmann::json>>();
                auto fut = fu->GetFuture();
                CreateBatchTask({true, 0, true}, Clock::duration::max(), [this, fu { std::move(fu) }, rpcAttr(std::move(rpcAttr))]() 
                {
                    nlohmann::json jxe(localFuncMap[rpcAttr["actname"].get<std::string>()]->Invoke(std::move(rpcAttr["args"])));
                    fu->SetValue(std::move(jxe));
                }).Detach();
                return fut.Get();
            }
            return {};
        }

        bool CreateRPBatchCall(CloudFogInfo *execRemote, nlohmann::json rpcAttr) 
        {
            if (rpcAttr.contains("actname") && rpcAttr.contains("args") && 
                localFuncMap.find(rpcAttr["actname"].get<std::string>()) != localFuncMap.end() &&
                execRemote != nullptr && rpcAttr.contains("actid") && rpcAttr.contains("cmd")) 
            {
                CreateBatchTask({true, 0, true}, Clock::duration::max(), [this, execRemote, rpcAttr(std::move(rpcAttr))]() 
                {
                    nlohmann::json jResult(localFuncMap[rpcAttr["actname"].get<std::string>()]->Invoke(rpcAttr["args"]));
                    jResult["actid"] = rpcAttr["actid"].get<int>();
                    jResult["cmd"] = "REXEC-RES";
                    auto vReq = nlohmann::json::to_cbor(jResult.dump());
                    for (int i = 0; i < 3; i++)
                    {
                        std::unique_lock lk(Remote::mCallback);
                        if (Remote::isValidConnection.find(execRemote) != Remote::isValidConnection.end())
                        {
                            if (mqtt_publish(execRemote->mqttAdapter, const_cast<char *>("/replies/up"), nvoid_new(vReq.data(), vReq.size())))
                            {
                                lk.unlock();
                                break;
                            }
                        }
                        lk.unlock();
                    }
                }).Detach();
                return true;
            }
            return false;
        }

        template <typename Fn, typename... Args>
        TaskHandle CreateInteractiveTask(const StackTraits &stackTraits, Duration deadline, Duration burst,
                                         std::function<void()> onCancel, Fn &&tf, Args &&... args)
        {
            TaskInterface *fn = nullptr;
            if (stackTraits.useSharedStack)
            {
                fn = new SharedCopyStackTask(this, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            else
            {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            fn->taskType = INTERACTIVE_TASK_T;
            fn->burst = std::move(burst);
            fn->deadline = std::move(deadline);
            fn->onCancel = std::move(onCancel);
            fn->isStealable = stackTraits.canSteal;
            std::lock_guard lock(qMutex);
            decider.RecordInteractiveJobArrival(
                {std::chrono::duration_cast<std::chrono::microseconds>(deadline).count(),
                 std::chrono::duration_cast<std::chrono::microseconds>(burst).count()});
            iEDFPriorityQueue.insert(*fn);
            cvQMutex.notify_one();
            return { fn->notifier };
        }

        template <typename Fn, typename... Args>
        TaskHandle CreateBatchTask(const StackTraits &stackTraits, Duration burst, Fn &&tf, Args &&... args)
        {
            TaskInterface *fn = nullptr;
            if (stackTraits.useSharedStack)
            {
                fn = new SharedCopyStackTask(this, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            else
            {
                fn = new StandAloneStackTask(this, stackTraits.stackSize, std::forward<Fn>(tf), std::forward<Args>(args)...);
            }
            fn->taskType = BATCH_TASK_T;
            fn->burst = std::move(burst);
            fn->isStealable = stackTraits.canSteal;
            if (fn->isStealable)
            {
                StealScheduler *pNextThief = nullptr;
                if (stackTraits.pinCore > -1 && 
                    stackTraits.pinCore < thiefs.size() && 
                    thiefs[stackTraits.pinCore] != nullptr) 
                {
                    pNextThief = thiefs[stackTraits.pinCore].get();
                } 
                else 
                {
                    pNextThief = GetMinThief();
                }
                if (pNextThief != nullptr)
                {
                    pNextThief->Steal(fn);
                }
                else
                {
                    std::lock_guard lock(qMutex);
                    bQueue.push_back(*fn);
                    cvQMutex.notify_one();
                }
            } 
            else 
            {
                std::lock_guard lock(qMutex);
                bQueue.push_back(*fn);
                cvQMutex.notify_one();
            }
            return { fn->notifier };
        }

        template <typename Fn, typename... Args>
        TaskHandle CreateRealTimeTask(const StackTraits &stackTraits, uint32_t id, Fn &&tf, Args &&... args)
        {
            TaskInterface *fn = nullptr;
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
            fn->isStealable = stackTraits.canSteal;
            std::lock_guard lock(qMutex);
            rtRegisterTable.insert(*fn);
            cvQMutex.notify_one();
            return { fn->notifier };
        }

        template <typename T, typename... Args>
        Future<T> CreateLocalNamedInteractiveExecution(const StackTraits &stackTraits, Duration deadline, Duration burst, 
                                                       const std::string &eName, Args &&... eArgs) 
        {
            auto tAttr = std::make_unique<TaskAttr<std::function<T(Args...)>, Args...>>(
                std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            auto pf = std::make_shared<Promise<T>>();
            auto fu = pf->GetFuture();
            CreateInteractiveTask(stackTraits, std::forward<Duration>(deadline), std::forward<Duration>(burst), [pf]() 
            {
                pf->SetException(std::make_exception_ptr(InvalidArgumentException("Local Named Execution Cancelled")));
            }
            , [pf, tAttr { std::move(tAttr) }]() 
            {
                try 
                {
                    pf->SetValue(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                } 
                catch (const std::exception& e) 
                {
                    pf->SetException(std::make_exception_ptr(e));
                }
            }).Detach();
            return fu;
        }

        template <typename T, typename... Args>
        Future<T> CreateLocalNamedBatchExecution(const StackTraits &stackTraits, Duration burst, const std::string &eName, Args &&... eArgs) 
        {
            auto tAttr = std::make_unique<TaskAttr<std::function<T(Args...)>, Args...>>(
                std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            auto pf = std::make_unique<Promise<T>>();
            auto fu = pf->GetFuture();
            CreateBatchTask(stackTraits, std::forward<Duration>(burst),
            [pf { std::move(pf) }, tAttr { std::move(tAttr) }]() 
            {
                try 
                {
                    pf->SetValue(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                } 
                catch (const std::exception& e) 
                {
                    pf->SetException(std::make_exception_ptr(e));
                }
            }).Detach();
            return fu;
        }

        template<typename Fn>
        void RegisterLocalExecution(const std::string &eName, Fn && fn) 
        {
            lexecFuncMap[eName] = std::function(std::forward<Fn>(fn));
        }

        template <typename... Args>
        void CreateRemoteExecAsyncMultiLevel(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs) 
        {
            remote->CreateRExecAsyncWithCallbackToEachConnection(eName, condstr, condvec, []{}, std::forward<Args>(eArgs)...);
        }

        template <typename... Args>
        void CreateRemoteExecAsync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs) 
        {
            remote->CreateRExecAsync(eName, condstr, condvec, std::forward<Args>(eArgs)...);
        }

        template <typename T, typename... Args>
        T CreateRemoteExecSyncMultiLevel(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            return remote->CreateRExecSyncWithCallbackToEachConnection<T>(eName, condstr, condvec, []{}, std::forward<Args>(eArgs)...);
        }

        template <typename T, typename... Args>
        T CreateRemoteExecSync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            return remote->CreateRExecSync<T>(eName, condstr, condvec, std::forward<Args>(eArgs)...);
        }

        template <typename T>
        T ExtractRemote(Future<nlohmann::json>& future) 
        {
            return future.Get().get<T>();
        }

        using JAMDataKeyType = std::pair<std::string, std::string>;

        RIBScheduler(uint32_t sharedStackSize);
        RIBScheduler(uint32_t sharedStackSize, uint32_t nThiefs);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName, 
                     RedisState redisState, std::vector<JAMDataKeyType> variableInfo);
        RIBScheduler(uint32_t sharedStackSize, 
                     std::vector<std::unique_ptr<StealScheduler>> thiefs);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName, 
                     std::vector<std::unique_ptr<StealScheduler>> thiefs);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName, 
                     RedisState redisState, std::vector<JAMDataKeyType> variableInfo, 
                     std::vector<std::unique_ptr<StealScheduler>> thiefs);
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
        std::thread tTimer;
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

        TimePoint currentTime, schedulerStartTime, cycleStartTime;

        std::vector<std::unique_ptr<StealScheduler>> thiefs;
        std::vector<RealTimeSchedule> rtScheduleNormal, rtScheduleGreedy;
                
        std::unordered_map<std::string, std::any> lexecFuncMap;
        std::unordered_map<std::string, std::unique_ptr<RExecDetails::RoutineInterface>> localFuncMap;

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

} // namespace JAMScript

#endif