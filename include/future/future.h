/**
 * @file        future.h
 * @brief       async await protocol in JAMScript
 * @warning     DO NOT USE STACK LOCAL VARIABLE AS CFuture OR data
 *              since WaitForValueFromFuture involves context switching
 * @warning     this implementation is abstract and minimal, but guarantees that
 *              a waiting task WILL NOT BE SCHEDULED, please avoid deadlock
 * @warning     althouth we chages task status and lock words atomically,
 *              NotifyFinishOfFuture is not considered atomic because invocation of
 *              PostFutureCallback is neither atomic nor synchronized
 * @warning     please avoid access of CFuture::lockWord and CTask::taskStatus
 *              by thread other than wakers and sleeper, otherwise, regular wakeup or sleep is not
 * guaranteed
 * @remark      mechanism of avoiding a task to not being scheduled is simple
 *              by just marking the task as TASK_PENDING, but user may define other
 *              actions in PostFutureCallback, but this function is not atomic
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
#ifndef AWAIT_H
#define AWAIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <core/scheduler/task.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CFuture CFuture;
typedef enum { ACK_FINISHED, ACK_CANCELLED, ACK_FAILED } Ack;

/**
 * @struct CFuture
 * @brief  await semantic for JAMScript scheduler
 * @remarks used along with CTask, compatible with xtask
 * @details spin wait, then sleep
 * @warning DO NOT USE STACK LOCAL VARIABLE AS CFuture OR data
 *          since WaitForValueFromFuture involves context switching
 * @warning please avoid access to CFuture::lockWord and CTask::taskStatus
 *          by thread other than wakers and sleeper,
 *          otherwise, regular wakeup or sleep is not guaranteed
 */
struct CFuture {
    uint32_t lockWord;                     /// spin lock for spin+sleep
    void* data;                            /// future data
    CTask* ownerTask;                      /// task that sleeps/wakeups
    uint32_t numberOfSpinRounds;           /// number of spin rounds
    void (*PostFutureCallback)(CFuture*);  /// cleanup after value prepared (make schedulable)
    Ack status;
};

/**
 * CFuture Initiallizer
 * @param future: memory buffer used for constructing the future
 * @param waiter: coroutine that waits while the future value not SetTaskReady
 * @param data: pointer to data buffer
 * @param PostFutureCallback: cleanup after future value SetTaskReady, e.g. make the coroutine
 * available for scheduling in scheduler::NextTask
 * @warning PostFutureCallback will be an empty function if you set it to be NULL
 * @warning no atomicitiy guarantee
 * @see scheduler::NextTask in task.h
 */
int CreateFuture(CFuture* future, CTask* waiter, void* data, void (*PostFutureCallback)(CFuture*));

/**
 * Get CFuture
 * @param future: data in this parameter will be retrieved
 * @warning DOES NOT USE STACK LOCAL VARIABLE with xtask
 * @warning this will make future->ownerTask become unschedulable, i.e. status be changed to
 * TASK_PENDING, which will NOT run by sheduler
 */
void WaitForValueFromFuture(CFuture* future);

/**
 * Notify CFuture
 * @param future: data in this parameter will be retrieved
 * @warning DOES NOT USE STACK LOCAL VARIABLE with xtask
 * @warning this will call PostFutureCallback after future->ownerTask being set to TASK_READY
 */
void NotifyFinishOfFuture(CFuture* future);

/**
 * Placeholder for PostFutureCallback
 * @brief an empty function
 */
void EmptyFuncPostFutureCallback(CFuture* future);

#ifdef __cplusplus
}
#endif
#endif