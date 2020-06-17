#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <mutex>

#include "timeout.h"
namespace JAMScript {
    class RIBScheduler;
    class TaskInterface;
    class Notifier;
    class SpinLock;
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::high_resolution_clock::duration;
    class Timer {
    public:
        friend class RIBScheduler;
        Timer(RIBScheduler* scheduler);
        ~Timer();
        void NotifyAllTimeouts();
        void UpdateTimeout();
        void SetTimeoutFor(TaskInterface* task, Duration dt);
        void SetTimeoutUntil(TaskInterface* task, TimePoint tp);
        void SetTimeoutFor(TaskInterface* task, Duration dt, std::unique_lock<JAMScript::SpinLock>& iLock, Notifier* f);
        void SetTimeoutUntil(TaskInterface* task, TimePoint tp, std::unique_lock<JAMScript::SpinLock>& iLock,
                             Notifier* f);
        static void TimeoutCallback(void* args);

    protected:
        void SetTimeout(TaskInterface* task, Duration dt, uint32_t mask);
        void SetTimeout(TaskInterface* task, Duration dt, uint32_t mask, std::unique_lock<JAMScript::SpinLock>& iLock,
                        Notifier* f);
        Timer() = delete;
        struct timeouts* timingWheelPtr;
        RIBScheduler* scheduler;
        Timer(Timer const&) = delete;
        Timer(Timer&&) = delete;
        Timer& operator=(Timer const&) = default;
        Timer& operator=(Timer&&) = default;
    };
}  // namespace JAMScript
#endif