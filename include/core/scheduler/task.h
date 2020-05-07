#ifndef TASK_H
#define TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "context.h"

typedef struct _scheduler_t scheduler_t;
typedef struct _task_t task_t;

/**
 * @enum task_status_t
 * @brief State Machine for Task
 * @details upon a task established, it is READY
 * @details a task could be set to PENDING by user, either explicitly or using yield
 * @warning a PENDING task will not be executed, even if it is dispatched by scheduler
 * @details a task is finished when it declares it finishes, finished tasks will not 
 *          be executed
 *  
 * 
 */
typedef enum _task_status_t {
    TASK_READY = 0, 
    TASK_PENDING = 1, 
    TASK_FINISHED
} task_status_t;

typedef enum _task_return_t {
    SUCCESS_TASK,
    ERROR_TASK_CONTEXT_INIT, 
    ERROR_TASK_INVALID_ARGUMENT,
    ERROR_TASK_STACK_OVERFLOW, 
    ERROR_TASK_CONTEXT_SWITCH, 
    ERROR_TASK_WRONG_TYPE
} task_return_t;

struct _task_t {
    task_status_t task_status;
    scheduler_t* scheduler;
    jam_ucontext_t context;
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
    jam_ucontext_t scheduler_context;
    task_t* (*next_task)();
    void (*idle_task)();
    void (*before_each)(task_t*);
    void (*after_each)(task_t*);
    void* (*scheduler_memset)(void *, int, size_t);
    int cont;
};

/**
 * Task Initializer
 * @param task_bytes: memory allocated for a task,
 * @param scheduler: scheduler of the task
 * @param task_function: function to be executed as the task
 * @param task_args: argument that WILL be passed into task function along with task itself
 * @param user_data: argument that WILLNOT be passed into task function along with task itself, 
 * @param stack_size: size of coroutine/task stack
 * @param stack: pointer to stack allocated for coroutine/task
 * @warning due to the dependency injection nature of the framework, caller is responsible for 
 *          allocating and initializing memory with proper size and content, it is suggested to 
 *          be set to 0 using memset. this is valid on, but not limited to task_bytes and stack
 * @warning task_bytes, scheduler, task_function, stack should NOT be NULL
 * @warning not thread safe
 * @remark  user is responsible of parsing user_data and task_args
 * @remark  task_function is not executed atomically/transactionally
 * @remark  task is READY after function returns
 * @details sanity checks, setup task_t, initialize coroutine context, make task to be READY
 * @return  SUCCESS_TASK if success, otherwise ERROR_TASK_INVALID_ARGUMENT
 */
extern task_return_t make_task(task_t* task_bytes, scheduler_t* scheduler, 
                               void (*task_function)(task_t*, void*), 
                               void* task_args, void* user_data, 
                               unsigned int stack_size, unsigned char* stack);

/**
 * Scheduler Initializer
 * @param scheduler_bytes: memory allocated for a scheduler,
 * @param next_task: feed scheduler the next task to run
 * @param idle_task: activities to do if there is no task to run
 * @param before_each: activities to do before executing ANY task
 * @param after_each: activities to do after executing ANY task
 * @warning due to the dependency injection nature of the framework, caller is responsible for 
 *          allocating and initializing memory with proper size and content, it is suggested to 
 *          be set to 0 using memset. this is valid on, but not limited to scheduler_bytes
 * @warning next_task, idle_task, before_each, after_each should NOT be NULL
 * @warning not thread safe
 * @remark  before_each, after_each could be a setup/cleanup
 * @return  SUCCESS_TASK if success, otherwise ERROR_TASK_INVALID_ARGUMENT
 */
extern task_return_t make_scheduler(scheduler_t* scheduler_bytes, 
                                    task_t* (*next_task)(), 
                                    void (*idle_task)(), 
                                    void (*before_each)(task_t*), 
                                    void (*after_each)(task_t*));

/**
 * Shutdown Scheduler
 * @param scheduler: scheduler to be shut down
 * @warning schedluer may not be null
 * @warning this is not synchronized, the time of effect depends on task and all other functions
 * @warning this is not atomic and value of scheduler_t::cont may be unexpected due to DATA RACE
 * @details break the while loop of the scheduler for its NEXT cycle
 * @return  SUCCESS_TASK if success, otherwise ERROR_TASK_INVALID_ARGUMENT
 */
extern task_return_t shutdown_scheduler(scheduler_t* scheduler);

/**
 * Context Switcher
 * @param from: where current context to be saved
 * @param to: where next context is
 * @warning from, to may not be null, or memory corrupted
 * @warning hardware dependent, only tested for AMD64 and Linux
 * @return return value has no meaning
 */
extern task_return_t context_switch(jam_ucontext_t* from, jam_ucontext_t* to);

/**
 * Yield Task
 * @param task: task to give up its context
 * @param status: state of the task after context switching, could be READY or PENDING
 * @warning this is NOT an atomic operation, and it is subject to DATA RACE
 * @return not meaningful
 */
extern task_return_t yield_task(task_t* task, task_status_t status);

/**
 * Finish Task
 * @param task: task to declare a finish of task, and a task MUST declare a finish 
 * @param return_value: return value of the task function, could be retrieved later on
 * @warning: this write is NOT atomic, and may not be propergated to other cores in a 
 *           multiprocessor program
 * @warning: a finished task could not be restored
 * @return not meaningful
 */
extern task_return_t finish_task(task_t* task, int return_value);

/**
 * Scheduler Mainloop
 * @param scheduler: scheduler to start
 * @remark  this is the start of execution of scheduling framework where we proudly 
 *          accept your flow of execution until you order shutdown @related shutdown_scheduler
 * @details executes the following functions in order: before_each, task, after_each if there is 
 *          a task, otherwise, idle_task
 * @remark  these functions could fully cover all possible points of injecting codes into scheduler
 *          loop, and is running in context of SCHEDULER, not TASK
 * @remark  these functions are not atomic/executed transactionally
 * @remark  to enable a common teardown for idle_task and after_each, call a common function within
 *          idle_task and after_each
 * @return  void
 */
extern void scheduler_mainloop(scheduler_t* scheduler);

/**
 * function placeholder for next_task and idle_task
 * @param   ()
 * @details this function does nothing and returns nothing
 * @return  void
 */
extern void empty_func_next_idle();

/**
 * function placeholder for before_each and after_each
 * @param   self
 * @details this function does nothing and returns nothing
 * @return  void
 */
extern void empty_func_before_after(task_t* self);

#ifdef __cplusplus
}
#endif

#endif