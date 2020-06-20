#include "concurrency/spinlock.hpp"
#include <core/task/task.hpp>

void JAMScript::SpinLock::lock() {
    int cnt = 0;
    while (flag.test_and_set(std::memory_order_acquire)) {
        if (ThisTask::Active() != nullptr && (cnt++) % 2000 == 0) {
            ThisTask::Yield();
        }
    }
}

bool JAMScript::SpinLock::try_lock() {
    return !flag.test_and_set(std::memory_order_acquire);
}

void JAMScript::SpinLock::unlock() {
    flag.clear(std::memory_order_release);
}