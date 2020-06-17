#include "scheduler/scheduler.h"

namespace JAMScript {
    void RIBScheduler::Enable(TaskInterface* toEnable) {
        if (toEnable->taskType == INTERACTIVE_TASK_T) {
            std::lock_guard<SpinLock> lock(qMutexWithType[INTERACTIVE_TASK_T]);
            if (toEnable->wiHook.is_linked()) {
                iWaitPool.erase(reinterpret_cast<uintptr_t>(toEnable));
            }
            if (toEnable->deadline - toEnable->burst + schedulerStartTime > Clock::now()) {
                if (!toEnable->riStackHook.is_linked()) {
                    iCancelStack.push_front(*toEnable);
                }
            } else {
                if (!toEnable->riEdfHook.is_linked()) {
                    iEDFPriorityQueue.insert(*toEnable);
                }
            }
        }
        if (toEnable->taskType == BATCH_TASK_T) {
            std::lock_guard<SpinLock> lock(qMutexWithType[BATCH_TASK_T]);
            if (toEnable->wbHook.is_linked()) {
                bWaitPool.erase(reinterpret_cast<uintptr_t>(toEnable));
            }
            if (!toEnable->rbQueueHook.is_linked()) {
                bQueue.push_back(*toEnable);
            }
        }
    }
    void RIBScheduler::Disable(TaskInterface* toDisable) {
        if (toDisable->taskType == INTERACTIVE_TASK_T) {
            std::lock_guard<SpinLock> lock(qMutexWithType[INTERACTIVE_TASK_T]);
            if (!toDisable->wiHook.is_linked()) {
                iWaitPool.insert(*toDisable);
            }
        }
        if (toDisable->taskType == BATCH_TASK_T) {
            std::lock_guard<SpinLock> lock(qMutexWithType[BATCH_TASK_T]);
            if (!toDisable->wbHook.is_linked()) {
                bWaitPool.insert(*toDisable);
            }
        }
    }
    void RIBScheduler::operator()() {
        schedulerStartTime = Clock::now();
        thief();
        timer.UpdateTimeout();
        while (toContinue) {
            std::unique_lock<std::mutex> lScheduleReady(sReadyRTSchedule);
            while (rtScheduleGreedy.empty() || rtScheduleNormal.empty()) {
                cvReadyRTSchedule.wait(lScheduleReady);
            }
            decider.NotifyChangeOfSchedule(rtScheduleNormal, rtScheduleGreedy);
            decltype(rtScheduleGreedy)* currentSchedule = nullptr;
            if (decider.DecideNextScheduleToRun()) {
                currentSchedule = &rtScheduleNormal;
            } else {
                currentSchedule = &rtScheduleGreedy;
            }
            lScheduleReady.unlock();
            cycleStartTime = Clock::now();
            for (auto& rtItem : *currentSchedule) {
                auto cTime = Clock::now();
                timer.NotifyAllTimeouts();
                if (rtItem.sTime <= cTime - cycleStartTime && cTime - cycleStartTime <= rtItem.eTime) {
                    std::unique_lock<SpinLock> lockrt(qMutexWithType[REAL_TIME_TASK_T]);
                    if (rtItem.taskId != 0 && rtRegisterTable.count(rtItem.taskId) > 0) {
                        auto currentRTIter = rtRegisterTable.find(rtItem.taskId);
                        lockrt.unlock();
                        eStats.jitters.push_back((cTime - cycleStartTime) - rtItem.sTime);
                        currentRTIter->SwapIn();
                        rtRegisterTable.erase_and_dispose(currentRTIter, [](TaskInterface* t) { delete t; });
                        while (Clock::now() - cycleStartTime < rtItem.eTime)
                            ;
                    } else {
                        lockrt.unlock();
                        while (toContinue && Clock::now() - cycleStartTime <= rtItem.eTime) {
                            timer.NotifyAllTimeouts();
                            cTime = Clock::now();
                            {
                                std::scoped_lock<SpinLock, SpinLock> lock2(qMutexWithType[0], qMutexWithType[1]);
                                if (bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty()) {
                                    goto END_LOOP;
                                } else if (!bQueue.empty() && iEDFPriorityQueue.empty() && iCancelStack.empty()) {
                                    goto DECISION_BATCH;
                                } else if (bQueue.empty() && (!iEDFPriorityQueue.empty() || !iCancelStack.empty())) {
                                    goto DECISION_INTERACTIVE;
                                } else {
                                    if (vClockB > vClockI)
                                        goto DECISION_INTERACTIVE;
                                    else
                                        goto DECISION_BATCH;
                                }
                            }
                        DECISION_BATCH : {
                            std::unique_lock<SpinLock> lock(qMutexWithType[BATCH_TASK_T]);
                            while (thief.Size() <
                                       bQueue.size() + bWaitPool.size() + iWaitPool.size() + iEDFPriorityQueue.size() &&
                                   bQueue.size() > 1 && bQueue.begin()->CanSteal()) {
                                auto* nSteal = &(*bQueue.begin());
                                bQueue.pop_front();
                                thief.Steal(nSteal);
                            }
                            if (bQueue.empty()) {
                                goto END_LOOP;
                            }
                            auto currentBatchIter = bQueue.begin();
                            auto* cBatchPtr = &(*currentBatchIter);
                            bQueue.erase(currentBatchIter);
                            lock.unlock();
                            auto tStart = Clock::now();
                            cBatchPtr->SwapIn();
                            vClockB += Clock::now() - tStart;
                            if (cBatchPtr->status == TASK_FINISHED)
                                delete cBatchPtr;
                            goto END_LOOP;
                        }
                        DECISION_INTERACTIVE : {
                            std::unique_lock<SpinLock> lock(qMutexWithType[INTERACTIVE_TASK_T]);
                            while (!iEDFPriorityQueue.empty() && iEDFPriorityQueue.top()->deadline -
                                                                         iEDFPriorityQueue.top()->burst +
                                                                         schedulerStartTime <
                                                                     cTime) {
                                auto* pTop = &(*iEDFPriorityQueue.top());
                                iEDFPriorityQueue.erase(iEDFPriorityQueue.top());
                                if (pTop->CanSteal()) {
                                    thief.Steal(pTop);
                                } else {
                                    iCancelStack.push_front(*pTop);
                                }
                            }
                            while (iCancelStack.size() > 3) {
                                iCancelStack.end()->onCancel();
                                iCancelStack.erase_and_dispose(iCancelStack.end(),
                                                               [](TaskInterface* x) { delete x; });
                            }
                            if (iEDFPriorityQueue.empty() && !iCancelStack.empty()) {
                                auto currentInteractiveIter = iCancelStack.begin();
                                auto* cIPtr = &(*currentInteractiveIter);
                                iCancelStack.erase(currentInteractiveIter);
                                lock.unlock();
                                auto tStart = Clock::now();
                                cIPtr->SwapIn();
                                auto tDelta = Clock::now() - tStart;
                                vClockI += tDelta;
                                cIPtr->burst -= tDelta;
                                if (cIPtr->status == TASK_FINISHED)
                                    delete cIPtr;
                            } else if (!iEDFPriorityQueue.empty()) {
                                auto currentInteractiveIter = iEDFPriorityQueue.top();
                                iEDFPriorityQueue.erase(currentInteractiveIter);
                                auto* cInteracPtr = &(*currentInteractiveIter);
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
                        if (!toContinue) {
                            return;
                        }
                    }
                }
            }
            numberOfPeriods++;
            // rtScheduleGreedy.clear();
            // rtScheduleNormal.clear();
#ifdef JAMSCRIPT_SCHED_AI_EXP
            std::cout << "Jitters: ";
            long tj = 0, nj = 0;
            for (auto& j : eStats.jitters) {
                std::cout << std::chrono::duration_cast<std::chrono::microseconds>(j).count() << " ";
                tj += std::chrono::duration_cast<std::chrono::microseconds>(j).count();
                nj++;
            }
            std::cout << "AVG: " << tj / nj << std::endl;
            eStats.jitters.clear();
#endif
        }
    }
}  // namespace JAMScript