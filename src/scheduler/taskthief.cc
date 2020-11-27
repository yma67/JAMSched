#include "scheduler/taskthief.hpp"
#include "scheduler/scheduler.hpp"
#include "io/iocp_wrapper.h"
#include <algorithm>

jamc::StealScheduler::StealScheduler(RIBScheduler *victim, uint32_t ssz) 
    : SchedulerBase(ssz), victim(victim), upCPUTime(0U), sizeOfQueue(0U), isRunning(true)
#if defined(__APPLE__) || defined(__linux__)
    ,evm(new IOCPAgent(this))
#endif
    {}

jamc::StealScheduler::~StealScheduler()
{
    auto dTaskInf = [](TaskInterface *t) { delete t; };
    isReady.clear_and_dispose(dTaskInf);
#if defined(__APPLE__) || defined(__linux__)
    delete evm;
#endif
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
    if (toEnable != nullptr)
    {
        std::scoped_lock lk(qMutex);
        if(toEnable->CanSteal() || toEnable->isStealable) sizeOfQueue++;
        isReady.push_front(*toEnable);
        toEnable->status = TASK_READY;
        cvQMutex.notify_all();
    }
}


void jamc::StealScheduler::Enable(TaskInterface *toEnable)
{
    if (toEnable != nullptr)
    {
        std::scoped_lock lk(qMutex);
        if(toEnable->CanSteal() || toEnable->isStealable) sizeOfQueue++;
        isReady.push_back(*toEnable);
        toEnable->status = TASK_READY;
        cvQMutex.notify_all();
    }
}

std::vector<jamc::TaskInterface *> jamc::StealScheduler::Steal()
{
    std::vector<TaskInterface *> tasksToSteal;
    std::scoped_lock sLock(qMutex);
    size_t stealableCount = 0;
    for (auto& task: isReady)
    {
        if (task.isStealable)
        {
            stealableCount++;
        }
    }
    if (stealableCount > 1) {
        auto toStealCount = std::min(stealableCount, isReady.size() / 2);
        auto itBatch = isReady.rbegin();
        while (itBatch != isReady.rend() && toStealCount > 0)
        {
            if (itBatch->isStealable)
            {
                auto *pNextSteal = &(*itBatch);
                isReady.erase(std::next(itBatch).base());
                sizeOfQueue--;
                tasksToSteal.push_back(pNextSteal);
                toStealCount--;
            }
            else
            {
                itBatch++;
            }
        }
    }
    return tasksToSteal;
}

size_t jamc::StealScheduler::StealFrom(StealScheduler *toSteal)
{
    if (toSteal != nullptr)
    {
        auto tasksToSteal = toSteal->Steal();
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
    return 0;
}

uint64_t jamc::StealScheduler::Size() const
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

void jamc::StealScheduler::RunSchedulerMainLoop()
{
    srand(time(nullptr));
    TaskInterface::ResetTaskInfos();
#if defined(__APPLE__) || defined(__linux__)
    auto* tIOManager = new StandAloneStackTask(this, 1024 * 256, [this]
    {
        while (true) this->evm->Run();
    });
    tIOManager->taskType = BATCH_TASK_T;
    tIOManager->isStealable = false;
    tIOManager->status = TASK_READY;
    tIOManager->enableImmediately = true;
    tIOManager->id = 0;
    tIOManager->ToggleNonSteal();
    tIOManager->Enable();
#endif
    while (toContinue)
    {
        auto starter = GetNextTask();
        if (starter != nullptr) {
            starter->SwapFrom(nullptr);
            TaskInterface::CleanupPreviousTask();
            TaskInterface::ResetTaskInfos();
        }
    }
#if defined(__APPLE__) || defined(__linux__)
    delete tIOManager;
#endif
}

jamc::TaskInterface *jamc::StealScheduler::GetNextTask() 
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
        return nullptr;
    }
    auto& pNext = isReady.front();
    isReady.pop_front();
    if(pNext.CanSteal() || pNext.isStealable) sizeOfQueue--;
    pNext.isStealable = false;
    pNext.status = TASK_RUNNING;
    return &pNext;
}

void jamc::StealScheduler::EndTask(TaskInterface *ptrCurrTask) 
{
    if (ptrCurrTask != nullptr)
    {
        if (ptrCurrTask->CanSteal())
        {
            ptrCurrTask->isStealable = true;
        }
        if (ptrCurrTask->status == TASK_FINISHED)
        {
            delete ptrCurrTask;
        }
    }
}
