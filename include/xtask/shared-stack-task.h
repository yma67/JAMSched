/**
 * @file        shared-stack-task.h 
 * @brief       Shared-Stack Task/Coroutine Implementation
 * @details     copies stack to another dynamically allocated location to avoid
 *              fragmentation caused by pre-allocating a stack with fixed sized
 * @remarks     context switching may be slower
 * @warning     DO NOT USE Stack Local Variable for Pass-By-Pointer Convension 
 *              when calling a function that involves context switch, 
 *              since when stack was copied, only content was changed, but address
 *              passed into the function has not being re-mapped to storage stack
 * @warning     Please use set_user_data() and get_user_data() to set and
 *              get user_data, since shared_stack takes a different layout
 * @see         task.h
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
#ifndef SHARED_STACK_TASK_H
#define SHARED_STACK_TASK_H
#ifdef __cplusplus
extern "C" {
#endif
#include "core/scheduler/task.h"
#include <stdint.h>
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    #include <valgrind/valgrind.h>
#endif
/**
 * @struct shared_stack_t 
 * @brief  shared stack
 */
typedef struct shared_stack_t {
    uint8_t* shared_stack_ptr;                                      /// pointer to the BEGINNING of shared stack
    uint8_t* __shared_stack_ptr;                                    /// pointer to the aligned beginning of shared stack
    uint8_t* shared_stack_ptr_high_addr;                            /// pointer to the high address value end of shared stack
    uint32_t shared_stack_size;                                     /// shared stack aligned size limit
    uint32_t __shared_stack_size;                                   /// shared stack actual size limit
    int is_allocatable;                                             /// whether the allocator would still be able to allocate using malloc
    void *(*shared_stack_malloc)(size_t);                           /// malloc function of the allocator
    void  (*shared_stack_free)(void *);                             /// free function of the allocator
    void *(*shared_stack_memcpy)(void *, const void *, size_t);     /// memcpy function, could be memcpy or user implemented
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    unsigned long v_stack_id;
#endif
} shared_stack_t;

/**
 * @struct xuser_data_t
 * @brief  user data extension for shared stack, stores saving stack (task private) 
 * @details takes place of the regular tasks' user_data field, so we have to use set_user_data, get_user_data to set and get the actual user data
 * @warning use set_user_data and get_user_data, DO NOT task_t->user_data = xxx, this will lose the private stack and shared stack of the task!!!!
 */
typedef struct xuser_data_t {
    void* user_data;                                                /// actual user data as task_t::user_data, should be accessed from set_user_data, get_user_data
    shared_stack_t* shared_stack;                                   /// pointer to its shared stack (shared_stack_t)
    uint8_t* private_stack;                                         /// pointer to beginning of private saving stack
    uint32_t private_stack_size;                                    /// current size of saving stack
    uint32_t __private_stack_size;                                  /// historical max size of saving stack, avoid calling malloc everytime that increases context switching time
} xuser_data_t;

/**
 * Shared Stack Task Initializer
 * @param scheduler: scheduler of the task
 * @param task_function: function to be executed as the task
 * @param task_args: argument that WILL be passed into task function along with task itself
 * @param xstack: common shared stack used for this task, should be allocated before this invocation
 * @warning DO NOT USE Stack Local Variable for Pass-By-Pointer Convension 
 *          when calling a function that involves context switch 
 * @see    make_task() for all other warnings and remarks
 * @return NULL if not possible to allocate memory, or task_function or scheduler is NULL
 */
extern task_t* make_shared_stack_task(scheduler_t* scheduler, 
                                      void (*task_function)(task_t*, void*), 
                                      void* task_args, shared_stack_t* xstack);

/**
 * Shared Stack Initializer
 * @param xstack_size: size of shared stack
 * @param xstack_malloc: malloc function used for allocating saver/storage stack for individual task, 
 *                       and allocating task object, ...
 * @param xstack_free: free everything the malloc does
 * @param xstack_memcpy: copy stack when context switching
 * @return NULL if the latter 3 parameters are NULL, or stack allocation failed
 */
extern shared_stack_t* make_shared_stack(uint32_t xstack_size, 
                                         void *(*xstack_malloc)(size_t), 
                                         void  (*xstack_free)(void *), 
                                         void *(*xstack_memcpy)(void *, 
                                                                const void *, 
                                                                size_t));
/**
 * Shared Stack Task Destryer
 * @param task: task to be destructed
 * @warning please back up the pointer to user_data you setted using set_user_data, 
 *          accessing it after destroying task may result a segmentation fault
 * @remark  does not free user_data itself but free location with a copy of value of user_data
 * @return  void
 */
extern void destroy_shared_stack_task(task_t* task);

/**
 * Shared Stack Destryer
 * @param stack: stack to be destructed
 * @warning destroy shared stack only all tasks sharing this stack has status TASK_FINISHED, this
 *          is not checked by this function
 * @return  void
 */
extern void destroy_shared_stack(shared_stack_t* stack);
#ifdef __cplusplus
}
#endif
#endif