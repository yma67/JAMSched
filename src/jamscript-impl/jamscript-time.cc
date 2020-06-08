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
#include "jamscript-impl/jamscript-time.hh"

#include "future/future.h"
#include "jamscript-impl/jamscript-remote.hh"
#include "jamscript-impl/jamscript-scheduler.hh"

JAMScript::JAMTimer::JAMTimer(Scheduler* scheduler) : scheduler(scheduler) {
    int err;
    timingWheelPtr = timeouts_open(0, &err);
}

JAMScript::JAMTimer::~JAMTimer() { timeouts_close(timingWheelPtr); }

void JAMScript::JAMTimer::TimeoutCallback(void* args) {
    auto* f = static_cast<CFuture*>(args);
    f->status = ACK_FINISHED;
    NotifyFinishOfFuture(f);
}

void JAMScript::JAMTimer::SetContinueOnTimeout(CTask* task, uint64_t t_ns, bool isAbsolute) {
    CFuture* f = new CFuture;
    struct timeout* timeOut = new struct timeout;
    CreateFuture(f, task, nullptr, InteractiveTaskHandlePostCallback);
    timeOut = timeout_init(timeOut, isAbsolute);
    timeout_setcb(timeOut, TimeoutCallback, f);
    timeouts_add(timingWheelPtr, timeOut, t_ns);
    WaitForValueFromFuture(f);
    delete f;
    delete timeOut;
}

void JAMScript::JAMTimer::SetContinueOnTimeoutFor(CTask* task, uint64_t t_ns) {
    this->SetContinueOnTimeout(task, t_ns, 0);
}

void JAMScript::JAMTimer::SetContinueOnTimeoutUntil(CTask* task, uint64_t t_ns) {
    this->SetContinueOnTimeout(task, t_ns, TIMEOUT_ABS);
}

void JAMScript::JAMTimer::NotifyAllTimeouts() {
    struct timeout* timeOut;
    timeouts_update(timingWheelPtr, scheduler->GetCurrentTimepointInScheduler());
    while ((timeOut = timeouts_get(timingWheelPtr))) timeOut->callback.fn(timeOut->callback.arg);
}

void JAMScript::JAMTimer::ZeroTimeout() { timeouts_update(timingWheelPtr, 0); }

void JAMScript::JAMTimer::UpdateTimeout() {
    timeouts_update(timingWheelPtr, scheduler->GetCurrentTimepointInScheduler());
}