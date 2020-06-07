//
// Created by mayuxiang on 2020-05-15.
//

#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
#include <queue>
#include <tuple>
#include <mutex>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <utility>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <future/future.h>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include "jamscript-impl/jamscript-realtime.hh"
#include "jamscript-impl/jamscript-decider.hh"
#include "jamscript-impl/jamscript-sporadic.hh"
#include "jamscript-impl/jamscript-interactive.hh"
#include "jamscript-impl/jamscript-batch.hh"
#include "jamscript-impl/jamscript-future.hh"

namespace jamscript {

struct task_schedule_entry {
    uint64_t start_time, end_time;
    uint32_t task_id;
    task_schedule_entry(uint64_t s, uint64_t e, uint32_t id) : 
    start_time(s), end_time(e), task_id(id) {}
    bool inside(uint64_t time_point) const {
        return ((start_time * 1000) <= time_point) && 
               (time_point <= (end_time * 1000));
    }
};

/**
 * before task
 * start task executing clock
 */
void before_each_jam_impl(task_t *self);

/**
 * after task
 * @remark ready task: increase clock, add back to queue
 * @remark pending task: increase clock
 * @remark finished task: increase clock, free memory
 */
void after_each_jam_impl(task_t *self);

/**
 * next task
 * @remark if the current time slot is RT, return RT
 * @remark if the current time slot is SS, if both queues empty, return nullptr, if batch queue is empty we return one from interactive, 
 *         and vice verca, otherwise, we dispatch according to virtual clock value
 * @remark for batch task, we use a unbounded FIFO queue
 * @remark for interactive task, we use a priority queue to implement EDF, and another bounded stack to store task with missing deadline
 * @remark when we decide to schedule interactive task, if there is any expired task from priority queue, we put them into stack, 
 *         if there is no task remaining after the previous process, we pop the (last entered, latest) expired task from stack (LIFO)
 *         if no such task exist in stack, return nullptr. if there is unexpired task from priority queue, we return the task with earliest
 *         deadline (EDF)
 */
task_t* next_task_jam_impl(scheduler_t *self);
void idle_task_jam_impl(scheduler_t *self);
void interactive_task_handle_post_callback(jamfuture_t *self);

class decider;
class batch_manager;
class realtime_manager;
class interactive_manager;
struct c_side_task_extender;
struct interactive_extender;

class c_side_scheduler {
public:
    friend void after_each_jam_impl(task_t *);
    friend void before_each_jam_impl(task_t *);
    friend void idle_task_jam_impl(scheduler_t *);
    friend task_t* next_task_jam_impl(scheduler_t *);
    friend void interactive_task_handle_post_callback(jamfuture_t *);
    friend class decider;
    friend class batch_manager;
    friend class sporadic_manager;
    friend class realtime_manager;
    friend class interactive_manager;
    void run();
    void exit();
    bool is_running();
    uint32_t get_num_cycle_finished();
    uint64_t get_current_timepoint_in_cycle();
    uint64_t get_current_timepoint_in_scheduler();
    uint64_t get_current_timepoint_in_task();
    void register_named_execution(std::string name, void* fp);
    bool add_real_time_task(uint32_t, void*, void(*)(task_t *, void*));
    bool add_batch_task(uint32_t burst, void* args, void(*f)(task_t *, void*));
    std::shared_ptr<jamfuture_t> 
    add_interactive_task(uint64_t, uint64_t, void *, void(*f)(task_t*, void*));
    c_side_scheduler(std::vector<task_schedule_entry> normal_schedule,
                     std::vector<task_schedule_entry> greedy_schedule,
                     uint32_t device_id, uint32_t stack_size, 
                     void* local_app_args,
                     void (*local_app_fn)(task_t *, void *));
    ~c_side_scheduler();
private:
    decider dec;
    scheduler_t* c_scheduler;
    task_t* c_local_app_task;
    uint64_t virtual_clock[2];
    realtime_manager rt_manager;
    std::recursive_mutex time_mutex;
    std::atomic<uint32_t> multiplier;
    std::vector<long long> total_jitter;
    uint32_t current_schedule_slot, device_id;
    std::mutex future_mutex, named_exec_mutex;
    std::unique_ptr<sporadic_manager> s_managers[2];
    std::vector<task_schedule_entry>* current_schedule;
    decltype(std::chrono::high_resolution_clock::now()) 
    task_start_time, cycle_start_time, scheduler_start_time;
    std::unordered_map<std::string, void*> local_function_map;
    std::vector<task_schedule_entry> normal_schedule, greedy_schedule;
    void download_schedule();
    void move_scheduler_slot();
    std::vector<task_schedule_entry>* decide();
    std::vector<jamscript::task_schedule_entry> *random_decide();
    c_side_scheduler(c_side_scheduler const&) = delete;
    c_side_scheduler(c_side_scheduler &&) = delete;
    c_side_scheduler& operator=(c_side_scheduler const&) = delete;
    c_side_scheduler& operator=(c_side_scheduler &&) = delete;
public:
    template <typename Tr, typename Tf>
    static void 
    local_named_task_function_br(task_t* self, void* args) {
        {
            auto* exec_p = 
            static_cast<std::pair<std::shared_ptr<future<Tr>>, Tf>*>(args);
            exec_p->first->set_value(std::move((exec_p->second)()));
            delete exec_p;
        }
        finish_task(self, 0);
    }
    template <typename Tr, typename Tf>
    static void
    local_named_task_function(task_t* self, void* args) {
        {
            auto* self_cpp = static_cast<interactive_extender*>(
                self->task_fv->get_user_data(self)
            );
            auto* exec_fp = static_cast<Tf*>(args);
            self_cpp->handle->data = new Tr(std::move((*exec_fp)()));
            self_cpp->handle->status = ack_finished;
            notify_future(self_cpp->handle.get());
            delete exec_fp;
        }
        finish_task(self, 0);
    }
    template <typename Tr, typename ...Args> 
    std::shared_ptr<jamfuture_t> 
    add_local_named_task_async(uint64_t deadline, 
                               uint64_t duration, std::string exec_name,
                               Args&& ...args) {
        if (local_function_map.find(exec_name) == local_function_map.end()) {
            return nullptr;
        }
        std::unique_lock<std::mutex> l(named_exec_mutex);
        auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(
            local_function_map[exec_name]
        );
        l.unlock();
        auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
        return add_interactive_task(deadline, duration, 
                                    new decltype(exec_fp)(std::move(exec_fp)),
                                    local_named_task_function
                                    <Tr, decltype(exec_fp)>);
    }
    template <typename Tr, typename ...Args> 
    std::shared_ptr<future<Tr>> 
    add_local_named_task_async(uint32_t task_id, std::string exec_name, 
                               Args&& ...args) {
        if (local_function_map.find(exec_name) == local_function_map.end()) {
            return nullptr;
        }
        std::unique_lock<std::mutex> l(named_exec_mutex);
        auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(
            local_function_map[exec_name]
        );
        l.unlock();
        auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
        auto task_handle = std::make_shared<future<Tr>>();
        add_real_time_task(task_id, new std::pair<std::shared_ptr<future<Tr>>, 
                           decltype(exec_fp)>(task_handle, std::move(exec_fp)), 
                           local_named_task_function_br<Tr,decltype(exec_fp)>);
        return task_handle;
    }
    template <typename Tr, typename ...Args> 
    std::shared_ptr<future<Tr>> 
    add_local_named_task_async(uint64_t burst, std::string exec_name, 
                               Args&& ...args) {
        if (local_function_map.find(exec_name) == local_function_map.end()) {
            return nullptr;
        }
        std::unique_lock<std::mutex> l(named_exec_mutex);
        auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(
            local_function_map[exec_name]
        );
        l.unlock();
        auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
        auto task_handle = std::make_shared<future<Tr>>();
        add_batch_task(burst, new std::pair<std::shared_ptr<future<Tr>>, 
                       decltype(exec_fp)>(task_handle, std::move(exec_fp)),
                       local_named_task_function_br<Tr, decltype(exec_fp)>);
        return task_handle;
    }
};

}
#endif //JAMSCRIPT_JAMSCRIPT_SCHEDULER_H