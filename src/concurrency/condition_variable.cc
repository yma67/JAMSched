#include "concurrency/condition_variable.h"

void JAMScript::ConditionVariableAny::notify_one() noexcept {
    std::lock_guard<SpinLock> lk(wListLock);
    if (!waitSet.empty()) {
        waitSet.begin()->Notify();
        waitSet.erase(waitSet.begin());
    }
}

void JAMScript::ConditionVariableAny::notify_all() noexcept {
    std::lock_guard<SpinLock> lk(wListLock);
    while (!waitSet.empty()) {
        waitSet.begin()->Notify();
        waitSet.erase(waitSet.begin());
    }
}