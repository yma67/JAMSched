#include "scheduler/taskthief.hpp"
#include "scheduler/scheduler.hpp"
#include <algorithm>

JAMScript::StealScheduler::StealScheduler(RIBScheduler *victim, uint32_t ssz) 
    : SchedulerBase(ssz), victim(victim) {}

JAMScript::StealScheduler::~StealScheduler()
{
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    isReady.clear_and_dispose(dTaskInf);
}

void JAMScript::StealScheduler::StopSchedulerMainLoop()
{
    std::unique_lock lk(qMutex);
    if (toContinue)
    {
        toContinue = false;
    }
    cvQMutex.notify_all();
    lk.unlock();
}

void JAMScript::StealScheduler::Steal(TaskInterface *toSteal)
{
    std::unique_lock lk(qMutex);
    toSteal->Steal(this);
    isReady.push_back(*toSteal);
    cvQMutex.notify_one();
}

size_t JAMScript::StealScheduler::StealFrom(StealScheduler *toSteal)
{
    std::scoped_lock sLock(qMutex, toSteal->qMutex);
    int stealableCount = 0;
    for (auto& task: toSteal->isReady)
    {
        if (task.isStealable)
        {
            stealableCount++;
        }
    }
    if (stealableCount > 1) {
        auto toStealCount = std::max(toSteal->isReady.size() * ((toSteal->isReady.size() - 1) / toSteal->isReady.size()), 1UL);;
        auto supposeTo = toStealCount;
        auto itBatch = toSteal->isReady.begin();
        while (itBatch != toSteal->isReady.end() && toStealCount > 0)
        {
            if (itBatch->isStealable)
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
    BOOST_ASSERT_MSG(!toEnable->trHook.is_linked(), "Should not duplicate ready worksteal");
    isReady.push_back(*toEnable);
    if (toEnable->CanSteal())
    {
        toEnable->isStealable = true;
    }
    toEnable->status = TASK_READY;
    cvQMutex.notify_one();
}

void JAMScript::StealScheduler::Disable(TaskInterface *toDisable)
{
    std::unique_lock lk(qMutex);
    toDisable->status = TASK_PENDING;
}

const uint32_t JAMScript::StealScheduler::Size() const
{
    std::unique_lock lk(qMutex);
    return isReady.size();
}

void JAMScript::StealScheduler::ShutDown()
{
    victim->ShutDown();
}

JAMScript::TimePoint JAMScript::StealScheduler::GetSchedulerStartTime() const
{
    return victim->GetSchedulerStartTime();
}

JAMScript::TimePoint JAMScript::StealScheduler::GetCycleStartTime() const
{
    return victim->GetCycleStartTime();
}

void JAMScript::StealScheduler::SleepFor(TaskInterface* task, const Duration &dt) 
{
    return victim->SleepFor(task, dt);
}

void JAMScript::StealScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp) 
{
    return victim->SleepUntil(task, tp);
}

void JAMScript::StealScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) 
{
    return victim->SleepFor(task, dt, lk);
}

void JAMScript::StealScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) 
{
    return victim->SleepUntil(task, tp, lk);
}

void JAMScript::StealScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) 
{
    return victim->SleepFor(task, dt, lk);
}

void JAMScript::StealScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) 
{
    return victim->SleepUntil(task, tp, lk);
}

void JAMScript::StealScheduler::RunSchedulerMainLoop()
{
    while (toContinue)
    {
        std::unique_lock lock(qMutex);
        if (isReady.empty())
        {
            lock.unlock();
            // During time of Trigeminal Neuralgia...
            size_t rStart = rand() % victim->thiefs.size();
            for (int T_T = 0; T_T < victim->thiefs.size(); T_T++) 
            {
                auto* pVictim = victim->thiefs[(rStart - T_T + victim->thiefs.size()) % victim->thiefs.size()].get();
                if (pVictim != this)
                {
                    auto numStolen = StealFrom(pVictim);
                    if ((numStolen > 0) || !isReady.empty())
                    {
                        if ((numStolen > 0)) printf("stolen: %lu\n", numStolen);
                        break;
                    }
                }
            }
            lock.lock();
        }
        while (isReady.empty() && toContinue)
        {
            cvQMutex.wait(lock);
        }
        if (!toContinue) 
        {
            break;
        }
        auto iterNext = isReady.begin();
        auto *pNext = &(*iterNext);
        isReady.pop_front();
        pNext->isStealable = false;
        pNext->status = TASK_RUNNING;
        lock.unlock();
        pNext->SwapIn();
        lock.lock();
        if (pNext->status == TASK_FINISHED)
        {
            delete pNext;
        }
    }
}