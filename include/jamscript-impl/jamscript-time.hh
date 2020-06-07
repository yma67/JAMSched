#ifndef JAMSCRIPT_JAMSCRIPT_TIME_HH
#define JAMSCRIPT_JAMSCRIPT_TIME_HH
#include <cstdint>
#include <cerrno>
#include <core/scheduler/task.h>
#include "jamscript-impl/timeout.h"
#include "jamscript-impl/jamscript-remote.hh"

namespace jamscript {
class c_side_scheduler;
class JAMTimer {
public:
    friend class c_side_scheduler;
    friend task_t* jamscript::next_task_jam_impl(scheduler_t *self_c);
    JAMTimer(c_side_scheduler* scheduler);
    ~JAMTimer();
    void NotifyAllTimeouts();
    void ZeroTimeout();
    void UpdateTimeout();
    void SetContinueOnTimeoutFor(task_t* task, uint64_t t_ns);
    void SetContinueOnTimeoutUntil(task_t* task, uint64_t t_ns);
    static void jamscript_timeout_callback(void *args);
protected:
    JAMTimer() = delete;
    struct timeouts *timingWheelPtr;
    c_side_scheduler* scheduler;
    void SetContinueOnTimeout(task_t* task, uint64_t t_ns, bool isAbsolute);
    JAMTimer (const JAMTimer &) = delete;
    JAMTimer & operator = (const JAMTimer &) = delete;
};
}
#endif