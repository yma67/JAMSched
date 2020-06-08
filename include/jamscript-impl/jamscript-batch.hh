#ifndef JAMSCRIPT_JAMSCRIPT_BATCH_H
#define JAMSCRIPT_JAMSCRIPT_BATCH_H
#include <mutex>
#include <queue>
#include <memory>
#include <unordered_set>
#include <xtask/shared-stack-task.h>
#include <core/scheduler/task.h>
#include "jamscript-impl/jamscript-sporadic.hh"

namespace JAMScript {

class Scheduler;

class BatchTaskManager : public SporadicTaskManager {
public:
    friend class Scheduler;
    CTask* DispatchTask() override;
    void PauseTask(CTask* task) override;
    bool SetTaskReady(CTask* task) override;
    void RemoveTask(CTask* task) override;
    void EnableTask(CTask* task) override;
    const uint32_t NumberOfTaskReady() const override;
    void UpdateBurstToTask(CTask* task, uint64_t burst) override;
    CTask* CreateRIBTask(uint64_t burst, void *args, 
                void (*func)(CTask *, void *)) override;
    CTask* CreateRIBTask(CTask *parent, uint64_t deadline, uint64_t burst, void *args,
                void (*func)(CTask *, void *)) override;
    BatchTaskManager(Scheduler* scheduler, uint32_t stackSize);
    ~BatchTaskManager() override;
private:
    std::deque<CTask*> batchQueue;
    std::unordered_set<CTask*> batchWait;
};

}
#endif