#include <mutex>
#include "time/time.hpp"
#include "boost/assert.hpp"
#include "core/task/task.hpp"
#include "concurrency/mutex.hpp"
#include "scheduler/scheduler.hpp"
#include "concurrency/notifier.hpp"
#include "concurrency/spinlock.hpp"

JAMScript::Timer::Timer(RIBScheduler *scheduler) : scheduler(scheduler)
{
    int err;
    timingWheelPtr = timeouts_open(0, &err);
}

JAMScript::Timer::~Timer()
{
    timeouts_update(timingWheelPtr, std::numeric_limits<uint64_t>::max());
    struct timeout *timeOut;
    while ((timeOut = timeouts_get(timingWheelPtr)))
    {
        timeOut->callback.fn(timeOut->callback.arg);
    }
    timeouts_close(timingWheelPtr);
}

void JAMScript::Timer::operator()() 
{
    while (scheduler->toContinue) 
    {
        NotifyAllTimeouts();
        std::this_thread::sleep_for(std::chrono::nanoseconds(500));
    }
}

void JAMScript::Timer::NotifyAllTimeouts()
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    struct timeout *timeOut;
    while ((timeOut = timeouts_get(timingWheelPtr)))
    {
        lk.unlock();
        timeOut->callback.fn(timeOut->callback.arg);
        lk.lock();
    }
}

void JAMScript::Timer::UpdateTimeout()
{
    std::lock_guard lk(sl);
    UpdateTimeoutWithoutLock();
}

void JAMScript::Timer::UpdateTimeoutWithoutLock()
{
    timeouts_update(timingWheelPtr, 
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - scheduler->GetSchedulerStartTime())
                    .count());
}

void JAMScript::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt) 
{ 
    SetTimeout(task, dt, 0); 
}

void JAMScript::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS);
}

void JAMScript::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask)
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    task->timeOut = timeout_init(task->timeOut, mask);
    timeout_setcb(task->timeOut, TimeoutCallback, task);
    timeouts_add(timingWheelPtr, task->timeOut, std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    BOOST_ASSERT_MSG(!task->trHook.is_linked(), "ready hook linked?");
    task->SwapOut();
}

void JAMScript::Timer::TimeoutCallback(void *args)
{
    auto *t = static_cast<TaskInterface *>(args);
    t->scheduler->Enable(t);
}

void JAMScript::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<JAMScript::SpinMutex> &iLock)
{
    SetTimeout(task, dt, 0, iLock);
}

void JAMScript::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<JAMScript::SpinMutex> &iLock)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS, iLock);
}

void JAMScript::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<Mutex> &iLock)
{
    SetTimeout(task, dt, 0, iLock);
}

void JAMScript::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<Mutex> &iLock)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS, iLock);
}

void JAMScript::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<SpinMutex> &iLock)
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    task->timeOut = timeout_init(task->timeOut, mask);
    timeout_setcb(task->timeOut, TimeoutCallback, task);
    timeouts_add(timingWheelPtr, task->timeOut, std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    iLock.unlock();
    BOOST_ASSERT_MSG(!task->trHook.is_linked(), "ready hook linked?");
    task->SwapOut();
    iLock.lock();
    if (!timeout_expired(task->timeOut)) timeout_del(task->timeOut);
}

void JAMScript::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<Mutex> &iLock)
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    task->timeOut = timeout_init(task->timeOut, mask);
    timeout_setcb(task->timeOut, TimeoutCallback, task);
    timeouts_add(timingWheelPtr, task->timeOut, std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    iLock.unlock();
    BOOST_ASSERT_MSG(!task->trHook.is_linked(), "ready hook linked?");
    task->SwapOut();
    iLock.lock();
    if (!timeout_expired(task->timeOut)) timeout_del(task->timeOut);
}