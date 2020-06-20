#include "scheduler/taskthief.hpp"

JAMScript::StealScheduler::StealScheduler(SchedulerBase* victim, uint32_t ssz) : 
    SchedulerBase(ssz), victim(victim) {}

JAMScript::StealScheduler::~StealScheduler() {
    cv.notify_all();
    auto dTaskInf = [](TaskInterface* t) { delete t; };
    isWait.clear_and_dispose(dTaskInf);
    isReady.clear_and_dispose(dTaskInf);
}

void JAMScript::StealScheduler::Steal(TaskInterface* toSteal) {
    std::unique_lock<std::mutex> lock(m);
    toSteal->Steal(this);
    isReady.push_back(*toSteal);
    cv.notify_all();
    stealCount++;
}

void JAMScript::StealScheduler::Enable(TaskInterface* toEnable) {
    std::unique_lock<std::mutex> lock(m);
    if (toEnable->wiHook.is_linked()) {
        isWait.erase(reinterpret_cast<uintptr_t>(toEnable));
    }
    if (!toEnable->rbQueueHook.is_linked()) {
        isReady.push_back(*toEnable);
    }
    cv.notify_all();
}

void JAMScript::StealScheduler::Disable(TaskInterface* toDisable) {
    std::unique_lock<std::mutex> lock(m);
    if (!toDisable->wiHook.is_linked()) {
        isWait.insert(*toDisable);
    }
}

const uint32_t JAMScript::StealScheduler::Size() const {
    return isWait.size() + isReady.size();
}

void JAMScript::StealScheduler::ShutDown_() {
    if (toContinue)
        toContinue = false;
    cv.notify_all();
}

void JAMScript::StealScheduler::ShutDown() {
    victim->ShutDown();
    ShutDown_();
}

void JAMScript::StealScheduler::RunSchedulerMainLoop() {
    std::thread t([this]() {
        while (victim->toContinue && toContinue) {
            std::unique_lock<std::mutex> lock(m);
            while (isReady.empty() && (victim->toContinue && toContinue)) {
                cv.wait(lock);
            }
            if (!(victim->toContinue && toContinue))
                break;
            auto iterNext = isReady.begin();
            auto* pNext = &(*iterNext);
            isReady.erase(iterNext);
            lock.unlock();
            pNext->SwapIn();
            if (pNext->status == TASK_FINISHED) {
                delete pNext;
            }
        }
    });
    t.detach();
}