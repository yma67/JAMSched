#include "scheduler/taskthief.hpp"
#include "scheduler/scheduler.hpp"
#include <algorithm>

JAMScript::StealScheduler::StealScheduler(RIBScheduler *victim, uint32_t ssz) : SchedulerBase(ssz), victim(victim)
{
    RunSchedulerMainLoop();
}

JAMScript::StealScheduler::~StealScheduler()
{
    std::unique_lock lk(m);
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
    std::unique_lock<SpinMutex> lock(m);
    toSteal->Steal(this);
    isReady.push_back(*toSteal);
    cv.notify_all();
    rCount++;
}

void JAMScript::StealScheduler::Enable(TaskInterface *toEnable)
{
    std::unique_lock<SpinMutex> lock(m);
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
            std::unique_lock<SpinMutex> lock(m);
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