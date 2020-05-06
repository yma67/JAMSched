#ifndef TASK_H
#define TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "context.h"

typedef struct _scheduler_t scheduler_t;
typedef struct _task_t task_t;

typedef enum _task_status_t {
    TASK_READY = 0, 
    TASK_PENDING = 1, 
    TASK_FINISHED
} task_status_t;

typedef enum _task_return_t {
    SUCCESS_TASK,
    ERROR_CONTEXT_INIT, 
    ERROR_STACK_WRONGSIZE,
    ERROR_STACK_OVERFLOW, 
    ERROR_CONTEXT_SWITCH, 
    ERROR_WRONG_TYPE
} task_return_t;

struct _task_t {
    task_status_t task_status;
    scheduler_t* scheduler;
    ucontext_t context;
    unsigned int task_id;
    void (*task_function)(task_t*, void*);
    void* (*task_memset)(void *, int, size_t);
    void *task_args;
    void *user_data;
    int return_value; // undefined until call finish_task
    unsigned char *stack;
    unsigned int stack_size;
};

struct _scheduler_t {
    unsigned int task_id_counter;
    ucontext_t scheduler_context;
    task_t* (*next_task)();
    void (*idle_task)();
    void (*before_each)(task_t*);
    void (*after_each)(task_t*);
    void* (*scheduler_memset)(void *, int, size_t);
    int cont;
};

extern task_return_t make_task(task_t* task_bytes, scheduler_t* scheduler, void (*task_function)(task_t*, void*), void* (*task_memset)(void*, int, size_t), void* task_args, void* user_data, unsigned int stack_size, unsigned char* stack);
extern task_return_t make_scheduler(scheduler_t* scheduler_bytes, task_t* (*next_task)(), void (*idle_task)(), void (*before_each)(task_t*), void (*after_each)(task_t*), void* (*scheduler_memset)(void*, int, size_t));
extern task_return_t shutdown_scheduler(scheduler_t* scheduler);
extern task_return_t context_switch(ucontext_t* from, ucontext_t* to);
extern task_return_t yield_task(task_t* task, task_status_t status);
extern task_return_t finish_task(task_t* task, int return_value);
extern void scheduler_mainloop(scheduler_t* scheduler);
extern void empty_func_next_idle();
extern void empty_func_before_after(task_t* self);

#ifdef __cplusplus
}
#endif

#endif