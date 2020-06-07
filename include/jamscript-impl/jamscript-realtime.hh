#ifndef JAMSCRIPT_JAMSCRIPT_REALTIME_H
#define JAMSCRIPT_JAMSCRIPT_REALTIME_H
#include <mutex>
#include <queue>
#include <memory>
#include <unordered_map>
#include <xtask/shared-stack-task.h>
#include <core/scheduler/task.h>
#include <future/future.h>

namespace jamscript {

class c_side_scheduler;

class realtime_manager {
public:
    friend class c_side_scheduler;
    friend void before_each_jam_impl(task_t *);
    friend void after_each_jam_impl(task_t *);
    friend task_t* next_task_jam_impl(scheduler_t *);
    friend void idle_task_jam_impl(scheduler_t *);
    friend void interactive_task_handle_post_callback(jamfuture_t *);
    void spin_until_end();
    task_t* dispatch(uint32_t id);
    void remove(task_t* to_remove);
    task_t* add(uint32_t id, void* args, void(*func)(task_t *, void*));
    realtime_manager(c_side_scheduler* scheduler, uint32_t stack_size);
    ~realtime_manager();
private:
    std::mutex m;
    c_side_scheduler* scheduler;
    shared_stack_t* c_shared_stack;
    std::unordered_map<uint32_t, std::deque<task_t*>> task_map;
    realtime_manager() = delete;
};
}

#endif