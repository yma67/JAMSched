#ifndef JAMSCRIPT_TASKTHIEF_HH
#define JAMSCRIPT_TASKTHIEF_HH
#include <concurrency/spinlock.h>
#include <core/task/task.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace JAMScript {
    class StealScheduler : public SchedulerBase {
    public:
        friend class RIBScheduler;
        void Steal(TaskInterface* toSteal) {
            std::unique_lock<std::mutex> lock(m);
            toSteal->Steal(this);
            isReady.push_back(*toSteal);
            cv.notify_all();
            stealCount++;
        }
        void Enable(TaskInterface* toEnable) override {
            std::unique_lock<std::mutex> lock(m);
            if (toEnable->wiHook.is_linked()) {
                isWait.erase(reinterpret_cast<uintptr_t>(toEnable));
            }
            if (!toEnable->rbQueueHook.is_linked()) {
                isReady.push_back(*toEnable);
            }
            cv.notify_all();
        }
        void Disable(TaskInterface* toDisable) override {
            std::unique_lock<std::mutex> lock(m);
            if (!toDisable->wiHook.is_linked()) {
                isWait.insert(*toDisable);
            }
        }
        uint32_t Size() { return isWait.size() + isReady.size(); }
        void ShutDown_() {
            if (toContinue)
                toContinue = false;
            cv.notify_all();
            for (auto& t : tx) t.join();
        }
        void ShutDown() {
            victim->ShutDown();
            ShutDown_();
        }
        void operator()() {
            tx.push_back(std::thread([this]() {
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
            }));
        }
        StealScheduler(SchedulerBase* victim, uint32_t ssz) : SchedulerBase(ssz), victim(victim) {}
        ~StealScheduler() {
            auto dTaskInf = [](TaskInterface* t) { delete t; };
            isWait.clear_and_dispose(dTaskInf);
            isReady.clear_and_dispose(dTaskInf);
            std::cout << "stealCount: " << stealCount << std::endl;
        }

    protected:
        std::mutex m;
        int stealCount = 0;
        std::condition_variable cv;
        std::vector<std::thread> tx;
        SchedulerBase* victim;
        JAMStorageTypes::InteractiveWaitSetType isWait;
        JAMStorageTypes::BatchQueueType isReady;
    };
}  // namespace JAMScript
#endif