#include "scheduler/scheduler.hpp"
#include <algorithm>

JAMScript::RIBScheduler::RIBScheduler(uint32_t sharedStackSize, uint32_t nThiefs)
    : SchedulerBase(sharedStackSize), timer(this), vClockI(std::chrono::nanoseconds(0)),
      decider(this), cThief(0), vClockB(std::chrono::nanoseconds(0)),
      numberOfPeriods(0), rtRegisterTable(JAMStorageTypes::RealTimeIdMultiMapType::bucket_traits(bucket, 200))
{
    for (uint32_t i = 0; i < nThiefs; i++)
    {
        thiefs.push_back(new StealScheduler{this, sharedStackSize});
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

JAMScript::RIBScheduler::~RIBScheduler()
{
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    rtRegisterTable.clear_and_dispose(dTaskInf);
    iEDFPriorityQueue.clear_and_dispose(dTaskInf);
    iCancelStack.clear_and_dispose(dTaskInf);
    bQueue.clear_and_dispose(dTaskInf);
    waitSet.clear_and_dispose(dTaskInf);
    std::for_each(thiefs.begin(), thiefs.end(), [](StealScheduler *ss) { delete ss; });
}

void JAMScript::RIBScheduler::SetSchedule(std::vector<RealTimeSchedule> normal, std::vector<RealTimeSchedule> greedy)
{
    rtScheduleNormal = std::move(normal);
    rtScheduleGreedy = std::move(greedy);
}

const JAMScript::TimePoint &JAMScript::RIBScheduler::GetSchedulerStartTime() const
{
    return schedulerStartTime;
}

const JAMScript::TimePoint &JAMScript::RIBScheduler::GetCycleStartTime() const
{
    return cycleStartTime;
}

void JAMScript::RIBScheduler::ShutDown()
{
    if (toContinue)
    {
        toContinue = false;
    }
}

void JAMScript::RIBScheduler::Disable(TaskInterface *toEnable)
{
    std::lock_guard<SpinMutex> lock(qMutexWithType[toEnable->taskType]);
    if (waitSet.find(reinterpret_cast<uintptr_t>(toEnable)) == waitSet.end())
    {
        waitSet.insert(*toEnable);
    }
}

void JAMScript::RIBScheduler::Enable(TaskInterface *toEnable)
{
    if (toEnable->taskType == INTERACTIVE_TASK_T)
    {
        std::lock_guard<SpinMutex> lock(qMutexWithType[INTERACTIVE_TASK_T]);
        if (waitSet.find(reinterpret_cast<uintptr_t>(toEnable)) != waitSet.end())
        {
            waitSet.erase(reinterpret_cast<uintptr_t>(toEnable));
        }
        if (toEnable->deadline - toEnable->burst + schedulerStartTime > Clock::now())
        {
            if (!toEnable->riStackHook.is_linked())
            {
                iCancelStack.push_front(*toEnable);
            }
        }
        else
        {
            if (!toEnable->riEdfHook.is_linked())
            {
                iEDFPriorityQueue.insert(*toEnable);
            }
        }
    }
    if (toEnable->taskType == BATCH_TASK_T)
    {
        std::lock_guard<SpinMutex> lock(qMutexWithType[BATCH_TASK_T]);
        if (waitSet.find(reinterpret_cast<uintptr_t>(toEnable)) != waitSet.end())
        {
            waitSet.erase(reinterpret_cast<uintptr_t>(toEnable));
        }
        if (!toEnable->rbQueueHook.is_linked())
        {
            bQueue.push_back(*toEnable);
        }
    }
}

uint32_t JAMScript::RIBScheduler::GetThiefSizes()
{
    uint32_t sz = 0;
    for (StealScheduler *ss : thiefs)
        sz += ss->Size();
    return sz;
}

void JAMScript::RIBScheduler::RunSchedulerMainLoop()
{
    schedulerStartTime = Clock::now();
    timer.UpdateTimeout();
    while (toContinue)
    {
        std::unique_lock<std::mutex> lScheduleReady(sReadyRTSchedule);
        while (rtScheduleGreedy.empty() || rtScheduleNormal.empty())
        {
            cvReadyRTSchedule.wait(lScheduleReady);
        }
        decider.NotifyChangeOfSchedule(rtScheduleNormal, rtScheduleGreedy);
        decltype(rtScheduleGreedy) currentSchedule;
        if (decider.DecideNextScheduleToRun())
        {
            if (remote == nullptr)
            {
                currentSchedule = rtScheduleNormal;
            }
            else
            {
                currentSchedule = std::move(rtScheduleNormal);
            }
        }
        else
        {
            if (remote == nullptr)
            {
                currentSchedule = rtScheduleGreedy;
            }
            else
            {
                currentSchedule = std::move(rtScheduleGreedy);
            }
        }
        lScheduleReady.unlock();
        cycleStartTime = Clock::now();
        for (auto &rtItem : currentSchedule)
        {
            auto cTime = Clock::now();
            timer.NotifyAllTimeouts();
            if (rtItem.sTime <= cTime - cycleStartTime && cTime - cycleStartTime <= rtItem.eTime)
            {
                std::unique_lock<SpinMutex> lockrt(qMutexWithType[REAL_TIME_TASK_T]);
                if (rtItem.taskId != 0 && rtRegisterTable.count(rtItem.taskId) > 0)
                {
                    auto currentRTIter = rtRegisterTable.find(rtItem.taskId);
                    lockrt.unlock();
                    eStats.jitters.push_back((cTime - cycleStartTime) - rtItem.sTime);
                    currentRTIter->SwapIn();
                    rtRegisterTable.erase_and_dispose(currentRTIter, [](TaskInterface *t) { delete t; });
                    while (Clock::now() - cycleStartTime < rtItem.eTime)
                        ;
                }
                else
                {
                    lockrt.unlock();
                    while (toContinue && Clock::now() - cycleStartTime <= rtItem.eTime)
                    {
                        timer.NotifyAllTimeouts();
                        cTime = Clock::now();
                        {
                            std::scoped_lock<SpinMutex, SpinMutex> lock2(qMutexWithType[0], qMutexWithType[1]);
                            if (bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty())
                            {
                                goto END_LOOP;
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
                        }
                    DECISION_BATCH:
                    {
                        std::unique_lock<SpinMutex> lock(qMutexWithType[BATCH_TASK_T]);
                        while (GetThiefSizes() < bQueue.size() + iEDFPriorityQueue.size() + iCancelStack.size() &&
                               bQueue.size() > 1 && bQueue.begin()->CanSteal())
                        {
                            auto *nSteal = &(*bQueue.begin());
                            bQueue.pop_front();
                            thiefs[cThief]->Steal(nSteal);
                            cThief = (cThief + 1) % thiefs.size();
                        }
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

                        vClockB += Clock::now() - tStart;
                        if (cBatchPtr->status == TASK_FINISHED)
                        {
                            delete cBatchPtr;
                        }
                        goto END_LOOP;
                    }
                    DECISION_INTERACTIVE:
                    {
                        std::unique_lock<SpinMutex> lock(qMutexWithType[INTERACTIVE_TASK_T]);
                        while (!iEDFPriorityQueue.empty() && iEDFPriorityQueue.top()->deadline -
                                                                     iEDFPriorityQueue.top()->burst +
                                                                     schedulerStartTime <
                                                                 cTime)
                        {
                            auto *pTop = &(*iEDFPriorityQueue.top());
                            iEDFPriorityQueue.erase(iEDFPriorityQueue.top());
                            if (pTop->CanSteal())
                            {
                                thiefs[cThief]->Steal(pTop);
                                cThief = (cThief + 1) % thiefs.size();
                            }
                            else
                            {
                                iCancelStack.push_front(*pTop);
                            }
                        }
                        while (iCancelStack.size() > 3)
                        {
                            iCancelStack.end()->onCancel();
                            iCancelStack.erase_and_dispose(iCancelStack.end(),
                                                           [](TaskInterface *x) { delete x; });
                        }
                        if (iEDFPriorityQueue.empty() && !iCancelStack.empty())
                        {
                            auto currentInteractiveIter = iCancelStack.begin();
                            auto *cIPtr = &(*currentInteractiveIter);
                            iCancelStack.erase(currentInteractiveIter);
                            lock.unlock();
                            auto tStart = Clock::now();
                            cIPtr->SwapIn();
                            auto tDelta = Clock::now() - tStart;
                            vClockI += tDelta;
                            cIPtr->burst -= tDelta;
                            if (cIPtr->status == TASK_FINISHED)
                                delete cIPtr;
                        }
                        else if (!iEDFPriorityQueue.empty())
                        {
                            auto currentInteractiveIter = iEDFPriorityQueue.top();
                            iEDFPriorityQueue.erase(currentInteractiveIter);
                            auto *cInteracPtr = &(*currentInteractiveIter);
                            lock.unlock();
                            auto tStart = Clock::now();
                            cInteracPtr->SwapIn();
                            auto tDelta = Clock::now() - tStart;
                            vClockI += tDelta;
                            cInteracPtr->burst -= tDelta;
                            if (cInteracPtr->status == TASK_FINISHED)
                                delete cInteracPtr;
                        }
                        goto END_LOOP;
                    }
                    END_LOOP:;
                    }
                    if (!toContinue)
                    {
                        return;
                    }
                }
            }
        }
        numberOfPeriods++;
#ifdef JAMSCRIPT_SCHED_AI_EXP
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
#endif
    }
}