#include "scheduler/taskthief.hpp"
#include "scheduler/scheduler.hpp"
#include <algorithm>
#include <sys/time.h>
#include <sys/resource.h>

JAMScript::StealScheduler::StealScheduler(RIBScheduler *victim, uint32_t ssz) 
    : SchedulerBase(ssz), victim(victim), upCPUTime(0U), sizeOfQueue(0U) {}

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
    sizeOfQueue++;
}

size_t JAMScript::StealScheduler::StealFrom(StealScheduler *toSteal)
{
    std::scoped_lock sLock(qMutex, toSteal->qMutex);
    unsigned long stealableCount = 0;
    for (auto& task: toSteal->isReady)
    {
        if (task.isStealable)
        {
            stealableCount++;
        }
    }
    if (stealableCount > 1) {
        auto nThiefs = victim->thiefs.size();
        auto toStealCount = std::max(stealableCount * ((nThiefs - 1) / nThiefs), 1UL);;
        auto supposeTo = toStealCount;
        auto itBatch = toSteal->isReady.begin();
        while (itBatch != toSteal->isReady.end() && toStealCount > 0)
        {
            if (itBatch->isStealable)
            {
                auto *pNextSteal = &(*itBatch);
                pNextSteal->Steal(this);
                toSteal->sizeOfQueue--;
                sizeOfQueue++;
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
    sizeOfQueue++;
}

void JAMScript::StealScheduler::Disable(TaskInterface *toDisable)
{
    //std::unique_lock lk(qMutex);
    toDisable->status = TASK_PENDING;
    sizeOfQueue--;
}

const uint64_t JAMScript::StealScheduler::Size() const
{
    // std::unique_lock lk(qMutex);
    return sizeOfQueue;
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

void JAMScript::StealScheduler::PostCoreUsage()
{
    struct rusage Ru;
    getrusage(RUSAGE_THREAD, &Ru);
    upCPUTime.store(Ru.ru_utime.tv_usec + Ru.ru_stime.tv_usec);
}

void JAMScript::StealScheduler::RunSchedulerMainLoop()
{
    while (toContinue)
    {
        std::unique_lock lock(qMutex);
        while (isReady.empty() && toContinue)
        {
            for (int retryStealCount = 0; retryStealCount < 2 && isReady.empty(); retryStealCount++)
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
                            if ((numStolen > 1)) printf("stolen: %lu\n", numStolen);
                            break;
                        }
                    }
                }
                lock.lock();
            }
            if (!isReady.empty())
            {
                break;
            }
            // PostCoreUsage();
            cvQMutex.wait(lock);
            // cvQMutex.wait_for(lock, std::chrono::microseconds(10));
        }
        if (!toContinue) 
        {
            break;
        }
        while (!isReady.empty())
        {
            auto iterNext = isReady.begin();
            auto *pNext = &(*iterNext);
            isReady.pop_front();
            pNext->isStealable = false;
            pNext->status = TASK_RUNNING;
            lock.unlock();
            pNext->SwapIn();
            sizeOfQueue--;
            //PostCoreUsage();
            lock.lock();
            if (pNext->status == TASK_FINISHED)
            {
                delete pNext;
            }
        }
    }
}