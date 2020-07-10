#include "scheduler/taskthief.hpp"
#include "scheduler/scheduler.hpp"
#include <algorithm>

JAMScript::StealScheduler::StealScheduler(RIBScheduler *victim, uint32_t ssz) : SchedulerBase(ssz), victim(victim)
{}

JAMScript::StealScheduler::~StealScheduler()
{
    std::unique_lock lk(qMutex);
    if (toContinue)
    {
        toContinue = false;
    }
    cv.notify_all();
    lk.unlock();
    t.join();
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    isReady.clear_and_dispose(dTaskInf);
}

void JAMScript::StealScheduler::Steal(TaskInterface *toSteal)
{
    std::unique_lock lk(qMutex);
    toSteal->Steal(this);
    isReady.push_back(*toSteal);
    cv.notify_all();
    rCount++;
    iCount++;
}

size_t JAMScript::StealScheduler::StealFrom(StealScheduler *toSteal)
{
    std::scoped_lock sLock(qMutex, toSteal->qMutex);
    if (toSteal->iCount > 1) {
        auto toStealCount = toSteal->iCount / 2;
        auto supposeTo = toStealCount;
        auto itBatch = toSteal->isReady.begin();
        while (itBatch != toSteal->isReady.end() && toStealCount > 0)
        {
            if (itBatch->CanSteal())
            {
                auto *pNextSteal = &(*itBatch);
                pNextSteal->Steal(this);
                itBatch = toSteal->isReady.erase(itBatch);
                isReady.push_back(*pNextSteal);
                toStealCount--;
            }
            else
            {
                itBatch++;
            }
        }
        return supposeTo - toStealCount;
    }
    return 0;
}

void JAMScript::StealScheduler::Enable(TaskInterface *toEnable)
{
    std::unique_lock lk(qMutex);
    if (toEnable->wsHook.is_linked())
    {
        toEnable->wsHook.unlink();
    }
    if (!toEnable->trHook.is_linked())
    {
        isReady.push_back(*toEnable);
    }
    rCount++;
    toEnable->status = TASK_READY;
    cv.notify_all();
}

void JAMScript::StealScheduler::Disable(TaskInterface *toDisable)
{
    rCount--;
    toDisable->status = TASK_PENDING;
}

const uint32_t JAMScript::StealScheduler::Size() const
{
    return rCount;
}

void JAMScript::StealScheduler::ShutDown_()
{
}

void JAMScript::StealScheduler::ShutDown()
{
    victim->ShutDown();
}

void JAMScript::StealScheduler::RunSchedulerMainLoop()
{
    t = std::thread([this]() {
        while (toContinue)
        {
            victim->timer.NotifyAllTimeouts();
            std::unique_lock lock(qMutex);
            if (isReady.empty())
            {
                lock.unlock();
                // During time of Cluster Headache...
                size_t rStart = rand() % victim->thiefs.size();
                for (int T_T = 0; T_T < victim->thiefs.size(); T_T++) 
                {
                    if (victim->thiefs[(rStart + T_T) % victim->thiefs.size()] != this && 
                        StealFrom(victim->thiefs[(rStart + T_T) % victim->thiefs.size()])) 
                    {
                        break;
                    }
                }
                lock.lock();
            }
            while (isReady.empty() && toContinue)
            {
                cv.wait(lock);
            }
            if (!toContinue) 
            {
                break;
            }
            auto iterNext = isReady.begin();
            auto *pNext = &(*iterNext);
            isReady.pop_front();
            if (pNext->CanSteal()) 
            {
                pNext->isStealable = false;
                iCount--;
            }
            rCount--;
            lock.unlock();
            pNext->SwapIn();
            lock.lock();
            if (pNext->status == TASK_FINISHED)
            {
                delete pNext;
            }
        }
    });
}