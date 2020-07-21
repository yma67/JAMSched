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
    timeouts_update(timingWheelPtr, std::numeric_limits<uint64_t>::max());
    struct timeout *timeOut;
    while ((timeOut = timeouts_get(timingWheelPtr)))
        timeOut->callback.fn(timeOut->callback.arg);
    timeouts_close(timingWheelPtr);
}

void JAMScript::Timer::operator()() {
    while (scheduler->toContinue) {
        NotifyAllTimeouts();
        std::this_thread::sleep_for(std::chrono::nanoseconds(500));
    }
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

void JAMScript::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<JAMScript::SpinMutex> &iLock)
{
    SetTimeout(task, dt, 0, iLock);
}

void JAMScript::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<JAMScript::SpinMutex> &iLock)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS, iLock);
}

void JAMScript::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask,  std::unique_lock<JAMScript::SpinMutex> &iLock)
{
    std::unique_lock lk(sl);
    UpdateTimeout_();
    struct timeout *timeOut = new struct timeout;
    timeOut = timeout_init(timeOut, mask);
    timeout_setcb(timeOut, TimeoutCallback, task);
    timeouts_add(timingWheelPtr, timeOut, std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    iLock.unlock();
    task->SwapOut();
    iLock.lock();
    if (!timeout_expired(timeOut)) timeout_del(timeOut);
    delete timeOut;
}