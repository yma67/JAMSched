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

jamc::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, uint32_t nThiefs)
    : SchedulerBase(sharedStackSize), timer(this), vClockI(std::chrono::nanoseconds(0)), idxRealTimeTask(0), 
      decider(this), cThief(0), vClockB(std::chrono::nanoseconds(0)), logManager(nullptr), broadcastManger(nullptr),
      numberOfPeriods(-1), rtRegisterTable(JAMStorageTypes::RealTimeIdMultiMapType::bucket_traits(bucket, RT_MMAP_BUCKET_SIZE))
{
    for (uint32_t i = 0; i < nThiefs; i++)
    {
        thiefs.push_back(std::make_unique<StealScheduler>(this, sharedStackSize));
    }
}

jamc::RIBScheduler::RIBScheduler(uint32_t sharedStackSize)
    : RIBScheduler(sharedStackSize, std::max(std::thread::hardware_concurrency() - 1, 1u)) {}

jamc::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                                      const std::string &appName, const std::string &devName)
    : RIBScheduler(sharedStackSize)
{
    remote = std::make_unique<Remote>(this, hostAddr, appName, devName);
}

jamc::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, const std::string &hostAddr,
                                      const std::string &appName, const std::string &devName, 
                                      RedisState redisState, std::vector<JAMDataKeyType> variableInfo)
    : RIBScheduler(sharedStackSize, hostAddr, appName, devName)
{
    logManager = std::make_unique<LogManager>(remote.get(), redisState);
    broadcastManger = std::make_unique<BroadcastManager>(remote.get(), std::move(redisState), std::move(variableInfo));
}

jamc::RIBScheduler::~RIBScheduler()
{
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    rtRegisterTable.clear_and_dispose(dTaskInf);
    iEDFPriorityQueue.clear_and_dispose(dTaskInf);
    iCancelStack.clear_and_dispose(dTaskInf);
    bQueue.clear_and_dispose(dTaskInf);
}

void jamc::RIBScheduler::SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy)
{
    if (normal.empty() || greedy.empty() || normal.back().eTime != greedy.back().eTime)
    {
        return;
    }
    std::unique_lock lScheduleReady(sReadyRTSchedule);
    rtScheduleNormal = std::move(normal);
    rtScheduleGreedy = std::move(greedy);
    decider.NotifyChangeOfSchedule(rtScheduleNormal, rtScheduleGreedy);
    cvReadyRTSchedule.notify_one();
}

jamc::TimePoint jamc::RIBScheduler::GetSchedulerStartTime() const
{
    return { schedulerStartTime };
}

jamc::TimePoint jamc::RIBScheduler::GetCycleStartTime() const
{
    return { cycleStartTime };
}

void jamc::RIBScheduler::SleepFor(TaskInterface* task, const Duration &dt) 
{
    timer.SetTimeoutFor(task, dt);
}

void jamc::RIBScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp) 
{
    timer.SetTimeoutUntil(task, tp);
}

void jamc::RIBScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) 
{
    timer.SetTimeoutFor(task, dt, lk);
}

void jamc::RIBScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) 
{
    timer.SetTimeoutUntil(task, tp, lk);
}

void jamc::RIBScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) 
{
    timer.SetTimeoutFor(task, dt, lk);
}

void jamc::RIBScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) 
{
    timer.SetTimeoutUntil(task, tp, lk);
}

void jamc::RIBScheduler::ShutDownRunOnce()
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

void jamc::RIBScheduler::ShutDown()
{
    std::call_once(ribSchedulerShutdownFlag, [this] { this->ShutDownRunOnce(); });
}

void jamc::RIBScheduler::Enable(TaskInterface *toEnable)
{
    BOOST_ASSERT_MSG(toEnable != nullptr, "Should not duplicate ready stack");
    std::lock_guard lock(qMutex);
    if (toEnable->taskType == INTERACTIVE_TASK_T)
    {
        iEDFPriorityQueue.insert(*toEnable);
    }
    if (toEnable->taskType == BATCH_TASK_T)
    {
        bQueue.push_back(*toEnable);
    }
    toEnable->status = TASK_READY;
    cvQMutex.notify_one();
}

void jamc::RIBScheduler::EnableImmediately(TaskInterface *toEnable)
{
    BOOST_ASSERT_MSG(toEnable != nullptr, "Should not duplicate ready stack");
    std::lock_guard lock(qMutex);
    if (toEnable->taskType == INTERACTIVE_TASK_T)
    {
        iEDFPriorityQueue.insert(*toEnable);
    }
    if (toEnable->taskType == BATCH_TASK_T)
    {
        bQueue.push_front(*toEnable);
    }
    toEnable->status = TASK_READY;
    cvQMutex.notify_one();
}

uint32_t jamc::RIBScheduler::GetThiefSizes()
{
    uint32_t sz = 0;
    for (auto &thief : thiefs)
        sz += thief->Size();
    return sz;
}

jamc::StealScheduler *jamc::RIBScheduler::GetMinThief()
{
    StealScheduler *minThief = nullptr;
    unsigned int minSize = std::numeric_limits<unsigned int>::max();
    std::vector<StealScheduler *> zeroSchedulers;
    for (auto& thief : thiefs)
    {
        size_t nsz = thief->Size();
        if (nsz == 0)
        {
            return thief.get();
        }
        if (minSize > nsz)
        {
            minThief = thief.get();
            minSize = nsz;
        }
    }
    return minThief;
}

nlohmann::json jamc::RIBScheduler::ConsumeOneFromBroadcastStream(const std::string &nameSpace, const std::string &variableName)
{
    if (broadcastManger != nullptr)
    {
        return broadcastManger->Get(nameSpace, variableName);
    }
    return {};
}

void jamc::RIBScheduler::ProduceOneToLoggingStream(const std::string &nameSpace, const std::string &variableName, const nlohmann::json &value)
{
    if (logManager != nullptr)
    {
        logManager->LogRaw(nameSpace, variableName, value);
    }
}

void jamc::RIBScheduler::RunSchedulerMainLoop()
{
    schedulerStartTime = Clock::now();
    std::thread tTimer{ [this] { timer.RunTimerLoop(); } };
    std::vector<std::thread> remoteCheckers;
    if (remote != nullptr)
    {
        remoteCheckers.emplace_back([this] { remote->CheckExpire(); });
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
        tThiefs.emplace_back([&thief] { thief->RunSchedulerMainLoop(); });
    }
    TaskInterface::ResetTaskInfos();
    while (toContinue)
    {
        auto starter = GetNextTask();
        if (starter != nullptr) 
        {
            starter->SwapFrom(nullptr);
            TaskInterface::CleanupPreviousTask();
            TaskInterface::ResetTaskInfos();
        }
    }
    for (int i = 0; i < thiefs.size(); i++) 
    {
        thiefs[i]->StopSchedulerMainLoop();
        tThiefs[i].join();
    }
    if (broadcastManger != nullptr && logManager != nullptr)
    {
        logManager->StopLoggerMainLoop();
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

jamc::TaskInterface *jamc::RIBScheduler::GetAnInteractiveBatchTask(std::unique_lock<decltype(qMutex)> &lock)
{
    if (bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty())
    {
        return nullptr;
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
        auto currentBatchIter = bQueue.begin();
        auto *cBatchPtr = &(*currentBatchIter);
        bQueue.erase(currentBatchIter);
        taskStartTime = Clock::now();
        return cBatchPtr;
    }
    DECISION_INTERACTIVE:
    {
        std::vector<TaskInterface *> toStealTasks;
        while (!iEDFPriorityQueue.empty() && 
                iEDFPriorityQueue.top()->deadline - iEDFPriorityQueue.top()->burst +
                schedulerStartTime < currentTime)
        {
            auto itedfqtop = iEDFPriorityQueue.top();
            auto *pTop = &(*itedfqtop);
            iEDFPriorityQueue.erase(itedfqtop);
            if (!thiefs.empty() && pTop->isStealable && pTop->CanSteal())
            {
                toStealTasks.push_back(pTop);
            }
            else
            {
                iCancelStack.push_front(*pTop);
            }
        }
        lock.unlock();
        {
            for (auto pTop: toStealTasks)
            {
                auto *pNextThief = GetMinThief();
                if (pNextThief != nullptr)
                {
                    pTop->Steal(pNextThief);
                    pNextThief->EnableImmediately(pTop);
                }
                else
                {
                    delete pTop;
                }
            }
        }
        lock.lock();
        while (iCancelStack.size() > 3)
        {
            auto *pItEdf = &(iCancelStack.back());
            iCancelStack.pop_back();
            pItEdf->onCancel();
            pItEdf->notifier->Notify();
            delete pItEdf;
        }
        if (!iEDFPriorityQueue.empty())
        {
            auto currentInteractiveIter = iEDFPriorityQueue.top();
            auto *cInteracPtr = &(*currentInteractiveIter);
            iEDFPriorityQueue.erase(currentInteractiveIter);
            taskStartTime = Clock::now();
            return cInteracPtr;
        }
        else if (!iCancelStack.empty())
        {
            auto currentInteractiveIter = iCancelStack.begin();
            auto *cIPtr = &(*currentInteractiveIter);
            iCancelStack.erase(currentInteractiveIter);
            taskStartTime = Clock::now();
            return cIPtr;
        }
        goto END_LOOP;
    }
    END_LOOP: return nullptr;
}

jamc::TaskInterface *jamc::RIBScheduler::GetNextTask()
{
    while (toContinue) 
    {
        if (TaskInterface::Active() != nullptr)
        {
            std::unique_lock lockRT(qMutex);
            switch (TaskInterface::Active()->taskType)
            {
                case (TaskType::BATCH_TASK_T): 
                {
                    vClockB += Clock::now() - taskStartTime;
                    break;
                }
                case (TaskType::REAL_TIME_TASK_T): 
                {
                    if (currentSchedule[idxRealTimeTask].eTime > std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US))
                    {
#ifdef JAMSCRIPT_BLOCK_WAIT
                        cvQMutex.wait_until(
                            lockRT,
                            GetCycleStartTime() + 
                            (currentSchedule[idxRealTimeTask].eTime - std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US)), 
                            [this] () -> bool {
                                return !(rtScheduleGreedy.empty() && rtScheduleNormal.empty() && toContinue);
                            }
                        );
#endif
                        if (!toContinue) 
                        {
                            return nullptr;
                        }
                    }
                    auto ceTime = currentSchedule[idxRealTimeTask].eTime;
                    lockRT.unlock();
                    while (Clock::now() - cycleStartTime < ceTime);
                    idxRealTimeTask++;
                    break;
                }
                case (TaskType::INTERACTIVE_TASK_T): 
                {
                    auto tDelta = Clock::now() - taskStartTime;
                    vClockI += tDelta;
                    TaskInterface::Active()->burst -= tDelta;
                    return nullptr;
                    break;
                }
                default: break;
            }
        }
        if (idxRealTimeTask < currentSchedule.size())
        {
            std::unique_lock lockRT(qMutex);
            currentTime = Clock::now();
            if (currentSchedule[idxRealTimeTask].sTime <= currentTime - cycleStartTime && 
                currentTime - cycleStartTime <= currentSchedule[idxRealTimeTask].eTime) 
            {
                if (currentSchedule[idxRealTimeTask].taskId != 0 && 
                    rtRegisterTable.count(currentSchedule[idxRealTimeTask].taskId) > 0)
                {
                    auto currentRTIter = rtRegisterTable.find(currentSchedule[idxRealTimeTask].taskId);
                    auto pRT = &(*currentRTIter);
                    rtRegisterTable.erase(currentRTIter);
                    eStats.jitters.push_back((currentTime - cycleStartTime) - currentSchedule[idxRealTimeTask].sTime);
                    return pRT;
                }
                else
                {
                    while (toContinue && Clock::now() - cycleStartTime <= 
                           currentSchedule[idxRealTimeTask].eTime - 
                           std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US))
                    {
                        currentTime = Clock::now();
                        auto pNextBI = GetAnInteractiveBatchTask(lockRT);
                        if (pNextBI != nullptr)
                        {
                            return pNextBI;
                        }
#ifdef JAMSCRIPT_BLOCK_WAIT
                        cvQMutex.wait_until(lockRT, 
                                            (cycleStartTime + currentSchedule[idxRealTimeTask].eTime - 
                                                std::chrono::microseconds(END_OF_RT_SLOT_SPIN_MAX_US)), 
                        [this]() -> bool { 
                            return !(bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty() && toContinue); 
                        });
#endif
                        if (!toContinue)
                        {
                            return nullptr;
                        }
                    }
                }
            }
            else
            {
                idxRealTimeTask++;
                continue;
            }
        }
        else
        {
            std::unique_lock lScheduleReady(sReadyRTSchedule);
            while (rtScheduleGreedy.empty() && rtScheduleNormal.empty())
            {
                lScheduleReady.unlock();
                {
                    std::unique_lock lQMutexScheduleReady(qMutex);
                    auto pNextBI = GetAnInteractiveBatchTask(lQMutexScheduleReady);
                    if (pNextBI != nullptr)
                    {
                        return pNextBI;
                    }
                }
                lScheduleReady.lock();
#if JAMSCRIPT_BLOCK_WAIT && RT_SCHEDULE_NOT_SET_RETRY_WAIT_TIME_NS > 0
                cvReadyRTSchedule.wait_for(
                    lScheduleReady, 
                    std::chrono::nanoseconds(RT_SCHEDULE_NOT_SET_RETRY_WAIT_TIME_NS)
                );
#endif
            }
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
            numberOfPeriods++;
            idxRealTimeTask = 0;
            cycleStartTime = Clock::now();
        }
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
    return nullptr;
}

void jamc::RIBScheduler::EndTask(TaskInterface *ptrCurrTask)
{
    if (ptrCurrTask != nullptr)
    {
        switch (ptrCurrTask->taskType)
        {
            case (TaskType::REAL_TIME_TASK_T): 
            {
                delete ptrCurrTask;
                break;
            }
            case (TaskType::BATCH_TASK_T):
            case (TaskType::INTERACTIVE_TASK_T): 
            {
                if (ptrCurrTask->status == TASK_FINISHED)
                {
                    delete ptrCurrTask;
                }
                break;
            }
            default: break;
        }
    }
}