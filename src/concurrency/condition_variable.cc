#include "concurrency/condition_variable.hpp"

void JAMScript::ConditionVariableAny::notify_one() noexcept
{
    std::unique_lock<SpinLock> lk(wListLock);
    if (!waitSet.empty())
    {
        waitSet.begin()->Notify(lk);
        waitSet.erase(waitSet.begin());
    }
}

void JAMScript::ConditionVariableAny::notify_all() noexcept
{
    std::unique_lock<SpinLock> lk(wListLock);
    while (!waitSet.empty())
    {
        waitSet.begin()->Notify(lk);
        waitSet.erase(waitSet.begin());
    }
}