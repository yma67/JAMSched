#ifndef JAMSCRIPT_JAMSCRIPT_REALTIME_H
#define JAMSCRIPT_JAMSCRIPT_REALTIME_H
#include <mutex>
#include <queue>
#include <memory>
#include <unordered_map>
#include <xtask/shared-stack-task.h>
#include <core/scheduler/task.h>

namespace jamscript {
class c_side_scheduler;
class realtime_manager {
    friend class c_side_scheduler;
public:
    task_t* add(uint32_t id, void* args, void(*func)(task_t *, void*));
    task_t* dispatch(uint32_t id);
    void remove(task_t* to_remove);
    void spin_until_end();
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