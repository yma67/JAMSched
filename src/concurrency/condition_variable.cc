#include "concurrency/condition_variable.hpp"

void JAMScript::ConditionVariableAny::notify_one()
{
    std::unique_lock lk(wListLock);
    if (!waitList.empty())
    {
        auto* pFr = &(*waitList.begin());
        waitList.pop_front();
        BOOST_ASSERT(!pFr->wsHook.is_linked());
        pFr->Enable();
    }
}

void JAMScript::ConditionVariableAny::notify_all()
{
    std::unique_lock lk(wListLock);
    while (!waitList.empty())
    {
        auto* pFr = &(*waitList.begin());
        waitList.pop_front();
        pFr->Enable();
    }
}