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
        std::chrono::high_resolution_clock::duration sTime, eTime;
        uint32_t taskId;
        RealTimeSchedule(const std::chrono::high_resolution_clock::duration &s,
                         const std::chrono::high_resolution_clock::duration &e, uint32_t id)
            : sTime(s), eTime(e), taskId(id) {}
    };

    struct StackTraits
    {
        bool useSharedStack;
        uint32_t stackSize;
        bool canSteal;
        int pinCore;
        StackTraits(bool ux, uint32_t ssz) : useSharedStack(ux), stackSize(ssz), canSteal(true), pinCore(-1) {}
        StackTraits(bool ux, uint32_t ssz, bool cs) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1) {}
        StackTraits(bool ux, uint32_t ssz, bool cs, int pc) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(pc) {}
    };

    class RIBScheduler : public SchedulerBase
    {
    public:

        friend class ConditionVariableAny;
        friend class StealScheduler;
        friend class Decider;
        friend class Remote;

        friend TaskInterface *ThisTask::Active();

        template <typename _Clock, typename _Dur>
        friend void ThisTask::SleepFor(std::chrono::duration<_Clock, _Dur> const &dt);

        template <typename _Clock, typename _Dur>
        friend void ThisTask::SleepUntil(std::chrono::time_point<_Clock, _Dur> const &tp);

        template <typename _Clock, typename _Dur>
        friend void ThisTask::SleepFor(std::chrono::duration<_Clock, _Dur> const &dt, std::unique_lock<SpinMutex> &lk, TaskInterface *f);

        template <typename _Clock, typename _Dur>
        friend void ThisTask::SleepUntil(std::chrono::time_point<_Clock, _Dur> const &tp, std::unique_lock<SpinMutex> &lk, TaskInterface *f);

        friend void ThisTask::Yield();

        void Enable(TaskInterface *toEnable) override;
        void Disable(TaskInterface *toEnable) override;

        const TimePoint &GetSchedulerStartTime() const;
        const TimePoint &GetCycleStartTime() const;
        void SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy);
        void ShutDown() override;
        bool Empty();
        void RunSchedulerMainLoop() override;

        void RegisterARPCall(const std::string &fName, const RExecDetails::RoutineInterface &rpcFunc) 
        {
            localFuncMap[fName] = &rpcFunc;
        }

        void RegisterRPCalls(std::unordered_map<std::string, const RExecDetails::RoutineInterface *> fvm) 
        {
            localFuncMap.insert(fvm.begin(), fvm.end());
        }

        // Not using const ref for memory safety
        nlohmann::json CreateJSONBatchCall(nlohmann::json rpcAttr) 
        {
            if (rpcAttr.contains("actname") && rpcAttr.contains("args") && 
                localFuncMap.find(rpcAttr["actname"].get<std::string>()) != localFuncMap.end()) 
            {
                auto* fu = new Promise<nlohmann::json>();
                CreateBatchTask({true, 0, true}, Clock::duration::max(), [this, fu, rpcAttr{ std::move(rpcAttr) }]() 
                {
                    auto jxe = std::move(localFuncMap[rpcAttr["actname"].get<std::string>()]->Invoke(rpcAttr["args"]));
                    fu->SetValue(std::move(jxe));
                    delete fu;
                }).Detach();
                auto fut = fu->GetFuture();
                return std::move(fut.Get());
            }
            return {};
        }

        // Not using const ref for memory safety
        bool CreateRPBatchCall(nlohmann::json rpcAttr) 
        {
            if (rpcAttr.contains("actname") && rpcAttr.contains("args") && 
                localFuncMap.find(rpcAttr["actname"].get<std::string>()) != localFuncMap.end() &&
                remote != nullptr && rpcAttr.contains("actid") && rpcAttr.contains("cmd")) 
            {
                CreateBatchTask({true, 0, true}, Clock::duration::max(), [this, rpcAttr{ std::move(rpcAttr) }]() 
                {
                    auto jResult = std::move(localFuncMap[rpcAttr["actname"].get<std::string>()]->Invoke(rpcAttr["args"]));
                    jResult["actid"] = rpcAttr["actid"].get<std::string>();
                    jResult["cmd"] = "REXEC-RES";
                    auto vReq = nlohmann::json::to_cbor(jResult.dump());
                    for (int i = 0; i < 3; i++)
                    {
                        if (mqtt_publish(remote->mq, const_cast<char *>("/replies/up"), nvoid_new(vReq.data(), vReq.size())))
                        {
                            break;
                        }
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
            return fn->notifier;
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
            std::lock_guard lock(qMutex);
            if (fn->isStealable)
            {
                StealScheduler *pNextThief = nullptr;
                if (stackTraits.pinCore > -1 && 
                    stackTraits.pinCore < thiefs.size() && 
                    thiefs[stackTraits.pinCore] != nullptr) 
                {
                    pNextThief = thiefs[stackTraits.pinCore];
                } 
                else 
                {
                    pNextThief = GetMinThief();
                }
                if (pNextThief != nullptr)
                {
                    pNextThief->Steal(fn);
                }
            } 
            else 
            {
                bQueue.push_back(*fn);
                cvQMutex.notify_one();
            }
            return fn->notifier;
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
            return fn->notifier;
        }

        template <typename T, typename... Args>
        Future<T> CreateLocalNamedInteractiveExecution(const StackTraits &stackTraits, Duration deadline, Duration burst, 
                                                       const std::string &eName, Args &&... eArgs) 
        {
            auto* pf = new Promise<T>();
            auto* tAttr = new TaskAttr(std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            CreateInteractiveTask(stackTraits, std::forward<Duration>(deadline), std::forward<Duration>(burst), [pf]() 
            {
                pf->SetException(std::make_exception_ptr(InvalidArgumentException("Local Named Execution Cancelled")));
            }
            , [pf, tAttr]() 
            {
                try 
                {
                    pf->SetValue(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                    delete tAttr;
                } 
                catch (const std::exception& e) 
                {
                    pf->SetException(std::make_exception_ptr(e));
                    delete tAttr;
                }
                delete pf;
            }).Detach();
            return std::move(pf->GetFuture());
        }

        template <typename T, typename... Args>
        Future<T> CreateLocalNamedBatchExecution(const StackTraits &stackTraits, Duration burst, const std::string &eName, Args &&... eArgs) 
        {
            auto* pf = new Promise<T>();
            auto* tAttr = new TaskAttr(std::any_cast<std::function<T(Args...)>>(lexecFuncMap[eName]), std::forward<Args>(eArgs)...);
            CreateBatchTask(stackTraits, std::forward<Duration>(burst),
            [pf, tAttr]() 
            {
                try 
                {
                    pf->SetValue(std::apply(std::move(tAttr->tFunction), std::move(tAttr->tArgs)));
                    delete tAttr;
                } 
                catch (const std::exception& e) 
                {
                    pf->SetException(std::make_exception_ptr(e));
                    delete tAttr;
                }
                delete pf;
            }).Detach();
            return std::move(pf->GetFuture());
        }

        template<typename Fn>
        void RegisterNamedExecution(const std::string &eName, Fn&& fn) 
        {
            lexecFuncMap[eName] = std::function(fn);
        }

        template <typename... Args>
        Future<nlohmann::json> CreateRemoteExecution(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs) 
        {
            return std::move(remote->CreateRExec(eName, condstr, condvec, std::forward<Args>(eArgs)...));
        }

        template <typename T>
        T ExtractRemote(Future<nlohmann::json>& future) 
        {
            return std::move(future.Get().get<T>());
        }

        RIBScheduler(uint32_t sharedStackSize);
        RIBScheduler(uint32_t sharedStackSize, uint32_t nThiefs);
        RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                     const std::string &appName, const std::string &devName);
        ~RIBScheduler() override;

    private:

        struct ExecutionStats
        {
            std::vector<std::chrono::high_resolution_clock::duration> jitters;
            std::vector<ITaskEntry> iRecords;
        };

        Timer timer;
        std::unique_ptr<Remote> remote;
        Decider decider;
        uint32_t cThief;
        std::vector<StealScheduler*> thiefs;
        ExecutionStats eStats;
        uint32_t numberOfPeriods;
        Duration vClockI, vClockB;
        std::mutex sReadyRTSchedule;
        std::condition_variable cvReadyRTSchedule, cvQMutex;
        TimePoint currentTime, schedulerStartTime, cycleStartTime;
        std::vector<RealTimeSchedule> rtScheduleNormal, rtScheduleGreedy;
        std::unordered_map<std::string, std::any> lexecFuncMap;
        std::unordered_map<std::string, const RExecDetails::RoutineInterface *> localFuncMap;

        JAMStorageTypes::BatchQueueType bQueue;
        JAMStorageTypes::InteractiveReadyStackType iCancelStack;
        JAMStorageTypes::RealTimeIdMultiMapType::bucket_type bucket[RT_MMAP_BUCKET_SIZE];
        JAMStorageTypes::RealTimeIdMultiMapType rtRegisterTable;
        JAMStorageTypes::InteractiveEdfPriorityQueueType iEDFPriorityQueue;

    private:

        uint32_t GetThiefSizes();
        StealScheduler* GetMinThief();
        bool TryExecuteAnInteractiveBatchTask(std::unique_lock<decltype(qMutex)> &lock);
        
    };

    namespace ThisTask {

        template <typename _Clock, typename _Dur>
        void SleepFor(const std::chrono::duration<_Clock, _Dur> &dt)
        {
            static_cast<RIBScheduler *>(thisTask->GetBaseScheduler())->timer.SetTimeoutFor(thisTask, std::move(std::chrono::duration_cast<Duration>(dt)));
        }

        template <typename _Clock, typename _Dur>
        void SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp)
        {
            static_cast<RIBScheduler *>(thisTask->GetBaseScheduler())->timer.SetTimeoutUntil(thisTask, std::move(convert(tp)));
        }

        template <typename _Clock, typename _Dur>
        void SleepFor(const std::chrono::duration<_Clock, _Dur> &dt, std::unique_lock<SpinMutex> &lk, TaskInterface *f)
        {
            static_cast<RIBScheduler *>(thisTask->GetBaseScheduler())->timer.SetTimeoutFor(thisTask, std::move(std::chrono::duration_cast<Duration>(dt)), lk, f);
        }

        template <typename _Clock, typename _Dur>
        void SleepUntil(const std::chrono::time_point<_Clock, _Dur> &tp, std::unique_lock<SpinMutex> &lk, TaskInterface *f)
        {
            static_cast<RIBScheduler *>(thisTask->GetBaseScheduler())->timer.SetTimeoutUntil(thisTask, std::move(convert(tp)), lk, f);
        }

    } // namespace ThisTask

} // namespace JAMScript

#endif