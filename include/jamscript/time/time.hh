#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <cerrno>
#include <cstdint>
#include "timeout.h"
#include <core/scheduler/task.h>
#include "jamscript/remote/remote.hh"

namespace JAMScript {
    class Scheduler;
    class JAMTimer {
    public:
        friend class Scheduler;
        friend CTask* NextTaskJAMScriptImpl(CScheduler* selfCScheduler);
        JAMTimer(Scheduler* scheduler);
        ~JAMTimer();
        void NotifyAllTimeouts();
        void ZeroTimeout();
        void UpdateTimeout();
        void SetContinueOnTimeoutFor(CTask* task, uint64_t t_ns);
        void SetContinueOnTimeoutUntil(CTask* task, uint64_t t_ns);
        static void TimeoutCallback(void* args);

    protected:
        JAMTimer() = delete;
        struct timeouts* timingWheelPtr;
        Scheduler* scheduler;
        void SetContinueOnTimeout(CTask* task, uint64_t t_ns, bool isAbsolute);
        JAMTimer(JAMTimer const&) = delete;
        JAMTimer(JAMTimer&&) = delete;
        JAMTimer& operator=(JAMTimer const&) = default;
        JAMTimer& operator=(JAMTimer&&) = default;
    };
}  // namespace JAMScript
#endif