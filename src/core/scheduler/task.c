#include "scheduler/task.h"


#define TASK_STACK_MIN 256
#define NULL ((void *)0)

void start_task(unsigned int task_addr_lower, unsigned int task_addr_upper) {
	task_t *task = (task_t*)(task_addr_lower | (((unsigned long)task_addr_upper << 16) << 16));
	task->task_function(task, task->task_args);
	task->task_status = TASK_FINISHED;
}

task_return_t make_task(task_t* task_bytes, scheduler_t* scheduler, void (*task_function)(task_t*, void*), void* (*task_memset)(void*, int, size_t), void* task_args, void* user_data, unsigned int stack_size) {
    // init task injected
    if (stack_size < TASK_STACK_MIN) {
        return ERROR_STACK_WRONGSIZE;
    }
    task_memset(task_bytes, 0, sizeof(task_t) + stack_size);
    task_bytes->task_memset = task_memset;
    task_bytes->stack_size = stack_size;
    task_bytes->task_id = scheduler->task_id_counter;
    task_bytes->task_function = task_function;
    task_bytes->task_args = task_args;
    task_bytes->scheduler = scheduler;
    task_bytes->stack = (unsigned char*)(task_bytes + 1);
    scheduler->task_id_counter = scheduler->task_id_counter + 1;
    task_bytes->user_data = user_data;
    // init context
    task_bytes->task_memset(&(task_bytes->context), 0, sizeof(ucontext_t));
    if (getcontext(&(task_bytes->context)) < 0)
		return ERROR_CONTEXT_INIT;
    task_bytes->context.uc_stack.ss_sp = &(task_bytes->stack[0]) + 8;
    task_bytes->context.uc_stack.ss_size = task_bytes->stack_size - 64;
	
#if defined(__sun__) && !defined(__MAKECONTEXT_V2_SOURCE)
#warning "doing sun thing"
	/* can avoid this with __MAKECONTEXT_V2_SOURCE but only on SunOS 5.9 */
    task_bytes->context.uc_stack.ss_sp = (char*)task_bytes->context.uc_stack.ss_sp + task_bytes->context.uc_stack.ss_size;
#endif
    makecontext(&task_bytes->context, (void(*)())start_task, 2, (unsigned int)((unsigned long)task_bytes), (unsigned int)((unsigned long)task_bytes >> 32));
    task_bytes->task_status = TASK_READY;
    return SUCCESS_TASK;
}


task_return_t context_switch(ucontext_t* from, ucontext_t* to) {
	if (swapcontext(from, to) < 0) 
		return ERROR_CONTEXT_SWITCH;
    return SUCCESS_TASK;
}

task_return_t switch_task(task_t* task) {
	if ((char*)(&task) <= (char*)(task->stack) || (char*)(&task) - (char*)(task->stack) < TASK_STACK_MIN)
		return ERROR_STACK_OVERFLOW;
	return context_switch(&task->context, &task->scheduler->scheduler_context);
}

task_return_t yield_task(task_t* task, task_status_t status) {
    if (status == TASK_PENDING || status == TASK_READY) {
        task->task_status = status;
        return switch_task(task);
    }
    return ERROR_WRONG_TYPE;
}

task_return_t finish_task(task_t* task, int return_value) {
    task->task_status = TASK_FINISHED;
    task->return_value = return_value;
    return switch_task(task);
}

task_return_t make_scheduler(scheduler_t* scheduler_bytes, task_t* (*next_task)(), void (*idle_task)(), void (*before_each)(), void (*after_each)(), void* (*scheduler_memset)(void*, int, size_t)) {
    scheduler_memset(scheduler_bytes, 0, sizeof(scheduler_t));
    scheduler_bytes->task_id_counter = 0;
    scheduler_bytes->next_task = next_task;
    scheduler_bytes->idle_task = idle_task;
    scheduler_bytes->before_each = before_each;
    scheduler_bytes->after_each = after_each;
    scheduler_bytes->scheduler_memset = scheduler_memset;
    scheduler_bytes->cont = 1;
    return SUCCESS_TASK;
}

task_return_t shutdown_scheduler(scheduler_t* scheduler) {
    scheduler->cont = 0;
    return SUCCESS_TASK;
}

void scheduler_mainloop(scheduler_t* scheduler) {
    while (scheduler->cont) {
        task_t* to_run = scheduler->next_task();
        if (to_run != NULL) {
            scheduler->before_each();
            context_switch(&scheduler->scheduler_context, &to_run->context);
            scheduler->after_each();
        } else {
            scheduler->idle_task();
        }
    }
}