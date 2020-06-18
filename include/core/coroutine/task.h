/**
 * @file        task.h 
 * @brief       JAMScript core control flow and coroutine construction
 * @warning     this module does not take care of memory management, everyting is 
 *              dependency injection and abstraction
 * @warning     DO NOT RETURN IN A TASK FUNCTION
 * @remark      Please include this Header ONLY, do NOT include context.c or anything
 *              under include/ucontext directory
 * @author      Yuxiang Ma, Muthucumaru Maheswaran
 * @copyright 
 *              Copyright 2020 Yuxiang Ma, Muthucumaru Maheswaran 
 * 
 *              Licensed under the Apache License, Version 2.0 (the "License");
 *              you may not use this file except in compliance with the License.
 *              You may obtain a copy of the License at
 * 
 *                  http://www.apache.org/licenses/LICENSE-2.0
 * 
 *              Unless required by applicable law or agreed to in writing, software
 *              distributed under the License is distributed on an "AS IS" BASIS,
 *              WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *              See the License for the specific language governing permissions and
 *              limitations under the License.
 */
#ifndef TASK_H
#define TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "core/coroutine/context.h"

/**
 * @typedef CScheduler
 * @brief  Definition of CScheduler
 */
typedef struct CScheduler CScheduler;

/**
 * @typedef CTask
 * @brief  Definition of CTask
 */
typedef struct CTask CTask;

/**
 * @brief State Machine for CTask
 * @details upon a task established, it is READY
 * @details a task could be set to PENDING by user, either explicitly or using yield
 * @warning a PENDING task will not be executed, even if it is dispatched by scheduler
 * @details a task is finished when it declares it finishes, finished tasks will not 
 *          be executed
 */
typedef enum {
    TASK_READY = 0, 
    TASK_PENDING = 1, 
    TASK_FINISHED, 
    TASK_RUNNING
} TaskStatus;

/**
 * @brief Exception for CTask
 */
typedef enum {
    SUCCESS_TASK,
    ERROR_TASK_CONTEXT_INIT, 
    ERROR_TASK_INVALID_ARGUMENT,
    ERROR_TASK_STACK_OVERFLOW, 
    ERROR_TASK_CONTEXT_SWITCH, 
    ERROR_TASK_WRONG_TYPE, 
    ERROR_TASK_CANCELLED
} TaskReturn;

typedef struct TaskFunctions {
    void (*TaskResume)(CTask*);                  /// function for resuming a task, switch from scheduler to task
    void (*TaskYield_)(CTask*);                  /// function for ending a task, switch from task to scheduler
    void*(*GetUserData)(CTask*);                 /// getter for userData, useful when writing extension component based on CTask
    void (*SetUserData)(CTask*, void*);          /// setter for userData, useful when writing extension component based on CTask
} TaskFunctions;

struct CTask {
    TaskStatus taskStatus;                       /// state machine of a task
    CScheduler* scheduler;                       /// scheduler of the task
    CScheduler* actualScheduler;
    JAMScriptUserContext context;                /// context store for this task, could be use to restore its execution
    void (*TaskFunction)(CTask*, void*);         /// function to be executed as the task
    void *taskArgs;                              /// argument that WILL be passed into task function along with task itself
    void *userData;                              /// argument that WILLNOT be passed into task function along with task itself
    int returnValue;                             /// undefined until call FinishTask
    unsigned char *stack;                        /// stack pointer to an allocated stack for this task, may NOT be null
    unsigned int stackSize;                      /// NumberOfTaskReady of stack, used for check
    TaskFunctions* taskFunctionVector;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    unsigned long v_stack_id;
#endif
};

struct CScheduler {
    CTask* taskRunning;
    JAMScriptUserContext schedulerContext;       /// context store for scheduler, used to switch back to scheduler
    CTask* (*NextTask)(CScheduler*);             /// feed scheduler the next task to run
    void (*IdleTask)(CScheduler*);               /// activities to do if there is no task to run
    void (*BeforeEach)(CTask*);                  /// activities to do before executing ANY task
    void (*AfterEach)(CTask*);                   /// activities to do after executing ANY task
    void*(*GetSchedulerData)(CScheduler*);       /// getter for schedulerData, useful when writing extension component based on CScheduler
    void (*SetSchedulerData)(CScheduler*, void*);/// getter for schedulerData, useful when writing extension component based on CScheduler
    volatile int isSchedulerContinue;            /// flag, used to determine whether scheduler continues to run
    void* schedulerData;                         /// user defined data for scheduler, useful when building an extension of the scheduler
};

extern __thread CTask* currentTask;

/**
 * CTask Initializer
 * @param taskBytes: memory allocated for a task,
 * @param scheduler: scheduler of the task
 * @param TaskFunction: function to be executed as the task
 * @param taskArgs: argument that WILL be passed into task function along with task itself
 * @param stackSize: NumberOfTaskReady of coroutine/task stack
 * @param stack: pointer to stack allocated for coroutine/task
 * @warning due to the dependency injection nature of the framework, caller is responsible for 
 *          allocating and initializing memory with proper NumberOfTaskReady and content, it is suggested to 
 *          be set to 0 using memset. this is valid on, but not limited to taskBytes
 * @warning taskBytes, scheduler, TaskFunction, should NOT be NULL
 * @warning not thread safe
 * @remark  user is responsible of parsing userData and taskArgs
 * @remark  user is responsible of setting userData 
 * @remark  TaskFunction is not executed atomically/transactionally
 * @remark  task is READY after function returns
 * @details sanity checks, setup CTask, initialize coroutine context, make task to be READY
 * @return  SUCCESS_TASK if success, otherwise ERROR_TASK_INVALID_ARGUMENT
 */
extern TaskReturn CreateTask(CTask* taskBytes, CScheduler* scheduler, 
                               void (*TaskFunction)(CTask*, void*), 
                               void* taskArgs, unsigned int stackSize, 
                               unsigned char* stack);

/**
 * CScheduler Initializer
 * @param schedulerBytes: memory allocated for a scheduler,
 * @param NextTask: feed scheduler the next task to run
 * @param IdleTask: activities to do if there is no task to run
 * @param BeforeEach: activities to do before executing ANY task
 * @param AfterEach: activities to do after executing ANY task
 * @warning due to the dependency injection nature of the framework, caller is responsible for 
 *          allocating and initializing memory with proper NumberOfTaskReady and content, it is suggested to 
 *          be set to 0 using memset. this is valid on, but not limited to schedulerBytes
 * @warning NextTask, IdleTask, BeforeEach, AfterEach should NOT be NULL
 * @warning not thread safe
 * @remark  BeforeEach, AfterEach could be a setup/cleanup
 * @return  SUCCESS_TASK if success, otherwise ERROR_TASK_INVALID_ARGUMENT
 */
extern TaskReturn CreateScheduler(CScheduler* schedulerBytes, 
                                    CTask* (*NextTask)(CScheduler* self), 
                                    void (*IdleTask)(CScheduler* self), 
                                    void (*BeforeEach)(CTask*), 
                                    void (*AfterEach)(CTask*));

/**
 * Shutdown CScheduler
 * @param scheduler: scheduler to be shut down
 * @warning schedluer may not be null
 * @warning this is not synchronized, the time of effect depends on task and all other functions
 * @warning this is not atomic and value of CScheduler::isSchedulerContinue may be unexpected due to DATA RACE
 * @details break the while loop of the scheduler for its NEXT cycle
 * @return  SUCCESS_TASK if success, otherwise ERROR_TASK_INVALID_ARGUMENT
 */
extern TaskReturn ShutdownScheduler(CScheduler* scheduler);

/**
 * Yield CTask
 * @param yieldingTask: task to give up its context
 * @warning this is NOT an atomic operation, and it is subject to DATA RACE
 * @warning will fail if stack overflow detected
 * @return void
 */
#define TaskYield(yieldingTask)\
    if (yieldingTask == NULL) __builtin_trap();\
    yieldingTask->taskFunctionVector->TaskYield_(yieldingTask);

/**
 * Finish CTask
 * @param finishingTask: task to declare a finish of task, and a task MUST declare a finish
 * @param finishingTaskReturnValue: return value of the task function, could be retrieved later on
 * @warning: this write is NOT atomic, and may not be propergated to other cores in a
 *           multiprocessor program
 * @warning: a finished task could not be restored
 * @warning: USE THIS VERY CAREFULLY IN C++, CreateRIBTask an extra {} to wrap all your code in the
 * TaskFunction before invoking this macro at the end
 * @return not meaningful
 */
#define FinishTask(finishingTask, finishingTaskReturnValue)\
    finishingTask->taskStatus = TASK_FINISHED;\
    finishingTask->returnValue = finishingTaskReturnValue;\
    TaskYield(finishingTask);

#define GetCurrentTaskRunning() (currentTask)

/**
 * CScheduler Mainloop
 * @param scheduler: scheduler to start
 * @remark  this is the start of execution of scheduling framework where we proudly 
 *          accept your flow of execution until you order shutdown
 * @details executes the following functions in order: BeforeEach, task, AfterEach if there is 
 *          a task, otherwise, IdleTask
 * @remark  these functions could fully cover all possible points of injecting codes into scheduler
 *          loop, and is running in context of SCHEDULER, not TASK
 * @remark  these functions are not atomic/executed transactionally
 * @remark  to EnableTask a common teardown for IdleTask and AfterEach, call a common function within
 *          IdleTask and AfterEach
 * @return  void
 */
extern void SchedulerMainloop(CScheduler* scheduler);

/**
 * function placeholder for NextTask and IdleTask
 * @param   self: scheduler to be scheduled
 * @details this function does nothing and returns nothing
 * @return  void
 */
extern void EmptyFuncNextIdle(CScheduler* self);

/**
 * function placeholder for BeforeEach and AfterEach
 * @param   self: task to be cleaned up
 * @details this function does nothing and returns nothing
 * @return  void
 */
extern void EmptyFuncBeforeAfter(CTask* self);

#ifdef __cplusplus
}
#endif

#endif