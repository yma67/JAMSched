/// Copyright 2020 Yuxiang Ma, Muthucumaru Maheswaran 
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// P.S. Just want to try out a different coding style...
///
#include "jamscript-impl/jamscript-remote.hh"
#include "future/future.h"
#include "jamscript-impl/jamscript-scheduler.hh"
#include "jamscript-impl/jamscript-time.hh"

jamscript::JAMTimer::JAMTimer(c_side_scheduler* scheduler) : 
scheduler(scheduler) {
    int err;
    timingWheelPtr = timeouts_open(0, &err);
}

jamscript::JAMTimer::~JAMTimer() {
    timeouts_close(timingWheelPtr);
}

void
jamscript::JAMTimer::jamscript_timeout_callback(void *args) {
    auto* f = static_cast<jamfuture_t*>(args);
    f->status = ack_finished;
    notify_future(f);
}

void
jamscript::JAMTimer::
SetContinueOnTimeout(task_t* task, uint64_t t_ns, bool isAbsolute) {
    jamfuture_t* f = new jamfuture_t;
    struct timeout * timeOut = new struct timeout;
    make_future(f, task, nullptr, interactive_task_handle_post_callback);
    timeOut = timeout_init(timeOut, isAbsolute);
    timeout_setcb(timeOut, jamscript_timeout_callback, f);
    timeouts_add(timingWheelPtr, timeOut, t_ns);
    get_future(f);
    delete f;
    delete timeOut;
}

void 
jamscript::JAMTimer::
SetContinueOnTimeoutFor(task_t* task, uint64_t t_ns) {
    this->SetContinueOnTimeout(task, t_ns, 0);
}

void 
jamscript::JAMTimer::
SetContinueOnTimeoutUntil(task_t* task, uint64_t t_ns) {
    this->SetContinueOnTimeout(task, t_ns, TIMEOUT_ABS);
}

void jamscript::JAMTimer::NotifyAllTimeouts() {
    struct timeout *timeOut;
    timeouts_update(timingWheelPtr, 
                    scheduler->get_current_timepoint_in_scheduler());
    while ((timeOut = timeouts_get(timingWheelPtr))) 
        timeOut->callback.fn(timeOut->callback.arg);
}

void jamscript::JAMTimer::ZeroTimeout() {
    timeouts_update(timingWheelPtr, 0);
}

void jamscript::JAMTimer::UpdateTimeout() {
    timeouts_update(timingWheelPtr, 
                    scheduler->get_current_timepoint_in_scheduler());
}