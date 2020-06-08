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
#include "future/future.h"

#define NUM_SPIN_ROUNDS 1000U

void EmptyFuncPostFutureCallback(CFuture* self) {}

// do not defer future using local stack variable as data entry if using xtask
int CreateFuture(CFuture* self, CTask* owner, void* data, void (*PostFutureCallback)(CFuture*)) {
    if (PostFutureCallback == NULL) {
        PostFutureCallback = EmptyFuncPostFutureCallback;
    }
    if (self == NULL || owner == NULL) {
        return 1;
    }
    self->data = data;
    self->ownerTask = owner;
    self->lockWord = TASK_READY;
    self->numberOfSpinRounds = NUM_SPIN_ROUNDS;
    self->PostFutureCallback = PostFutureCallback;
    return 0;
}

void WaitForValueFromFuture(CFuture* self) {
    if (__atomic_compare_exchange_n(&(self->ownerTask->taskStatus), &(self->lockWord), TASK_PENDING,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        while (__atomic_fetch_add(&(self->lockWord), 1, __ATOMIC_RELAXED) <
               self->numberOfSpinRounds)
            ;
        while (__atomic_load_n(&(self->lockWord), __ATOMIC_ACQUIRE) < 0x80000000) {
            YieldTask(self->ownerTask);
        }
    }
    __atomic_fetch_or(&(self->lockWord), 0x80000000, __ATOMIC_SEQ_CST);
}

void NotifyFinishOfFuture(CFuture* self) {
    if (__atomic_fetch_or(&(self->lockWord), 0x80000000, __ATOMIC_RELEASE) >=
        self->numberOfSpinRounds) {
        __atomic_store_n(&(self->ownerTask->taskStatus), TASK_READY, __ATOMIC_RELEASE);
        self->PostFutureCallback(self);
        return;
    }
    __atomic_store_n(&(self->ownerTask->taskStatus), TASK_READY, __ATOMIC_RELEASE);
}