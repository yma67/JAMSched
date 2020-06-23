#include "concurrency/condition_variable.hpp"

void JAMScript::ConditionVariableAny::notify_one() noexcept
{
    std::unique_lock<SpinMutex> lk(wListLock);
    if (!waitList.empty())
    {
        waitList.front()->Enable();
        waitList.pop_front();
    }
}

void JAMScript::ConditionVariableAny::notify_all() noexcept
{
    std::unique_lock<SpinMutex> lk(wListLock);
    while (!waitList.empty())
    {
        waitList.front()->Enable();
        waitList.pop_front();
    }
}