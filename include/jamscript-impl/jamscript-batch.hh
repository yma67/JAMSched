#ifndef JAMSCRIPT_JAMSCRIPT_BATCH_H
#define JAMSCRIPT_JAMSCRIPT_BATCH_H
#include <mutex>
#include <queue>
#include <memory>
#include <unordered_set>
#include <xtask/shared-stack-task.h>
#include <core/scheduler/task.h>
#include "jamscript-impl/jamscript-sporadic.hh"

namespace jamscript {

class c_side_scheduler;

class batch_manager : public sporadic_manager {
public:
    friend class c_side_scheduler;
    task_t* dispatch() override;
    const uint32_t size() const override;
    void pause(task_t* task) override;
    bool ready(task_t* task) override;
    void remove(task_t* task) override;
    void enable(task_t* task) override;
    void update_burst(task_t* task, uint64_t burst) override;
    task_t* add(uint64_t burst, void *args, 
                void (*func)(task_t *, void *)) override;
    task_t* add(task_t *parent, uint64_t deadline, uint64_t burst, void *args,
                void (*func)(task_t *, void *)) override;
    batch_manager(c_side_scheduler* scheduler, uint32_t stack_size);
    ~batch_manager() override;
private:
    std::deque<task_t*> batch_queue;
    std::unordered_set<task_t*> batch_wait;
};

}
#endif