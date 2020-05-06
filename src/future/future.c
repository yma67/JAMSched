/**
    The MIT License (MIT)
    Copyright (c) 2017 Yuxiang Ma, Muthucumaru Maheswaran
    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to
    the following conditions:
    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY O9F ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "future.h"

#define NUM_SPIN_ROUNDS 1000U

void empty_func_post_future_callback(jamfuture_t* self) {}

void make_future(jamfuture_t* self, task_t* owner, void* data, void (*post_future_callback)(jamfuture_t*)) {
    self->data = data;
    self->owner_task = owner;
    self->lock_word = TASK_READY;
    self->post_future_callback = post_future_callback;
}

void get_future(jamfuture_t* self) {
    int exp = self->lock_word;
    if (__atomic_compare_exchange_n(&(self->owner_task->task_status), &(exp), TASK_PENDING, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        while (__atomic_fetch_add(&(self->lock_word), 1, __ATOMIC_RELAXED) < NUM_SPIN_ROUNDS);
        if (__atomic_load_n(&(self->lock_word), __ATOMIC_ACQUIRE) < 0x80000000) {
            context_switch(&(self->owner_task->context), &(self->owner_task->scheduler->scheduler_context));
        }
        __atomic_store_n(&(self->owner_task->task_status), TASK_READY, __ATOMIC_RELEASE);
    }
}

void notify_future(jamfuture_t* self) {
    if (__atomic_fetch_or(&(self->lock_word), 0x80000000, __ATOMIC_RELEASE) >= NUM_SPIN_ROUNDS) {
        __atomic_store_n(&(self->owner_task->task_status), TASK_READY, __ATOMIC_RELEASE);
        self->post_future_callback(self);
    }
    __atomic_store_n(&(self->owner_task->task_status), TASK_READY, __ATOMIC_RELEASE);
}