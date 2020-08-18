#include "scheduler/scheduler.hpp"
#include "concurrency/notifier.hpp"
#include "core/task/task.hpp"
#include "remote/data.hpp"
#include <algorithm>

#ifndef RT_SCHEDULE_NOT_SET_RETRY_WAIT_TIME_NS
#define RT_SCHEDULE_NOT_SET_RETRY_WAIT_TIME_NS 10000
#endif

#ifndef END_OF_RT_SLOT_SPIN_MAX_US
#define END_OF_RT_SLOT_SPIN_MAX_US 200
#endif

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, uint32_t nThiefs)
    : SchedulerBase(sharedStackSize), timer(this), vClockI(std::chrono::nanoseconds(0)),
      decider(this), cThief(0), vClockB(std::chrono::nanoseconds(0)), logManager(nullptr), broadcastManger(nullptr),
      numberOfPeriods(0), rtRegisterTable(JAMStorageTypes::RealTimeIdMultiMapType::bucket_traits(bucket, RT_MMAP_BUCKET_SIZE))
{
    for (uint32_t i = 0; i < nThiefs; i++)
    {
        thiefs.push_back(std::make_unique<StealScheduler>(this, sharedStackSize));
    }
}

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize)
    : RIBScheduler(sharedStackSize, std::max(std::thread::hardware_concurrency() - 1, 1u)) {}

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                                      const std::string &appName, const std::string &devName)
    : RIBScheduler(sharedStackSize)
{
    remote = std::make_unique<Remote>(this, hostAddr, appName, devName);
}

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                                      const std::string &appName, const std::string &devName, 
                                      RedisState redisState, std::vector<JAMDataKeyType> variableInfo)
    : RIBScheduler(sharedStackSize, hostAddr, appName, devName)
{
    logManager = std::make_unique<LogManager>(remote.get(), redisState);
    broadcastManger = std::make_unique<BroadcastManager>(remote.get(), std::move(redisState), std::move(variableInfo));
}

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, std::vector<std::unique_ptr<StealScheduler>> thiefs)
    : SchedulerBase(sharedStackSize), timer(this), vClockI(std::chrono::nanoseconds(0)), logManager(nullptr), 
      decider(this), cThief(0), vClockB(std::chrono::nanoseconds(0)), thiefs(std::move(thiefs)), broadcastManger(nullptr),
      numberOfPeriods(0), rtRegisterTable(JAMStorageTypes::RealTimeIdMultiMapType::bucket_traits(bucket, RT_MMAP_BUCKET_SIZE))
{}

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                                      const std::string &appName, const std::string &devName, std::vector<std::unique_ptr<StealScheduler>> thiefs)
    : RIBScheduler(sharedStackSize, std::forward<decltype(thiefs)>(thiefs))
{
    remote = std::make_unique<Remote>(this, hostAddr, appName, devName);
}

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                                      const std::string &appName, const std::string &devName, RedisState redisState,
                                      std::vector<JAMDataKeyType> variableInfo, std::vector<std::unique_ptr<StealScheduler>> thiefs)
    : RIBScheduler(sharedStackSize, std::forward<decltype(thiefs)>(thiefs))
{
    logManager = std::make_unique<LogManager>(remote.get(), redisState);
    broadcastManger = std::make_unique<BroadcastManager>(remote.get(), std::move(redisState), std::move(variableInfo));
    remote = std::make_unique<Remote>(this, hostAddr, appName, devName);
}

JAMScript::RIBScheduler::~RIBScheduler()
{
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    rtRegisterTable.clear_and_dispose(dTaskInf);
    iEDFPriorityQueue.clear_and_dispose(dTaskInf);
    iCancelStack.clear_and_dispose(dTaskInf);
    bQueue.clear_and_dispose(dTaskInf);
}

void JAMScript::RIBScheduler::SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy)
{
    std::unique_lock<std::mutex> lScheduleReady(sReadyRTSchedule);
    rtScheduleNormal = std::move(normal);
    rtScheduleGreedy = std::move(greedy);
    decider.NotifyChangeOfSchedule(rtScheduleNormal, rtScheduleGreedy);
    cvReadyRTSchedule.notify_one();
}

JAMScript::TimePoint JAMScript::RIBScheduler::GetSchedulerStartTime() const
{
    return { schedulerStartTime };
}

JAMScript::TimePoint JAMScript::RIBScheduler::GetCycleStartTime() const
{
    return { cycleStartTime };
}

void JAMScript::RIBScheduler::SleepFor(TaskInterface* task, const Duration &dt) 
{
    timer.SetTimeoutFor(task, dt);
}

void JAMScript::RIBScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp) 
{
    timer.SetTimeoutUntil(task, tp);
}

void JAMScript::RIBScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) 
{
    timer.SetTimeoutFor(task, dt, lk);
}

void JAMScript::RIBScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) 
{
    timer.SetTimeoutUntil(task, tp, lk);
}

void JAMScript::RIBScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) 
{
    timer.SetTimeoutFor(task, dt, lk);
}

void JAMScript::RIBScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) 
{
    timer.SetTimeoutUntil(task, tp, lk);
}

void JAMScript::RIBScheduler::ShutDownRunOnce()
{
    if (remote != nullptr)
    {
        remote->CancelAllRExecRequests();
    }
    if (toContinue)
    {
        toContinue = false;
    }
    cvQMutex.notify_all();
}

void JAMScript::RIBScheduler::ShutDown()
{
    std::call_once(ribSchedulerShutdownFlag, [this] { this->ShutDownRunOnce(); });
}

void JAMScript::RIBScheduler::Disable(TaskInterface *toDisable)
{
    toDisable->status = TASK_PENDING;
}

void JAMScript::RIBScheduler::Enable(TaskInterface *toEnable)
{
    std::lock_guard lock(qMutex);
    if (toEnable->taskType == INTERACTIVE_TASK_T)
    {
        if (toEnable->deadline - toEnable->burst + schedulerStartTime > Clock::now())
        {
            BOOST_ASSERT_MSG(!toEnable->riStackHook.is_linked(), "Should not duplicate ready stack");
            iCancelStack.push_front(*toEnable);
        }
        else
        {
            BOOST_ASSERT_MSG(!toEnable->riEdfHook.is_linked(), "Should not duplicate ready edf");
            iEDFPriorityQueue.insert(*toEnable);
        }
    }
    if (toEnable->taskType == BATCH_TASK_T)
    {
        BOOST_ASSERT_MSG(!toEnable->rbQueueHook.is_linked(), "Should not duplicate ready batch");
        bQueue.push_back(*toEnable);
    }
    toEnable->status = TASK_READY;
    cvQMutex.notify_one();
}

uint32_t JAMScript::RIBScheduler::GetThiefSizes()
{
    uint32_t sz = 0;
    for (auto &thief : thiefs)
        sz += thief->Size();
    return sz;
}

JAMScript::StealScheduler *JAMScript::RIBScheduler::GetMinThief()
{
    StealScheduler *minThief = nullptr;
    unsigned int minSize = std::numeric_limits<unsigned int>::max();
    for (auto& thief : thiefs)
    {
        if (minSize > thief->Size())
        {
            minThief = thief.get();
            minSize = thief->Size();
        }
    }
    return minThief;
}

bool JAMScript::RIBScheduler::TryExecuteAnInteractiveBatchTask(std::unique_lock<decltype(qMutex)> &lock) {
    if (bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty())
    {
        return false;
    }
    else if (!bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty())
    {
        goto DECISION_BATCH;
    }
    else if (bQueue.empty() && (!iEDFPriorityQueue.empty() || !iCancelStack.empty()))
    {
        goto DECISION_INTERACTIVE;
    }
    else
    {
        if (vClockB > vClockI)
            goto DECISION_INTERACTIVE;
        else
            goto DECISION_BATCH;
    }
    DECISION_BATCH:
    {
        if (bQueue.empty())
        {
            goto END_LOOP;
        }
        auto currentBatchIter = bQueue.begin();
        auto *cBatchPtr = &(*currentBatchIter);
        bQueue.erase(currentBatchIter);
        lock.unlock();
        auto tStart = Clock::now();
        cBatchPtr->SwapIn();
        lock.lock();
        vClockB += Clock::now() - tStart;
        if (cBatchPtr->status == TASK_FINISHED)
        {
            delete cBatchPtr;
        }
        goto END_LOOP;
    }
    DECISION_INTERACTIVE:
    {
        while (!iEDFPriorityQueue.empty() && iEDFPriorityQueue.top()->deadline -
                                                        iEDFPriorityQueue.top()->burst +
                                                        schedulerStartTime <
                                                        currentTime)
        {
            auto *pTop = &(*iEDFPriorityQueue.top());
            iEDFPriorityQueue.erase(iEDFPriorityQueue.top());
            if (!thiefs.empty() && pTop->isStealable)
            {
                auto *pNextThief = GetMinThief();
                if (pNextThief != nullptr)
                {
                    pNextThief->Steal(pTop);
                }
            }
            else
            {
                iCancelStack.push_front(*pTop);
            }
        }
        while (iCancelStack.size() > 3)
        {
            auto *pItEdf = &(iCancelStack.back());
            iCancelStack.pop_back();
            pItEdf->onCancel();
            pItEdf->notifier->Notify();
            delete pItEdf;
        }
        if (iEDFPriorityQueue.empty() && !iCancelStack.empty())
        {
            auto currentInteractiveIter = iCancelStack.begin();
            auto *cIPtr = &(*currentInteractiveIter);
            iCancelStack.erase(currentInteractiveIter);
            lock.unlock();
            auto tStart = Clock::now();
            cIPtr->SwapIn();
            lock.lock();
            auto tDelta = Clock::now() - tStart;
            vClockI += tDelta;
            cIPtr->burst -= tDelta;
            if (cIPtr->status == TASK_FINISHED)
            {
                delete cIPtr;
            }
        }
        else if (!iEDFPriorityQueue.empty())
        {
            auto currentInteractiveIter = iEDFPriorityQueue.top();
            iEDFPriorityQueue.erase(currentInteractiveIter);
            auto *cInteracPtr = &(*currentInteractiveIter);
            lock.unlock();
            auto tStart = Clock::now();
            cInteracPtr->SwapIn();
            lock.lock();
            auto tDelta = Clock::now() - tStart;
            vClockI += tDelta;
            cInteracPtr->burst -= tDelta;
            if (cInteracPtr->status == TASK_FINISHED)
            {
                delete cInteracPtr;
            }
        }
        goto END_LOOP;
    }
    END_LOOP: return true;
}

void JAMScript::RIBScheduler::RunSchedulerMainLoop()
{
    schedulerStartTime = Clock::now();
    std::thread tTimer{ [this] { timer.RunTimerLoop(); } };
    std::vector<std::thread> remoteCheckers;
    if (remote != nullptr)
    {
        remoteCheckers.push_back(std::thread { [this] { remote->CheckExpire(); }});
    }
    if (broadcastManger != nullptr && logManager != nullptr)
    {
        tBroadcastManager = std::thread([this] {
            broadcastManger->RunBroadcastMainLoop();
        });
        tLogManger = std::thread([this] {
            logManager->RunLoggerMainLoop();
        });
    }
    std::vector<std::thread> tThiefs;
    for (auto& thief: thiefs) 
    {
        tThiefs.push_back(std::thread {
            [&thief] { thief->RunSchedulerMainLoop(); }
        });
    }
    while (toContinue)
    {
        std::unique_lock<std::mutex> lScheduleReady(sReadyRTSchedule);
        while (rtScheduleGreedy.empty() && rtScheduleNormal.empty())
        {
            lScheduleReady.unlock();
            std::unique_lock lQMutexScheduleReady(qMutex);
            auto haveBITaskToExecute = TryExecuteAnInteractiveBatchTask(lQMutexScheduleReady);
            lScheduleReady.lock();
#if JAMSCRIPT_BLOCK_WAIT && RT_SCHEDULE_NOT_SET_RETRY_WAIT_TIME_NS > 0
            if (!haveBITaskToExecute)
            {
                cvReadyRTSchedule.wait_for(
                    lScheduleReady, 
                    std::chrono::nanoseconds(RT_SCHEDULE_NOT_SET_RETRY_WAIT_TIME_NS)
                );
            }
#endif
        }
        decltype(rtScheduleGreedy) currentSchedule;
        if (rtScheduleNormal.empty())
        {
            currentSchedule = rtScheduleGreedy;
        }
        else if (rtScheduleGreedy.empty())
        {
            currentSchedule = rtScheduleNormal;
        }
        else
        {
            if (decider.DecideNextScheduleToRun())
            {
                currentSchedule = rtScheduleNormal;
            }
            else
            {
                currentSchedule = rtScheduleGreedy;
            }
        }
        lScheduleReady.unlock();
        cycleStartTime = Clock::now();
        for (auto &rtItem : currentSchedule)
        {
            currentTime = Clock::now();
            if (rtItem.sTime <= currentTime - cycleStartTime && currentTime - cycleStartTime <= rtItem.eTime)
            {
                std::unique_lock lockRT(qMutex);
                if (rtItem.taskId != 0 && rtRegisterTable.count(rtItem.taskId) > 0)
                {
                    auto currentRTIter = rtRegisterTable.find(rtItem.taskId);
                    lockRT.unlock();
                    eStats.jitters.push_back((currentTime - cycleStartTime) - rtItem.sTime);
                    currentRTIter->SwapIn();
                    lockRT.lock();
                    rtRegisterTable.erase_and_dispose(currentRTIter, [](TaskInterface *t) { delete t; });
#ifdef JAMSCRIPT_BLOCK_WAIT
                    if (currentSchedule.back().eTime > std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US))
                    {
                        cvQMutex.wait_until(
                            lockRT,
                            GetCycleStartTime() + 
                            (rtItem.eTime - std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US)), 
                            [this] () -> bool {
                                return !(rtScheduleGreedy.empty() && rtScheduleNormal.empty() && toContinue);
                            }
                        );
                    }
#endif
                    while (Clock::now() - cycleStartTime < rtItem.eTime);
                }
                else
                {
                    lockRT.unlock();
                    while (toContinue && Clock::now() - cycleStartTime <= rtItem.eTime)
                    {
                        currentTime = Clock::now();
                        std::unique_lock lockIBTask(qMutex);
                        if (!TryExecuteAnInteractiveBatchTask(lockIBTask)) 
                        {
#ifdef JAMSCRIPT_BLOCK_WAIT
                            cvQMutex.wait_until(lockIBTask, 
                                                (cycleStartTime + rtItem.eTime - 
                                                 std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US)), 
                            [this]() -> bool { 
                                return !(bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty() && toContinue); 
                            });
#endif
                        }
                        lockIBTask.unlock();
                        if (!toContinue)
                        {
                            break;
                        }
                    }
                }
            }
        }
        numberOfPeriods++;
#ifdef JAMSCRIPT_SCHED_AI_EXP
        if (!eStats.jitters.empty())
        {
            std::cout << "Jitters: ";
            long tj = 0, nj = 0;
            for (auto &j : eStats.jitters)
            {
                std::cout << std::chrono::duration_cast<std::chrono::microseconds>(j).count() << " ";
                tj += std::chrono::duration_cast<std::chrono::microseconds>(j).count();
                nj++;
            }
            std::cout << "AVG: " << tj / nj << std::endl;
            eStats.jitters.clear();
        }
#endif
    }
    for (int i = 0; i < thiefs.size(); i++) 
    {
        thiefs[i]->StopSchedulerMainLoop();
        tThiefs[i].join();
    }
    if (broadcastManger != nullptr && logManager != nullptr)
    {
        tLogManger.join();
        broadcastManger->StopBroadcastMainLoop();
        tBroadcastManager.join();
    }
    if (remote != nullptr)
    {
        remote->cvLoopSleep.notify_one();
        remoteCheckers[0].join();
    }
    tTimer.join();
}