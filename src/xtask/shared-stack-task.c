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
/// contains aligned stack from
///
/// Copyright 2018 Sen Han <00hnes@gmail.com>
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

#include "xtask/shared-stack-task.h"
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)(0))
#endif



void* get_shared_stack_task_user_data(task_t* task) {
    return ((xuser_data_t*)(task->user_data))->user_data;
}

void  set_shared_stack_task_user_data(task_t* task, void* pudata) {
    ((xuser_data_t*)(task->user_data))->user_data = pudata;
}

void _void_ret_func_xstack() { }

shared_stack_t* make_shared_stack(uint32_t xstack_size, 
                                  void *(*xstack_malloc)(size_t), 
                                  void  (*xstack_free)(void *), 
                                  void *(*xstack_memcpy)(void *, 
                                                         const void *, 
                                                         size_t)) {
    if (xstack_malloc == NULL || xstack_free == NULL || xstack_memcpy == NULL)
        return NULL;
    shared_stack_t* new_xstack = xstack_malloc(sizeof(shared_stack_t));
    if (new_xstack == NULL) return NULL;
    new_xstack->shared_stack_free = xstack_free;
    new_xstack->shared_stack_malloc = xstack_malloc;
    new_xstack->shared_stack_memcpy = xstack_memcpy;
    new_xstack->__shared_stack_size = xstack_size;
    new_xstack->__shared_stack_ptr = xstack_malloc(xstack_size);
    if (new_xstack->__shared_stack_ptr == NULL) {
        xstack_free(new_xstack);
        return NULL;
    }
    uintptr_t u_p = (uintptr_t)(new_xstack->__shared_stack_size - 
                    (sizeof(void*) << 1) + 
                    (uintptr_t)new_xstack->__shared_stack_ptr);
    u_p = (u_p >> 4) << 4;
    new_xstack->shared_stack_ptr_high_addr = (void*)u_p;
#ifdef __x86_64__
    new_xstack->shared_stack_ptr = (void*)(u_p - sizeof(void*));
    *((void**)(new_xstack->shared_stack_ptr)) = (void*)(_void_ret_func_xstack);
#elif defined(__aarch64__) || defined(__arm__)
    new_xstack->shared_stack_ptr = (void*)(u_p);
#else
#error "not supported"
#endif
    new_xstack->shared_stack_size = new_xstack->__shared_stack_size - 16 - 
                                    (sizeof(void*) << 1); 
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    new_xstack->v_stack_id = VALGRIND_STACK_REGISTER(
        new_xstack->__shared_stack_ptr, 
        (void*)((uintptr_t)new_xstack->__shared_stack_ptr + 
                new_xstack->__shared_stack_size)
    );
#endif
    new_xstack->is_allocatable = 1;
    return new_xstack;
}

xuser_data_t* make_xuser_data(void* original_udata, 
                              shared_stack_t* shared_stack) {
    xuser_data_t* user_data = shared_stack->shared_stack_malloc(
                                sizeof(xuser_data_t)
                              );
    if (user_data == NULL) {
        shared_stack->is_allocatable = 0;
        return NULL;
    }
    user_data->user_data = original_udata;
    user_data->private_stack = 0;
    user_data->private_stack_size = 0;
    user_data->__private_stack_size = 0;
    user_data->shared_stack = shared_stack;
    return user_data;
}

void shared_stack_task_resume(task_t* xself) {
    if (xself->task_status == TASK_READY) {
        xuser_data_t* xdata = xself->user_data;
        shared_stack_t* xstack = xdata->shared_stack;
        xstack->shared_stack_memcpy(xstack->shared_stack_ptr - 
                                    xdata->private_stack_size, 
                                    xdata->private_stack, 
                                    xdata->private_stack_size);
        swapcontext(&xself->scheduler->scheduler_context, 
                       &xself->context);
	return;
    }
}

void shared_stack_task_yield(task_t* xself) {
    xuser_data_t* xdata = xself->user_data;
    shared_stack_t* xstack = xdata->shared_stack;
    void* tos = NULL;
#ifdef __x86_64__
    asm("movq %%rsp, %0" : "=rm" (tos));
#elif defined(__aarch64__) || defined(__arm__)
    asm("mov %[tosp], sp" : [tosp] "=r" (tos));
#else
#error "not supported"
#endif
    if ((uintptr_t)(tos) <= (uintptr_t)xstack->shared_stack_ptr && 
        ((uintptr_t)(xstack->shared_stack_ptr_high_addr) - 
         (uintptr_t)(xstack->shared_stack_size)) <= (uintptr_t)(tos)) {
        xdata->private_stack_size = (uintptr_t)(xstack->shared_stack_ptr) - 
                                    (uintptr_t)(tos);
        if (xdata->__private_stack_size < xdata->private_stack_size) {
            xstack->shared_stack_free(xdata->private_stack);
            xdata->__private_stack_size = xdata->private_stack_size;
            xdata->private_stack = xstack->shared_stack_malloc(
                                                xdata->private_stack_size
                                           );
            if (xdata->private_stack == NULL) {
                xself->task_status = TASK_FINISHED;
                xself->return_value = ERROR_TASK_STACK_OVERFLOW;
                swapcontext(&xself->context, 
                               &xself->scheduler->scheduler_context);
            }
        }
        xstack->shared_stack_memcpy(xdata->private_stack, tos, 
                                    xdata->private_stack_size);
        swapcontext(&xself->context, &xself->scheduler->scheduler_context);
	return;
    }
}

task_fvt shared_stack_task_fv = {
    .resume_task   = shared_stack_task_resume,
    .yield_task_   = shared_stack_task_yield, 
    .get_user_data = get_shared_stack_task_user_data,
    .set_user_data = set_shared_stack_task_user_data
};

task_t* make_shared_stack_task(scheduler_t* scheduler, 
                               void (*task_function)(task_t*, void*), 
                               void* task_args, shared_stack_t* xstack) {
    if (xstack->is_allocatable == 0 || scheduler == NULL || 
        task_function == NULL) return NULL;
    task_t* task_bytes = xstack->shared_stack_malloc(sizeof(task_t));
    if (task_bytes == NULL) {
        xstack->is_allocatable = 0;
        return NULL;
    }
    xuser_data_t* new_xdata = make_xuser_data(NULL, xstack);
    if (new_xdata == NULL) {
        xstack->shared_stack_free(task_bytes);
        xstack->is_allocatable = 0;
        return NULL;
    }
    make_task(task_bytes, scheduler, task_function, task_args,
              new_xdata->shared_stack->__shared_stack_size, 
              new_xdata->shared_stack->__shared_stack_ptr);
    task_bytes->user_data = new_xdata;
    task_bytes->task_fv = &shared_stack_task_fv;
    return task_bytes;
}

void destroy_shared_stack_task(task_t* task) {
    xuser_data_t* xdata = task->user_data;
    xdata->shared_stack->shared_stack_free(task);
    xdata->shared_stack->shared_stack_free(xdata->private_stack);
    xdata->shared_stack->shared_stack_free(xdata);
}

void destroy_shared_stack(shared_stack_t* stack) {
    void (*free_fn)(void *) = stack->shared_stack_free;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(stack->v_stack_id);
#endif
    free_fn(stack->__shared_stack_ptr);
    free_fn(stack);
}
