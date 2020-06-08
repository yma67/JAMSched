#ifndef JAMSCRIPT_JAMSCRIPT_TASKTYPE_H
#define JAMSCRIPT_JAMSCRIPT_TASKTYPE_H
#include <cstdint>
#include <memory>
#include <future/future.h>

namespace JAMScript {

enum CTaskType { 
    INTERACTIVE_TASK_T = 0, 
    BATCH_TASK_T = 1,  
    REAL_TIME_TASK_T
};

struct CTaskExtender {
    CTaskType taskType;
    CTaskExtender(CTaskType taskType) : 
    taskType(taskType) {}
private:
    CTaskExtender() = delete;
};

struct InteractiveTaskExtender : public CTaskExtender {
    uint64_t burst, deadline;
    std::shared_ptr<CFuture> handle;
    InteractiveTaskExtender(uint64_t burst, uint64_t deadline, 
                         std::shared_ptr<CFuture> handle) : 
    CTaskExtender(INTERACTIVE_TASK_T), burst(burst), deadline(deadline),
    handle(std::move(handle)) {}
private:
    InteractiveTaskExtender() = delete;
};

struct BatchTaskExtender : public CTaskExtender {
    uint64_t burst;
    BatchTaskExtender(uint64_t burst) : 
    CTaskExtender(BATCH_TASK_T), burst(burst) {}
private:
    BatchTaskExtender() = delete;
};

struct RealTimeTaskExtender : public CTaskExtender {
    uint32_t id;
    uint64_t start, deadline;
    RealTimeTaskExtender(uint32_t id) : 
    CTaskExtender(REAL_TIME_TASK_T), id(id), start(0), deadline(0) {}
private:
    RealTimeTaskExtender() = delete;
};

}

#endif