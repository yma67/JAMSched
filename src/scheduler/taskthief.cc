#include "scheduler/taskthief.hpp"
#include <algorithm>

JAMScript::StealScheduler::StealScheduler(SchedulerBase *victim, uint32_t ssz) : SchedulerBase(ssz), victim(victim)
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
    isWait.clear_and_dispose(dTaskInf);
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
    if (isWait.find(reinterpret_cast<uintptr_t>(toEnable)) != isWait.end())
    {
        isWait.erase(reinterpret_cast<uintptr_t>(toEnable));
    }
    if (!toEnable->trHook.is_linked())
    {
        isReady.push_back(*toEnable);
    }
    rCount++;
    cv.notify_all();
}

void JAMScript::StealScheduler::Disable(TaskInterface *toDisable)
{
    std::unique_lock<SpinMutex> lock(m);
    if (isWait.find(reinterpret_cast<uintptr_t>(toDisable)) == isWait.end())
    {
        isWait.insert(*toDisable);
    }
}

const uint32_t JAMScript::StealScheduler::Size() const
{
    return isWait.size() + isReady.size();
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
            while (rCount < 1 && toContinue)
            {
                cv.wait(lock);
            }
            if (!toContinue) 
            {
                break;
            }
            auto iterNext = isReady.begin();
            auto *pNext = &(*iterNext);
            isReady.erase(iterNext);
            rCount--;
            lock.unlock();
            pNext->SwapIn();
            if (pNext->status == TASK_FINISHED)
            {
                delete pNext;
            }
        }
    });
}