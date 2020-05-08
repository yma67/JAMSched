#include "scheduler/task.h"
#include <stdint.h>

#define TASK_STACK_MIN 256
#define NULL ((void *)0)


void empty_func_next_idle(scheduler_t* self) {}
void empty_func_before_after(task_t* self) {}
void* get_user_data(task_t* t) { return t->user_data; }

void start_task(unsigned int task_addr_lower, unsigned int task_addr_upper) {
	task_t *task = (task_t*)(task_addr_lower | 
                   (((unsigned long)task_addr_upper << 16) << 16));
	task->task_function(task, task->task_args);
	task->task_status = TASK_FINISHED;
}

task_return_t make_task(task_t* task_bytes, scheduler_t* scheduler, 
                        void (*task_function)(task_t*, void*), void* task_args,
                        void* user_data, unsigned int stack_size, 
                        unsigned char* stack) {
    // init task injected
    if (task_bytes == NULL || scheduler == NULL || task_function == NULL) {
        return ERROR_TASK_INVALID_ARGUMENT;
    }
    task_bytes->stack_size = stack_size;
    task_bytes->task_id = scheduler->task_id_counter;
    task_bytes->task_function = task_function;
    task_bytes->task_args = task_args;
    task_bytes->scheduler = scheduler;
    task_bytes->stack = stack;
    scheduler->task_id_counter = scheduler->task_id_counter + 1;
    task_bytes->user_data = user_data;
    task_bytes->resume_task = resume_task;
    task_bytes->yield_task = yield_task;
    task_bytes->get_user_data = get_user_data;
    // init context
    if (getcontext(&(task_bytes->context)) < 0)
		return ERROR_TASK_CONTEXT_INIT;
    task_bytes->context.uc_stack.ss_sp = task_bytes->stack + 8;
    task_bytes->context.uc_stack.ss_size = task_bytes->stack_size - 64;
    makecontext(&task_bytes->context, (void(*)())start_task, 2, 
                (uint32_t)((uint64_t)task_bytes), 
                (uint32_t)((uint64_t)task_bytes >> 32));
    task_bytes->task_status = TASK_READY;
    return SUCCESS_TASK;
}

task_return_t context_switch(jam_ucontext_t* from, jam_ucontext_t* to) {
	if (swapcontext(from, to) < 0) 
		return ERROR_TASK_CONTEXT_SWITCH;
    return SUCCESS_TASK;
}

void resume_task(task_t* self) {
    context_switch(&self->scheduler->scheduler_context, &self->context);
}

task_return_t switch_task(task_t* task) {
    char tos = 0;
    if ((uintptr_t)(&tos) < (uintptr_t)(task->stack) || 
        (uintptr_t)(&tos) > (uintptr_t)(task->stack + task->stack_size)) {
        return ERROR_TASK_STACK_OVERFLOW;
    }
	return context_switch(&task->context, &task->scheduler->scheduler_context);
}

void yield_task(task_t* task) {
    if (task == NULL) return;
    switch_task(task);
}

task_return_t finish_task(task_t* task, int return_value) {
    if (task == NULL) return ERROR_TASK_INVALID_ARGUMENT;
    task->task_status = TASK_FINISHED;
    task->return_value = return_value;
    task->yield_task(task);
    return SUCCESS_TASK;
}

task_return_t make_scheduler(scheduler_t* scheduler_bytes, 
                             task_t* (*next_task)(scheduler_t* self), 
                             void (*idle_task)(scheduler_t* self), 
                             void (*before_each)(task_t*), 
                             void (*after_each)(task_t*)) {
    if (scheduler_bytes == NULL || next_task == NULL || idle_task == NULL || 
        before_each == NULL || after_each == NULL) 
        return ERROR_TASK_INVALID_ARGUMENT;
    scheduler_bytes->task_id_counter = 0;
    scheduler_bytes->next_task = next_task;
    scheduler_bytes->idle_task = idle_task;
    scheduler_bytes->before_each = before_each;
    scheduler_bytes->after_each = after_each;
    scheduler_bytes->cont = 1;
    return SUCCESS_TASK;
}

task_return_t shutdown_scheduler(scheduler_t* scheduler) {
    if (scheduler == NULL) return ERROR_TASK_INVALID_ARGUMENT;
    scheduler->cont = 0;
    return SUCCESS_TASK;
}

void scheduler_mainloop(scheduler_t* scheduler) {
    if (scheduler == NULL) return;
    while (scheduler->cont) {
        task_t* to_run = scheduler->next_task(scheduler);
        if (to_run != NULL && to_run->task_status == TASK_READY) {
            scheduler->before_each(to_run);
            to_run->resume_task(to_run);
            scheduler->after_each(to_run);
        } else {
            scheduler->idle_task(scheduler);
        }
    }
}