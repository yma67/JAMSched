#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <mutex>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include "timeout.h"

namespace JAMScript {

    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::high_resolution_clock::duration;
    
    class RIBScheduler;
    class TaskInterface;
    class Notifier;
    class SpinLock;
    
    class Timer {
    public:

        friend class RIBScheduler;

        void NotifyAllTimeouts();
        void UpdateTimeout();
        void SetTimeoutFor(TaskInterface* task, Duration dt);
        void SetTimeoutUntil(TaskInterface* task, TimePoint tp);
        void SetTimeoutFor(TaskInterface* task, Duration dt, std::unique_lock<JAMScript::SpinLock>& iLock, Notifier* f);
        void SetTimeoutUntil(TaskInterface* task, TimePoint tp, std::unique_lock<JAMScript::SpinLock>& iLock,
                             Notifier* f);
        static void TimeoutCallback(void* args);

        Timer(RIBScheduler* scheduler);
        ~Timer();

    protected:

        void SetTimeout(TaskInterface* task, Duration dt, uint32_t mask);
        void SetTimeout(TaskInterface* task, Duration dt, uint32_t mask, std::unique_lock<JAMScript::SpinLock>& iLock,
                        Notifier* f);
        Timer() = delete;
        struct timeouts* timingWheelPtr;
        RIBScheduler* scheduler;
        Timer(Timer const&) = delete;
        Timer(Timer&&) = delete;
        Timer& operator=(Timer const&) = delete;
        Timer& operator=(Timer&&) = delete;

    };

}  // namespace JAMScript
#endif