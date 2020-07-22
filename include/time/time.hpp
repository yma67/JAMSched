#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <mutex>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include "timeout.h"
#include "concurrency/spinlock.hpp"

namespace JAMScript
{

    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::steady_clock::duration;

    class RIBScheduler;
    class TaskInterface;
    class Notifier;
    class SpinMutex;

    class Timer
    {
    public:

        friend class RIBScheduler;

        void operator()();
        void NotifyAllTimeouts();
        void UpdateTimeout();
        void SetTimeoutFor(TaskInterface *task, const Duration &dt);
        void SetTimeoutUntil(TaskInterface *task, const TimePoint &tp);
        void SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<JAMScript::SpinMutex> &iLock);
        void SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<JAMScript::SpinMutex> &iLock);

        Timer(RIBScheduler *scheduler);
        ~Timer();

    private:

        Timer() = delete;
        void UpdateTimeoutWithoutLock();
        static void TimeoutCallback(void *args);
        void SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask);
        void SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<JAMScript::SpinMutex> &iLock);

        struct timeouts *timingWheelPtr;
        RIBScheduler *scheduler;
        SpinMutex sl;

        Timer(Timer const &) = delete;
        Timer(Timer &&) = delete;
        Timer &operator=(Timer const &) = delete;
        Timer &operator=(Timer &&) = delete;

    };

} // namespace JAMScript
#endif