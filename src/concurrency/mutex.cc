#include "core/task/task.hpp"
#include "concurrency/mutex.hpp"

bool JAMScript::FIFOTaskMutex::try_lock() {
    std::lock_guard<SpinLock> lk(qLock);
    auto prev = m_state;
    m_state = LOCKED;
    return prev == UNLOCKED;
}

void JAMScript::FIFOTaskMutex::lock() {
    std::unique_lock<SpinLock> lk(qLock);
    auto prev = m_state;
    m_state = LOCKED;
    Notifier* n = new Notifier(JAMScript::ThisTask::Active());
    while (prev != UNLOCKED) {
        waitSet.insert(*n);
        n->Join(lk);
        prev = m_state;
        m_state = LOCKED;
    }
    waitSet.erase(reinterpret_cast<uintptr_t>(n));
    delete n;
}

void JAMScript::FIFOTaskMutex::unlock() {
    std::lock_guard<SpinLock> lk(qLock);
    m_state = UNLOCKED;
    if (!waitSet.empty()) {
        waitSet.begin()->Notify();
    }
}