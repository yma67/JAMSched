#include "scheduler/taskthief.hpp"
#include "scheduler/scheduler.hpp"
#include <algorithm>
#include <sys/time.h>
#include <sys/resource.h>

jamc::StealScheduler::StealScheduler(RIBScheduler *victim, uint32_t ssz) 
    : SchedulerBase(ssz), victim(victim), upCPUTime(0U), sizeOfQueue(0U) {}

jamc::StealScheduler::~StealScheduler()
{
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    isReady.clear_and_dispose(dTaskInf);
}

void jamc::StealScheduler::StopSchedulerMainLoop()
{
    std::unique_lock lk(qMutex);
    if (toContinue)
    {
        toContinue = false;
    }
    cvQMutex.notify_all();
    lk.unlock();
}

void jamc::StealScheduler::EnableImmediately(TaskInterface *toEnable)
{
    std::scoped_lock lk(qMutex);
    BOOST_ASSERT_MSG(!toEnable->trHook.is_linked(), "Should not duplicate ready worksteal");
    sizeOfQueue++;
    isReady.push_front(*toEnable);
    toEnable->status = TASK_READY;
    cvQMutex.notify_one();
}


void jamc::StealScheduler::Enable(TaskInterface *toEnable)
{
    std::scoped_lock lk(qMutex);
    BOOST_ASSERT_MSG(!toEnable->trHook.is_linked(), "Should not duplicate ready worksteal");
    sizeOfQueue++;
    isReady.push_back(*toEnable);
    toEnable->status = TASK_READY;
    cvQMutex.notify_one();
}

size_t jamc::StealScheduler::StealFrom(StealScheduler *toSteal)
{
    std::vector<TaskInterface *> tasksToSteal;
    {
        std::scoped_lock sLock(toSteal->qMutex);
        size_t stealableCount = 0;
        for (auto& task: toSteal->isReady)
        {
            if (task.isStealable)
            {
                stealableCount++;
            }
        }
        if (stealableCount > 1) {
            auto nThiefs = victim->thiefs.size();
            auto toStealCount = std::min(stealableCount, toSteal->isReady.size() / 2);
            auto supposeTo = toStealCount;
            auto itBatch = toSteal->isReady.rbegin();
            while (itBatch != toSteal->isReady.rend() && toStealCount > 0)
            {
                if (itBatch->isStealable)
                {
                    auto *pNextSteal = &(*itBatch);
                    toSteal->isReady.erase(std::next(itBatch).base());
                    toSteal->sizeOfQueue--;
                    sizeOfQueue++;
                    tasksToSteal.push_back(pNextSteal);
                    toStealCount--;
                }
                else
                {
                    itBatch++;
                }
            }
        }
    }
    {
        std::scoped_lock sLock(qMutex);
        for (auto tsk: tasksToSteal)
        {
            tsk->Steal(this);
            sizeOfQueue++;
            isReady.push_front(*tsk);
        }
    }
    return tasksToSteal.size();
}

const uint64_t jamc::StealScheduler::Size() const
{
    return sizeOfQueue;
}

void jamc::StealScheduler::ShutDown()
{
    victim->ShutDown();
}

jamc::TimePoint jamc::StealScheduler::GetSchedulerStartTime() const
{
    return victim->GetSchedulerStartTime();
}

jamc::TimePoint jamc::StealScheduler::GetCycleStartTime() const
{
    return victim->GetCycleStartTime();
}

void jamc::StealScheduler::SleepFor(TaskInterface* task, const Duration &dt) 
{
    return victim->SleepFor(task, dt);
}

void jamc::StealScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp) 
{
    return victim->SleepUntil(task, tp);
}

void jamc::StealScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<SpinMutex> &lk) 
{
    return victim->SleepFor(task, dt, lk);
}

void jamc::StealScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<SpinMutex> &lk) 
{
    return victim->SleepUntil(task, tp, lk);
}

void jamc::StealScheduler::SleepFor(TaskInterface* task, const Duration &dt, std::unique_lock<Mutex> &lk) 
{
    return victim->SleepFor(task, dt, lk);
}

void jamc::StealScheduler::SleepUntil(TaskInterface* task, const TimePoint &tp, std::unique_lock<Mutex> &lk) 
{
    return victim->SleepUntil(task, tp, lk);
}

void jamc::StealScheduler::PostCoreUsage()
{
    struct rusage Ru;
    getrusage(RUSAGE_THREAD, &Ru);
    upCPUTime.store(Ru.ru_utime.tv_usec + Ru.ru_stime.tv_usec);
}

void jamc::StealScheduler::RunSchedulerMainLoop()
{
    srand(time(nullptr));
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
                    if (pVictim != this && StealFrom(pVictim) > 0)
                    {
                        break;
                    }
                }
                lock.lock();
            }
            if (!isReady.empty() || !toContinue)
            {
                break;
            }
            cvQMutex.wait(lock);
        }
        if (!toContinue) 
        {
            break;
        }
        while (!isReady.empty() && toContinue)
        {
            auto& pNext = isReady.front();
            isReady.pop_front();
            sizeOfQueue--;
            pNext.isStealable = false;
            pNext.status = TASK_RUNNING;
            lock.unlock();
            pNext.SwapIn();
            lock.lock();
            if (pNext.CanSteal()) 
            {
                pNext.isStealable = true;
            }
            if (pNext.status == TASK_FINISHED)
            {
                delete &(pNext);
            }
        }
    }
}