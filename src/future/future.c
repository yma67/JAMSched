#include "future/future.h"

#define NUM_SPIN_ROUNDS 1000U

void empty_func_post_future_callback(jamfuture_t* self) {}

// do not defer future using local stack variable as data entry if using xtask
void make_future(jamfuture_t* self, task_t* owner, void* data, 
                 void (*post_future_callback)(jamfuture_t*)) {
    if (post_future_callback == NULL) {
        post_future_callback = empty_func_post_future_callback;
    }
    self->data = data;
    self->owner_task = owner;
    self->lock_word = TASK_READY;
    self->post_future_callback = post_future_callback;
}

void get_future(jamfuture_t* self) {
    if (__atomic_compare_exchange_n(&(self->owner_task->task_status), 
                                    &(self->lock_word), TASK_PENDING, 
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        while (__atomic_fetch_add(&(self->lock_word), 1, 
                                    __ATOMIC_RELAXED) < NUM_SPIN_ROUNDS);
        if (__atomic_load_n(&(self->lock_word), 
                            __ATOMIC_ACQUIRE) < 0x80000000) {
            self->owner_task->yield_task(self->owner_task);
        }
    }
    __atomic_fetch_or(&(self->lock_word), 0x80000000, __ATOMIC_SEQ_CST);
}

void notify_future(jamfuture_t* self) {
    if (__atomic_fetch_or(&(self->lock_word), 0x80000000, 
                          __ATOMIC_RELEASE) >= NUM_SPIN_ROUNDS) {
        __atomic_store_n(&(self->owner_task->task_status), TASK_READY, 
                         __ATOMIC_RELEASE);
        self->post_future_callback(self);
    }
    __atomic_store_n(&(self->owner_task->task_status), TASK_READY, 
                     __ATOMIC_RELEASE);
}