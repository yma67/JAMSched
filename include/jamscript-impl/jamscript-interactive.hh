#ifndef JAMSCRIPT_JAMSCRIPT_INTERACTIVE_H
#define JAMSCRIPT_JAMSCRIPT_INTERACTIVE_H
#include <mutex>
#include <queue>
#include <memory>
#include <functional>
#include <unordered_set>
#include <future/future.h>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include "jamscript-impl/jamscript-sporadic.hh"

namespace jamscript {

static std::function<bool(const std::pair<uint64_t, task_t*>&,
                          const std::pair<uint64_t, task_t*>&)> edf_cmp =
                                  [] (const std::pair<uint64_t, task_t*>& p1,
                                      const std::pair<uint64_t, task_t*>& p2) {
    return p1.first > p2.first;
};

class c_side_scheduler;
class task_schedule_entry;

class interactive_manager : public sporadic_manager {
public:
    friend class c_side_scheduler;
    task_t* dispatch() override;
    void pause(task_t* task) override;
    bool ready(task_t* task) override;
    void remove(task_t* task) override;
    void enable(task_t* task) override;
    const uint32_t size() const override;
    void update_burst(task_t* task, uint64_t burst) override;
    task_t* add(uint64_t burst, void *args, 
                void (*func)(task_t *, void *)) override;
    task_t* add(task_t *parent, uint64_t deadline, uint64_t burst,
                void *args, void (*func)(task_t *, void *)) override;
    interactive_manager(c_side_scheduler* scheduler, uint32_t stack_size);
    ~interactive_manager() override;
private:
    std::deque<task_t*> interactive_stack;
    std::unordered_set<task_t*> interactive_wait;
    std::priority_queue<
        std::pair<uint64_t, task_t*>,
        std::vector<std::pair<uint64_t, task_t*>>, 
        decltype(edf_cmp)
    > interactive_queue;
};

}
#endif