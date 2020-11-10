#include <mutex>
#ifdef __APPLE__
#include <sys/event.h>
#endif
#include "time/time.hpp"
#include "boost/assert.hpp"
#include "io/iocp_wrapper.h"
#include "core/task/task.hpp"
#include "concurrency/mutex.hpp"
#include "scheduler/scheduler.hpp"
#include "concurrency/notifier.hpp"
#include "concurrency/spinlock.hpp"

constexpr std::chrono::nanoseconds kTimerSampleDelta(5000);

jamc::Timer::Timer(RIBScheduler *scheduler) : scheduler(scheduler)
#ifdef __APPLE__
, kqFileDescriptor(kqueue())
#endif
{
    int err;
    timingWheelPtr = timeouts_open(0, &err);
}

jamc::Timer::~Timer() 
{
    timeouts_update(timingWheelPtr, std::numeric_limits<uint64_t>::max());
    struct timeout *timeOut;
    while ((timeOut = timeouts_get(timingWheelPtr)))
    {
        delete static_cast<TaskInterface *>(timeOut->callback.arg);
    }
    timeouts_close(timingWheelPtr);
#ifdef __APPLE__
    close(kqFileDescriptor);
#endif
}

void jamc::Timer::RequestIO(int kqFD) const
{
    auto* t = TaskInterface::Active();
    if (t != nullptr)
    {
#ifdef __APPLE__
        struct kevent ev{};
        EV_SET(&ev, kqFD, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, t);
        kevent(kqFileDescriptor, &ev, 1, nullptr, 0, nullptr);
        t->SwapOut();
#endif
    }
}

void jamc::Timer::RunTimerLoop() 
{
    uint64_t printCount = 0;
    while (scheduler->toContinue.load())
    {
#ifdef __APPLE__
        constexpr std::size_t cEvent = 1024;
        struct kevent kev[cEvent];
        struct timespec timeout{};
        timeout.tv_sec = 0;
        timeout.tv_nsec = 5000;
        int n = kevent(kqFileDescriptor, nullptr, 0, kev, cEvent, &timeout);
        for (int i = 0; i < n; i++)
        {
            struct kevent & ev = kev[i];
            auto *t = static_cast<TaskInterface *>(ev.udata);
            if (ev.filter == EVFILT_READ)
            {
                t->Enable();
            }
        }
#else
        std::this_thread::sleep_for(kTimerSampleDelta);
#endif
        NotifyAllTimeouts();
#ifdef JAMSCRIPT_SHOW_EXECUTOR_COUNT
        if (printCount++ == 1000)
        {
            printCount = 0;
            std::cout << "sizes of executors ";
            for (auto& t: scheduler->thiefs)
            {
                std::cout << t->Size() << " ";
            }
            std::cout << std::endl;
        }
#endif
    }
}

void jamc::Timer::NotifyAllTimeouts()
{
    std::scoped_lock lk(sl);
    UpdateTimeoutWithoutLock();
    struct timeout *timeOut;
    while ((timeOut = timeouts_get(timingWheelPtr)))
    {
        timeOut->callback.fn(timeOut->callback.arg);
    }
}

void jamc::Timer::UpdateTimeout()
{
    std::lock_guard lk(sl);
    UpdateTimeoutWithoutLock();
}

void jamc::Timer::UpdateTimeoutWithoutLock()
{
    timeouts_update(timingWheelPtr, 
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - scheduler->GetSchedulerStartTime())
                    .count());
}

void jamc::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt) 
{ 
    SetTimeout(task, Clock::now() - scheduler->GetSchedulerStartTime() + dt, TIMEOUT_ABS);
}

void jamc::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS);
}

void jamc::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask)
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    task->cvStatus.store(0, std::memory_order_seq_cst);
    timeout_init(task->timeOut.get(), mask);
    timeout_setcb(task->timeOut.get(), TimeoutCallback, task);
    timeouts_add(timingWheelPtr, task->timeOut.get(), std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    task->SwapOut();
}

void jamc::Timer::TimeoutCallback(void *args)
{
    auto *t = static_cast<TaskInterface *>(args);
    auto cvWaitFlag = t->cvStatus.exchange(-2, std::memory_order_seq_cst);
    if (cvWaitFlag != static_cast<std::intptr_t>(-1))
    {
        t->EnableImmediately();
    }
}

void jamc::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<jamc::SpinMutex> &iLock)
{
    SetTimeout(task, Clock::now() - scheduler->GetSchedulerStartTime() + dt, TIMEOUT_ABS, iLock);
}

void jamc::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<jamc::SpinMutex> &iLock)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS, iLock);
}

void jamc::Timer::SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<Mutex> &iLock)
{
    SetTimeout(task, Clock::now() - scheduler->GetSchedulerStartTime() + dt, TIMEOUT_ABS, iLock);
}

void jamc::Timer::SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<Mutex> &iLock)
{
    SetTimeout(task, tp - scheduler->GetSchedulerStartTime(), TIMEOUT_ABS, iLock);
}

void jamc::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<SpinMutex> &iLock)
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    timeout_init(task->timeOut.get(), mask);
    timeout_setcb(task->timeOut.get(), TimeoutCallback, task);
    timeouts_add(timingWheelPtr, task->timeOut.get(), std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    iLock.unlock();
    task->SwapOut();
    lk.lock();
    if (!timeout_expired(task->timeOut.get())) jamscript_timeout_del(task->timeOut.get());
}

void jamc::Timer::SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<Mutex> &iLock)
{
    std::unique_lock lk(sl);
    UpdateTimeoutWithoutLock();
    timeout_init(task->timeOut.get(), mask);
    timeout_setcb(task->timeOut.get(), TimeoutCallback, task);
    timeouts_add(timingWheelPtr, task->timeOut.get(), std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    lk.unlock();
    iLock.unlock();
    task->SwapOut();
    lk.lock();
    if (!timeout_expired(task->timeOut.get())) jamscript_timeout_del(task->timeOut.get());
}