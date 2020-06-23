#include <mutex>
#include "time/time.hpp"
#include "core/task/task.hpp"
#include "concurrency/notifier.hpp"
#include "concurrency/spinlock.hpp"
#include "scheduler/scheduler.hpp"

JAMScript::Timer::Timer(RIBScheduler *scheduler) : scheduler(scheduler)
{
    int err;
    timingWheelPtr = timeouts_open(0, &err);
}

JAMScript::Timer::~Timer()
{
    timeouts_close(timingWheelPtr);
}

void JAMScript::Timer::NotifyAllTimeouts()
{
    std::lock_guard lk(sl);
    UpdateTimeout_();
    struct timeout *timeOut;
    while ((timeOut = timeouts_get(timingWheelPtr)))
        timeOut->callback.fn(timeOut->callback.arg);
}

void JAMScript::Timer::UpdateTimeout()
{
    std::lock_guard lk(sl);
    UpdateTimeout_();
}

void JAMScript::Timer::UpdateTimeout_()
{
    timeouts_update(timingWheelPtr, std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        Clock::now() - scheduler->GetSchedulerStartTime())
                                        .count());
}

void JAMScript::Timer::SetTimeoutFor(TaskInterface *task, Duration dt) { SetTimeout(task, dt, 0); }

void JAMScript::Timer::SetTimeoutUntil(TaskInterface *task, TimePoint tp)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS);
}

void JAMScript::Timer::SetTimeout(TaskInterface *task, Duration dt, uint32_t mask)
{
    std::unique_lock lk(sl);
    UpdateTimeout_();
    struct timeout *timeOut = new struct timeout;
    timeOut = timeout_init(timeOut, mask);
    timeout_setcb(timeOut, TimeoutCallback, task);
    timeouts_add(timingWheelPtr, timeOut, std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    task->SwapOut();
    delete timeOut;
}

void JAMScript::Timer::TimeoutCallback(void *args)
{
    auto *t = static_cast<TaskInterface *>(args);
    t->scheduler->Enable(t);
}

void JAMScript::Timer::SetTimeoutFor(TaskInterface *task, Duration dt, std::unique_lock<JAMScript::SpinMutex> &iLock,
                                     TaskInterface *f)
{
    SetTimeout(task, dt, 0, iLock, f);
}

void JAMScript::Timer::SetTimeoutUntil(TaskInterface *task, TimePoint tp, std::unique_lock<JAMScript::SpinMutex> &iLock,
                                       TaskInterface *f)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS, iLock, f);
}

void JAMScript::Timer::SetTimeout(TaskInterface *task, Duration dt, uint32_t mask,
                                  std::unique_lock<JAMScript::SpinMutex> &iLock, TaskInterface *f)
{
    std::unique_lock lk(sl);
    UpdateTimeout_();
    struct timeout *timeOut = new struct timeout;
    timeOut = timeout_init(timeOut, mask);
    timeout_setcb(timeOut, TimeoutCallback, f);
    timeouts_add(timingWheelPtr, timeOut, std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    iLock.unlock();
    f->SwapOut();
    iLock.lock();
    if (!timeout_expired(timeOut)) timeout_del(timeOut);
    delete timeOut;
}