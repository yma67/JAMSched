#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <cstdint>
#include <cerrno>
#include <core/scheduler/task.h>
#include "jamscript-impl/timeout.h"
#include "jamscript-impl/jamscript-remote.hh"

namespace JAMScript {
class Scheduler;
class JAMTimer {
public:
    friend class Scheduler;
    friend CTask* NextTaskJAMScriptImpl(CScheduler *self_c);
    JAMTimer(Scheduler* scheduler);
    ~JAMTimer();
    void NotifyAllTimeouts();
    void ZeroTimeout();
    void UpdateTimeout();
    void SetContinueOnTimeoutFor(CTask* task, uint64_t t_ns);
    void SetContinueOnTimeoutUntil(CTask* task, uint64_t t_ns);
    static void TimeoutCallback(void *args);
protected:
    JAMTimer() = delete;
    struct timeouts *timingWheelPtr;
    Scheduler* scheduler;
    void SetContinueOnTimeout(CTask* task, uint64_t t_ns, bool isAbsolute);
    JAMTimer (const JAMTimer &) = delete;
    JAMTimer & operator = (const JAMTimer &) = delete;
};
}
#endif