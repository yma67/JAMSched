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
};

struct real_time_extender : public c_side_task_extender {
    uint32_t id;
    uint64_t start, deadline;
};

struct batch_extender : public c_side_task_extender {
    uint64_t burst;
};

struct interactive_extender : public c_side_task_extender {
    uint64_t burst, deadline;
    std::shared_ptr<jamfuture_t> handle;
};

struct task_schedule_entry {
    uint64_t start_time, end_time;
    uint32_t task_id;
    task_schedule_entry(uint64_t s, uint64_t e, uint32_t id) : 
    start_time(s), end_time(e), task_id(id) {}
    bool inside(uint64_t time_point, uint64_t hyper_period, 
                uint32_t multiplier) const {
        return ((start_time + multiplier * hyper_period) <= time_point) && 
                (time_point <= (end_time + multiplier * hyper_period));
    }
};

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
    void run();
    bool is_running();
    void exit();
    c_side_scheduler(std::vector<task_schedule_entry> normal_schedule,
                     std::vector<task_schedule_entry> greedy_schedule,
                     uint32_t stack_size, void* local_app_args,
                     void (*local_app_fn)(task_t *, void *));
    ~c_side_scheduler();
    uint32_t current_schedule_slot, multiplier;
private:
    scheduler_t* c_scheduler;
    task_t* c_local_app_task;
    shared_stack_t* c_shared_stack;
    
    uint64_t virtual_clock_batch, virtual_clock_interactive;
    std::vector<task_schedule_entry>* current_schedule;
    decltype(std::chrono::high_resolution_clock::now()) scheduler_start_time,
                                                        task_start_time;
    std::vector<task_schedule_entry> normal_schedule, greedy_schedule;
    std::priority_queue<std::pair<uint64_t, task_t*>,
                        std::vector<std::pair<uint64_t, task_t*>>,
                        decltype(jamscript::edf_cmp)> interactive_queue;
    std::deque<task_t*> batch_queue, interactive_stack;
    std::unordered_map<uint32_t, std::vector<task_t*>> real_time_tasks_map;
    std::mutex real_time_tasks_mutex, batch_tasks_mutex;
};

void before_each_jam_impl(task_t *);
void after_each_jam_impl(task_t *);
task_t* next_task_jam_impl(scheduler_t *);
void idle_task_jam_impl(scheduler_t *);
void interactive_task_handle_post_callback(jamfuture_t *);

}
#endif //JAMSCRIPT_JAMSCRIPT_SCHEDULER_H
