#ifndef JAMSCRIPT_JAMSCRIPT_SPORADIC_H
#define JAMSCRIPT_JAMSCRIPT_SPORADIC_H
#include <mutex>
#include <future/future.h>
#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include "jamscript-impl/jamscript-tasktype.hh"

namespace jamscript {

class c_side_scheduler;

class sporadic_manager {
public:
    friend class c_side_scheduler;
    friend void before_each_jam_impl(task_t *);
    friend void after_each_jam_impl(task_t *);
    friend task_t* next_task_jam_impl(scheduler_t *);
    friend void idle_task_jam_impl(scheduler_t *);
    friend void interactive_task_handle_post_callback(jamfuture_t *);
    virtual task_t* dispatch() = 0;
    virtual void pause(task_t* task) = 0;
    virtual bool ready(task_t* task) = 0;
    virtual void remove(task_t* task) = 0;
    virtual void enable(task_t* task) = 0;
    virtual void update_burst(task_t* task, uint64_t burst) = 0;
    virtual task_t* add(uint64_t burst, void *args, 
                        void (*func)(task_t *, void *)) = 0;
    virtual task_t* add(task_t *parent, uint64_t deadline, uint64_t burst,
                        void *args, void (*func)(task_t *, void *)) = 0;
    virtual const uint32_t size() const = 0;
    sporadic_manager(c_side_scheduler* scheduler, uint32_t stack_size) : 
    scheduler(scheduler), stack_size(stack_size) {

    }
    virtual ~sporadic_manager() {}
protected:
    std::mutex m;
    c_side_scheduler* scheduler;
    uint32_t stack_size;
    sporadic_manager(sporadic_manager const&) = delete;
    sporadic_manager(sporadic_manager &&) = delete;
    sporadic_manager& operator=(sporadic_manager const&) = delete;
    sporadic_manager& operator=(sporadic_manager &&) = delete;
};

}
#endif