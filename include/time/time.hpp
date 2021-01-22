#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <mutex>
#include <thread>
#include <cerrno>
#include <chrono>
#include <cstdint>

#include "timeout.h"
#include "concurrency/spinlock.hpp"

namespace jamc
{

    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::steady_clock::duration;

    class TaskInterface;
    class RIBScheduler;
    class IOCPAgent;
    class SpinMutex;
    class Notifier;
    class Mutex;

    class Timer
    {
    public:

        friend class RIBScheduler;

        void RunTimerLoop();
        void UpdateTimeout();
        void NotifyAllTimeouts();
        void RequestIO(int) const;
        void SetTimeoutFor(TaskInterface *task, const Duration &dt);
        void SetTimeoutUntil(TaskInterface *task, const TimePoint &tp);
        void SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<SpinMutex> &iLock);
        void SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<SpinMutex> &iLock);
        void SetTimeoutFor(TaskInterface *task, const Duration &dt, std::unique_lock<Mutex> &iLock);
        void SetTimeoutUntil(TaskInterface *task, const TimePoint &tp, std::unique_lock<Mutex> &iLock);
        
        static void SetGPUSampleRate(std::chrono::nanoseconds);
        static void SetSampleRate(std::chrono::nanoseconds);

        explicit Timer(RIBScheduler *scheduler);
        ~Timer();

    private:

        Timer() = delete;
        void UpdateTimeoutWithoutLock();
        void CancelTimeout(TaskInterface *task);
        static void TimeoutCallback(void *args);
        void SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask);
        void SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<SpinMutex> &iLock);
        void SetTimeout(TaskInterface *task, const Duration &dt, uint32_t mask, std::unique_lock<Mutex> &iLock);
        
        static std::chrono::nanoseconds kTimerSampleDelta, kTimerSampleDeltaGPU;
        struct timeouts *timingWheelPtr;
        RIBScheduler *scheduler;
        int kqFileDescriptor;
        SpinMutex sl;

        Timer(Timer const &) = delete;
        Timer(Timer &&) = delete;
        Timer &operator=(Timer const &) = delete;
        Timer &operator=(Timer &&) = delete;

    };

} // namespace jamc
#endif
