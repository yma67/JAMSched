#include "xtask/shared-stack-task.h"
#include <stdint.h>
#include <stdarg.h>

void* get_shared_stack_task_user_data(task_t* task) {
    return ((xuser_data_t*)(task->user_data))->user_data;
}

void  set_shared_stack_task_user_data(task_t* task, void* pudata) {
    ((xuser_data_t*)(task->user_data))->user_data = pudata;
}

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
    new_xstack->shared_stack_size = xstack_size;
    new_xstack->shared_stack_ptr = xstack_malloc(xstack_size);
    if (new_xstack->shared_stack_ptr == NULL) {
        xstack_free(new_xstack);
        return NULL;
    }
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
        xstack->shared_stack_memcpy(xstack->shared_stack_ptr + 
                                    xstack->shared_stack_size - 
                                    xdata->private_stack_size, 
                                    xdata->private_stack, 
                                    xdata->private_stack_size);
        context_switch(&xself->scheduler->scheduler_context, 
                       &xself->context);
    }
}

void shared_stack_task_yield(task_t* xself) {
    xuser_data_t* xdata = xself->user_data;
    shared_stack_t* xstack = xdata->shared_stack;
    char tosm = 'M';
    if ((uintptr_t)(&tosm) > (uintptr_t)xstack->shared_stack_ptr && 
        (uintptr_t)(&tosm) <= (uintptr_t)(xstack->shared_stack_size + 
                                          xstack->shared_stack_ptr)) {
        if (xdata->__private_stack_size < (uintptr_t)(
                                            xstack->shared_stack_ptr + 
                                            xstack->shared_stack_size - 
                                            (uint8_t*)&tosm)) {
            xstack->shared_stack_free(xdata->private_stack);
            xdata->__private_stack_size = xstack->shared_stack_ptr + 
                                          xstack->shared_stack_size - 
                                          (uint8_t*)&tosm;
            xdata->private_stack = xstack->shared_stack_malloc(
                                                xdata->__private_stack_size
                                           );
            if (xdata->private_stack == NULL) {
                xself->task_status = TASK_FINISHED;
                xself->return_value = ERROR_TASK_STACK_OVERFLOW;
                context_switch(&xself->context, &xself->scheduler->scheduler_context);
            }
        }
        xdata->private_stack_size = xstack->shared_stack_ptr + 
                                    xstack->shared_stack_size - 
                                    (uint8_t*)&tosm;
        xstack->shared_stack_memcpy(xdata->private_stack, &tosm, 
                                    xdata->private_stack_size);
    }
    context_switch(&xself->context, &xself->scheduler->scheduler_context);
}

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
              new_xdata->shared_stack->shared_stack_size, 
              new_xdata->shared_stack->shared_stack_ptr);
    task_bytes->user_data = new_xdata;
    task_bytes->resume_task = shared_stack_task_resume;
    task_bytes->yield_task = shared_stack_task_yield;
    task_bytes->get_user_data = get_shared_stack_task_user_data;
    task_bytes->set_user_data = set_shared_stack_task_user_data;
    return task_bytes;
}

void destroy_shared_stack_task(task_t* task) {
    xuser_data_t* xdata = task->user_data;
    xdata->shared_stack->shared_stack_free(xdata->private_stack);
    xdata->shared_stack->shared_stack_free(xdata);
    xdata->shared_stack->shared_stack_free(task);
}

void destroy_shared_stack(shared_stack_t* stack) {
    void (*free_fn)(void *) = stack->shared_stack_free;
    free_fn(stack->shared_stack_ptr);
    free_fn(stack);
}