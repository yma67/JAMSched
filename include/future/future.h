/**
 * @file future.h 
 * @brief   async await protocol in JAMScript
 * @warning DO NOT USE STACK LOCAL VARIABLE AS jamfuture_t OR data
 *          since get_future involves context switching
 * @warning this implementation is abstract and minimal, but guarantees that 
 *          a waiting task WILL NOT BE SCHEDULED, please avoid deadlock
 * @warning althouth we chages task status and lock words atomically, 
 *          notify_future is not considered atomic because invocation of 
 *          post_future_callback is neither atomic nor synchronized
 * @warning please avoid access of jamfuture_t::lock_word and task_t::task_status 
 *          by thread other than wakers and sleeper, otherwise, regular wakeup or sleep is not guaranteed
 * @remark  mechanism of avoiding a task to not being scheduled is simple 
 *          by just marking the task as TASK_PENDING, but user may define other
 *          actions in post_future_callback, but this function is not atomic
 */
#ifndef AWAIT_H
#define AWAIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <core/scheduler/task.h>

typedef struct jamfuture_t jamfuture_t;

/**
 * @struct jamfuture_t
 * @brief  await semantic for jamscript scheduler
 * @remarks used along with task_t, compatible with xtask 
 * @warning DO NOT USE STACK LOCAL VARIABLE AS jamfuture_t OR data
 *          since get_future involves context switching
 * @warning please avoid access to jamfuture_t::lock_word and task_t::task_status 
 *          by thread other than wakers and sleeper, 
 *          otherwise, regular wakeup or sleep is not guaranteed
 */
struct jamfuture_t {
    uint32_t lock_word;
    void* data;
    task_t* owner_task;
    void (*post_future_callback)(jamfuture_t*);
};

/**
 * Future Initiallizer
 * @param future: memory buffer used for constructing the future
 * @param waiter: coroutine that waits while the future value not ready
 * @param data: pointer to data buffer
 * @param post_future_callback: cleanup after future value ready, e.g. make the coroutine available for scheduling in scheduler::next_task
 * @warning post_future_callback will be an empty function if you set it to be NULL
 * @warning no atomicitiy guarantee
 * @see scheduler::next_task in task.h
 */
void make_future(jamfuture_t* future, task_t* waiter, void* data, 
                 void (*post_future_callback)(jamfuture_t*));

/**
 * Get Future
 * @param future: data in this parameter will be retrieved
 * @warning DOES NOT USE STACK LOCAL VARIABLE with xtask
 * @warning this will make future->owner_task become unschedulable, i.e. status be changed to TASK_PENDING, which will NOT run by sheduler
 */
void get_future(jamfuture_t* future);

/**
 * Notify Future
 * @param future: data in this parameter will be retrieved
 * @warning DOES NOT USE STACK LOCAL VARIABLE with xtask
 * @warning this will call post_future_callback after future->owner_task being set to TASK_READY
 */
void notify_future(jamfuture_t* future);

/**
 * Placeholder for post_future_callback
 * @brief an empty function
 */
void empty_func_post_future_callback(jamfuture_t* future);

#ifdef __cplusplus
}
#endif
#endif