#ifndef AWAIT_H
#define AWAIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <scheduler/task.h>

typedef struct _await_promise jamfuture_t;

struct _await_promise {

    uint32_t lock_word;
    void* data;
    task_t* owner_task;
    void (*post_future_callback)(jamfuture_t*);

};

void make_future(jamfuture_t*, task_t*, void*, void (*)(jamfuture_t*));
void get_future(jamfuture_t*);
void notify_future(jamfuture_t*);
void empty_func_post_future_callback(jamfuture_t*);

#ifdef __cplusplus
}
#endif
#endif