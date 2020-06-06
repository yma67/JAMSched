#ifndef JAMSCRIPT_JAMSCRIPT_TASKTYPE_H
#define JAMSCRIPT_JAMSCRIPT_TASKTYPE_H
#include <cstdint>
#include <memory>
#include <future/future.h>
namespace jamscript {
enum ctask_types { interactive_task_t = 0, batch_task_t = 1,  real_time_task_t };

struct c_side_task_extender {
    ctask_types task_type;
    c_side_task_extender(ctask_types task_type) : 
    task_type(task_type) {}
private:
    c_side_task_extender() = delete;
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

struct batch_extender : public c_side_task_extender {
    uint64_t burst;
    batch_extender(uint64_t burst) : 
    c_side_task_extender(batch_task_t), burst(burst) {}
private:
    batch_extender() = delete;
};

struct real_time_extender : public c_side_task_extender {
    uint32_t id;
    uint64_t start, deadline;
    real_time_extender(uint32_t id) : 
    c_side_task_extender(real_time_task_t), id(id), start(0), deadline(0) {}
private:
    real_time_extender() = delete;
};

}

#endif