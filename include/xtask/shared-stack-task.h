#ifndef SHARED_STACK_TASK_H
#define SHARED_STACK_TASK_H
#ifdef __cplusplus
extern "C" {
#endif
#include "scheduler/task.h"
#include <stdint.h>

typedef struct _shared_stack_t {
    uint8_t* shared_stack_ptr;
    uint32_t shared_stack_size;
    void *(*shared_stack_malloc)(size_t);
    void  (*shared_stack_free)(void *);
    void *(*shared_stack_memcpy)(void *, const void *, size_t);
} shared_stack_t;

typedef struct _xuser_data_t {
    void* user_data;
    shared_stack_t* shared_stack;
    uint8_t* private_stack;
    uint32_t private_stack_size;
    uint32_t __private_stack_size;
} xuser_data_t;

extern task_t* make_shared_stack_task(scheduler_t* scheduler, 
                                      void (*task_function)(task_t*, void*), 
                                      void* task_args, void* user_data, 
                                      shared_stack_t* xstack);

extern shared_stack_t* make_shared_stack(uint32_t xstack_size, 
                                         void *(*xstack_malloc)(size_t), 
                                         void  (*xstack_free)(void *), 
                                         void *(*xstack_memcpy)(void *, 
                                                                const void *, 
                                                                size_t));
extern void destroy_shared_stack_task(task_t* task);
extern void destroy_shared_stack(shared_stack_t* stack);
#ifdef __cplusplus
}
#endif
#endif