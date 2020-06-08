/**
 * @file        shared-stack-task.h 
 * @brief       Shared-Stack CTask/Coroutine Implementation
 * @details     copies stack to another dynamically allocated location to avoid
 *              fragmentation caused by pre-allocating a stack with fixed sized
 * @remarks     context switching may be slower
 * @warning     DO NOT USE Stack Local Variable for Pass-By-Pointer Convension 
 *              when calling a function that involves context switch, 
 *              since when stack was copied, only content was changed, but address
 *              passed into the function has not being re-mapped to storage stack
 * @warning     Please use SetUserData() and GetUserData() to set and
 *              get userData, since sharedStack takes a different layout
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
 * @struct CSharedStack 
 * @brief  shared stack
 */
typedef struct CSharedStack {
    uint8_t* sharedStackPtr;                                      /// pointer to the BEGINNING of shared stack
    uint8_t* __sharedStackPtr;                                    /// pointer to the aligned beginning of shared stack
    uint8_t* sharedStackPtrHighAddress;                            /// pointer to the high address value end of shared stack
    uint32_t sharedStackSize;                                     /// shared stack aligned NumberOfTaskReady limit
    uint32_t __sharedStackSize;                                   /// shared stack actual NumberOfTaskReady limit
    int isAllocatable;                                             /// whether the allocator would still be able to allocate using malloc
    void *(*SharedStackMalloc)(size_t);                           /// malloc function of the allocator
    void  (*SharedStackFree)(void *);                             /// free function of the allocator
    void *(*SharedStackMemcpy)(void *, const void *, size_t);     /// memcpy function, could be memcpy or user implemented
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    unsigned long v_stack_id;
#endif
} CSharedStack;

/**
 * @struct xuser_data_t
 * @brief  user data extension for shared stack, stores saving stack (task private) 
 * @details takes place of the regular tasks' userData field, so we have to use SetUserData, GetUserData to set and get the actual user data
 * @warning use SetUserData and GetUserData, DO NOT CTask->userData = xxx, this will lose the private stack and shared stack of the task!!!!
 */
typedef struct xuser_data_t {
    void* userData;                                                /// actual user data as CTask::userData, should be accessed from SetUserData, GetUserData
    CSharedStack* sharedStack;                                   /// pointer to its shared stack (CSharedStack)
    uint8_t* privateStack;                                         /// pointer to beginning of private saving stack
    uint32_t privateStackSize;                                    /// current NumberOfTaskReady of saving stack
    uint32_t __privateStackSize;                                  /// historical max NumberOfTaskReady of saving stack, avoid calling malloc everytime that increases context switching time
} xuser_data_t;

/**
 * Shared Stack CTask Initializer
 * @param scheduler: scheduler of the task
 * @param TaskFunction: function to be executed as the task
 * @param taskArgs: argument that WILL be passed into task function along with task itself
 * @param xstack: common shared stack used for this task, should be allocated before this invocation
 * @warning DO NOT USE Stack Local Variable for Pass-By-Pointer Convension 
 *          when calling a function that involves context switch 
 * @see    CreateTask() for all other warnings and remarks
 * @return NULL if not possible to allocate memory, or TaskFunction or scheduler is NULL
 */
extern CTask* CreateSharedStackTask(CScheduler* scheduler, 
                                      void (*TaskFunction)(CTask*, void*), 
                                      void* taskArgs, CSharedStack* xstack);

/**
 * Shared Stack Initializer
 * @param xstack_size: NumberOfTaskReady of shared stack
 * @param xstack_malloc: malloc function used for allocating saver/storage stack for individual task, 
 *                       and allocating task object, ...
 * @param xstack_free: free everything the malloc does
 * @param xstack_memcpy: copy stack when context switching
 * @return NULL if the latter 3 parameters are NULL, or stack allocation failed
 */
extern CSharedStack* CreateSharedStack(uint32_t xstack_size, 
                                         void *(*xstack_malloc)(size_t), 
                                         void  (*xstack_free)(void *), 
                                         void *(*xstack_memcpy)(void *, 
                                                                const void *, 
                                                                size_t));
/**
 * Shared Stack CTask Destryer
 * @param task: task to be destructed
 * @warning please back up the pointer to userData you setted using SetUserData, 
 *          accessing it after destroying task may result a segmentation fault
 * @remark  does not free userData itself but free location with a copy of value of userData
 * @return  void
 */
extern void DestroySharedStackTask(CTask* task);

/**
 * Shared Stack Destryer
 * @param stack: stack to be destructed
 * @warning destroy shared stack only all tasks sharing this stack has status TASK_FINISHED, this
 *          is not checked by this function
 * @return  void
 */
extern void DestroySharedStack(CSharedStack* stack);
#ifdef __cplusplus
}
#endif
#endif