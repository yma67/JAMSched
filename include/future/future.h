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