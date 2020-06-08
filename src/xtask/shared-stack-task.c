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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void*)(0))
#endif

void* get_shared_stack_task_user_data(CTask* task) {
    return ((xuser_data_t*)(task->userData))->userData;
}

void set_shared_stack_task_user_data(CTask* task, void* pudata) {
    ((xuser_data_t*)(task->userData))->userData = pudata;
}

void _void_ret_func_xstack() {}

CSharedStack* CreateSharedStack(uint32_t xstack_size, void* (*xstack_malloc)(size_t),
                                void (*xstack_free)(void*),
                                void* (*xstack_memcpy)(void*, const void*, size_t)) {
    if (xstack_malloc == NULL || xstack_free == NULL || xstack_memcpy == NULL)
        return NULL;
    CSharedStack* new_xstack = xstack_malloc(sizeof(CSharedStack));
    if (new_xstack == NULL)
        return NULL;
    new_xstack->SharedStackFree = xstack_free;
    new_xstack->SharedStackMalloc = xstack_malloc;
    new_xstack->SharedStackMemcpy = xstack_memcpy;
    new_xstack->__sharedStackSize = xstack_size;
    new_xstack->__sharedStackPtr = xstack_malloc(xstack_size);
    if (new_xstack->__sharedStackPtr == NULL) {
        xstack_free(new_xstack);
        return NULL;
    }
    uintptr_t u_p = (uintptr_t)(new_xstack->__sharedStackSize - (sizeof(void*) << 1) +
                                (uintptr_t)new_xstack->__sharedStackPtr);
    u_p = (u_p >> 4) << 4;
    new_xstack->sharedStackPtrHighAddress = (void*)u_p;
#ifdef __x86_64__
    new_xstack->sharedStackPtr = (void*)(u_p - sizeof(void*));
    *((void**)(new_xstack->sharedStackPtr)) = (void*)(_void_ret_func_xstack);
#elif defined(__aarch64__) || defined(__arm__)
    new_xstack->sharedStackPtr = (void*)(u_p);
#else
#error "not supported"
#endif
    new_xstack->sharedStackSize = new_xstack->__sharedStackSize - 16 - (sizeof(void*) << 1);
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    new_xstack->v_stack_id = VALGRIND_STACK_REGISTER(
        new_xstack->__sharedStackPtr,
        (void*)((uintptr_t)new_xstack->__sharedStackPtr + new_xstack->__sharedStackSize));
#endif
    new_xstack->isAllocatable = 1;
    return new_xstack;
}

xuser_data_t* make_xuser_data(void* original_udata, CSharedStack* sharedStack) {
    xuser_data_t* userData = sharedStack->SharedStackMalloc(sizeof(xuser_data_t));
    if (userData == NULL) {
        sharedStack->isAllocatable = 0;
        return NULL;
    }
    userData->userData = original_udata;
    userData->privateStack = 0;
    userData->privateStackSize = 0;
    userData->__privateStackSize = 0;
    userData->sharedStack = sharedStack;
    return userData;
}

void shared_stack_task_resume(CTask* xself) {
    if (xself->taskStatus == TASK_READY) {
        xuser_data_t* xdata = xself->userData;
        CSharedStack* xstack = xdata->sharedStack;
        xstack->SharedStackMemcpy(xstack->sharedStackPtr - xdata->privateStackSize,
                                  xdata->privateStack, xdata->privateStackSize);
        currentTask = xself;
        SwapToContext(&xself->scheduler->schedulerContext, &xself->context);
        return;
    }
}

void shared_stack_task_yield(CTask* xself) {
    xuser_data_t* xdata = xself->userData;
    CSharedStack* xstack = xdata->sharedStack;
    void* tos = NULL;
#ifdef __x86_64__
    asm("movq %%rsp, %0" : "=rm"(tos));
#elif defined(__aarch64__) || defined(__arm__)
    asm("mov %[tosp], sp" : [ tosp ] "=r"(tos));
#else
#error "not supported"
#endif
    if ((uintptr_t)(tos) <= (uintptr_t)xstack->sharedStackPtr &&
        ((uintptr_t)(xstack->sharedStackPtrHighAddress) - (uintptr_t)(xstack->sharedStackSize)) <=
            (uintptr_t)(tos)) {
        xdata->privateStackSize = (uintptr_t)(xstack->sharedStackPtr) - (uintptr_t)(tos);
        if (xdata->__privateStackSize < xdata->privateStackSize) {
            xstack->SharedStackFree(xdata->privateStack);
            xdata->__privateStackSize = xdata->privateStackSize;
            xdata->privateStack = xstack->SharedStackMalloc(xdata->privateStackSize);
            if (xdata->privateStack == NULL) {
                xself->taskStatus = TASK_FINISHED;
                xself->returnValue = ERROR_TASK_STACK_OVERFLOW;
                SwapToContext(&xself->context, &xself->scheduler->schedulerContext);
            }
        }
        xstack->SharedStackMemcpy(xdata->privateStack, tos, xdata->privateStackSize);
        SwapToContext(&xself->context, &xself->scheduler->schedulerContext);
        return;
    }
}

TaskFunctions shared_stack_task_fv = {.ResumeTask = shared_stack_task_resume,
                                      .YieldTask_ = shared_stack_task_yield,
                                      .GetUserData = get_shared_stack_task_user_data,
                                      .SetUserData = set_shared_stack_task_user_data};

CTask* CreateSharedStackTask(CScheduler* scheduler, void (*TaskFunction)(CTask*, void*),
                             void* taskArgs, CSharedStack* xstack) {
    if (xstack->isAllocatable == 0 || scheduler == NULL || TaskFunction == NULL)
        return NULL;
    CTask* taskBytes = xstack->SharedStackMalloc(sizeof(CTask));
    if (taskBytes == NULL) {
        xstack->isAllocatable = 0;
        return NULL;
    }
    xuser_data_t* new_xdata = make_xuser_data(NULL, xstack);
    if (new_xdata == NULL) {
        xstack->SharedStackFree(taskBytes);
        xstack->isAllocatable = 0;
        return NULL;
    }
    CreateTask(taskBytes, scheduler, TaskFunction, taskArgs,
               new_xdata->sharedStack->__sharedStackSize, new_xdata->sharedStack->__sharedStackPtr);
    taskBytes->userData = new_xdata;
    taskBytes->taskFunctionVector = &shared_stack_task_fv;
    return taskBytes;
}

void DestroySharedStackTask(CTask* task) {
    xuser_data_t* xdata = task->userData;
    xdata->sharedStack->SharedStackFree(task);
    xdata->sharedStack->SharedStackFree(xdata->privateStack);
    xdata->sharedStack->SharedStackFree(xdata);
}

void DestroySharedStack(CSharedStack* stack) {
    void (*free_fn)(void*) = stack->SharedStackFree;
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(stack->v_stack_id);
#endif
    free_fn(stack->__sharedStackPtr);
    free_fn(stack);
}
