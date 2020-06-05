//
// Created by mayuxiang on 2020-05-15.
//

#ifndef JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
#define JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
#include "core/scheduler/task.h"
#include "xtask/shared-stack-task.h"
#include "future/future.h"
#include <queue>
#include <tuple>
#include <mutex>
#include <vector>
#include <memory>
#include <chrono>
#include <utility>
#include <functional>
#include <unordered_set>
#include <unordered_map>

namespace jamscript {

enum ctask_types { batch_task_t, interactive_task_t, real_time_task_t };

static std::function<bool(const std::pair<uint64_t, task_t*>&,
                          const std::pair<uint64_t, task_t*>&)> edf_cmp =
                                  [] (const std::pair<uint64_t, task_t*>& p1,
                                      const std::pair<uint64_t, task_t*>& p2) {
    return p1.first > p2.first;
};

struct c_side_task_extender {
    ctask_types task_type;
    c_side_task_extender(ctask_types task_type) : 
    task_type(task_type) {}
private:
    c_side_task_extender() = delete;
};

struct real_time_extender : public c_side_task_extender {
    uint32_t id;
    uint64_t start, deadline;
    real_time_extender(uint32_t id) : 
    c_side_task_extender(real_time_task_t), id(id), start(0), deadline(0) {}
private:
    real_time_extender() = delete;
};

struct batch_extender : public c_side_task_extender {
    uint64_t burst;
    batch_extender(uint64_t burst) : 
    c_side_task_extender(batch_task_t), burst(burst) {}
private:
    batch_extender() = delete;
};

struct interactive_extender : public c_side_task_extender {
    uint64_t burst, deadline;
    std::shared_ptr<jamfuture_t> handle;
    interactive_extender(uint64_t burst, uint64_t deadline, 
                         std::shared_ptr<jamfuture_t> handle) : 
    c_side_task_extender(interactive_task_t), burst(burst), deadline(deadline),
    handle(std::move(handle)) {}
private:
    interactive_extender() = delete;
};

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

class c_side_scheduler {
public:

    friend void before_each_jam_impl(task_t *);
    friend void after_each_jam_impl(task_t *);
    friend task_t* next_task_jam_impl(scheduler_t *);
    friend void idle_task_jam_impl(scheduler_t *);
    friend void interactive_task_handle_post_callback(jamfuture_t *);

    bool add_batch_task(uint32_t burst, void* args, 
                        void(*local_exec_fn)(task_t *, void*));
    bool add_real_time_task(uint32_t, void*, void(*)(task_t *, void*));
    std::shared_ptr<jamfuture_t> add_interactive_task(task_t *, uint64_t, 
                                                      uint64_t, void *, 
                                                      void(*)(task_t*, void*));
    std::vector<task_schedule_entry>* decide();
    std::vector<task_schedule_entry>& get_normal_schedule();
    void set_normal_schedule(const std::vector<task_schedule_entry>& sched);
    std::vector<task_schedule_entry>& get_greedy_schedule();
    void set_greedy_schedule(const std::vector<task_schedule_entry>& sched);
    void run();
    bool is_running();
    void exit();

    c_side_scheduler(std::vector<task_schedule_entry> normal_schedule,
                     std::vector<task_schedule_entry> greedy_schedule,
                     uint32_t device_id, uint32_t stack_size, 
                     void* local_app_args,
                     void (*local_app_fn)(task_t *, void *));
    ~c_side_scheduler();

private:
    scheduler_t* c_scheduler;
    task_t* c_local_app_task;
    shared_stack_t* c_shared_stack;
    uint64_t virtual_clock_batch, virtual_clock_interactive;
    std::vector<long long> total_jitter;
    std::vector<task_schedule_entry>* current_schedule;
    std::vector<uint64_t> normal_ss_acc, greedy_ss_acc;
    std::vector<interactive_extender> interactive_record;
    decltype(std::chrono::high_resolution_clock::now()) task_start_time, 
                                                        cycle_start_time;
    std::vector<task_schedule_entry> normal_schedule, greedy_schedule;
    std::priority_queue<std::pair<uint64_t, task_t*>,
                        std::vector<std::pair<uint64_t, task_t*>>,
                        decltype(jamscript::edf_cmp)> interactive_queue;
    std::unordered_set<task_t*> batch_wait, interactive_wait;
    std::deque<task_t*> batch_queue, interactive_stack;
    std::unordered_map<uint32_t, std::vector<task_t*>> real_time_tasks_map;
    std::mutex real_time_tasks_mutex, batch_tasks_mutex, 
               interactive_tasks_mutex;
    std::vector<jamscript::task_schedule_entry> *random_decide();
    void download_schedule();
    c_side_scheduler(c_side_scheduler const&) = delete;
    c_side_scheduler(c_side_scheduler &&) = delete;
    c_side_scheduler& operator=(c_side_scheduler const&) = delete;
    c_side_scheduler& operator=(c_side_scheduler &&) = delete;

public:
    uint32_t current_schedule_slot, multiplier, device_id;
    decltype(std::chrono::high_resolution_clock::now()) scheduler_start_time;
    std::unordered_map<std::string, void*> local_function_map;

    template <typename Tr, typename Tf>
    static void 
    local_named_task_function_br(task_t* self, void* args) {
        {
            auto* exec_p = 
            static_cast<std::pair<std::shared_ptr<jamfuture_t>, Tf>*>(args);
            auto* value_slot = new Tr;
            exec_p->first->data = value_slot;
            *(value_slot) = (exec_p->second)();
            exec_p->first->status = ack_finished;
            notify_future(exec_p->first.get());
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
            auto* value_slot = new Tr;
            self_cpp->handle->data = value_slot;
            auto* exec_fp = static_cast<Tf*>(args);
            *(value_slot) = (*exec_fp)();
            self_cpp->handle->status = ack_finished;
            notify_future(self_cpp->handle.get());
            delete exec_fp;
        }
        finish_task(self, 0);
    }

    template <typename Tr, typename ...Args> 
    std::shared_ptr<jamfuture_t> 
    add_local_named_task_async(task_t* parent_task, uint64_t deadline, 
                               uint64_t duration, std::string exec_name,
                               Args... args) {
        if (local_function_map.find(exec_name) == local_function_map.end()) {
            return nullptr;
        }
        auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(
            local_function_map[exec_name]
        );
        auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
        return add_interactive_task(parent_task, deadline, duration, 
                                    new decltype(exec_fp)(exec_fp),
                                    local_named_task_function
                                    <Tr, decltype(exec_fp)>);
    }

    template <typename Tr, typename ...Args> 
    std::shared_ptr<jamfuture_t> 
    add_local_named_task_async(task_t* parent_task, uint32_t task_id, 
                               std::string exec_name, Args... args) {
        if (local_function_map.find(exec_name) == local_function_map.end()) {
            return nullptr;
        }
        auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(
            local_function_map[exec_name]
        );
        auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
        auto task_handle = std::make_shared<jamfuture_t>();
        make_future(task_handle.get(), parent_task, nullptr, 
                    interactive_task_handle_post_callback);
        add_real_time_task(task_id, new std::pair<std::shared_ptr<jamfuture_t>, 
                           decltype(exec_fp)>(task_handle, std::move(exec_fp)), 
                           local_named_task_function_br<Tr,decltype(exec_fp)>);
        return task_handle;
    }

    template <typename Tr, typename ...Args> 
    std::shared_ptr<jamfuture_t> 
    add_local_named_task_async(task_t* parent_task, uint64_t burst, 
                               std::string exec_name, Args... args) {
        if (local_function_map.find(exec_name) == local_function_map.end()) {
            return nullptr;
        }
        auto* named_exec_fp = reinterpret_cast<Tr (*)(Args...)>(
            local_function_map[exec_name]
        );
        auto exec_fp = std::bind(named_exec_fp, std::forward<Args>(args)...);
        auto task_handle = std::make_shared<jamfuture_t>();
        make_future(task_handle.get(), parent_task, nullptr, 
                    interactive_task_handle_post_callback);
        add_batch_task(burst, new std::pair<std::shared_ptr<jamfuture_t>, 
                       decltype(exec_fp)>(task_handle, std::move(exec_fp)),
                       local_named_task_function_br<Tr, decltype(exec_fp)>);
        return task_handle;
    }

};

}
#endif //JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
