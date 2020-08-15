#include "concurrency/condition_variable.hpp"

void JAMScript::ConditionVariableAny::notify_one()
{
    std::unique_lock lk(wListLock);
    if (!waitList.empty())
    {
        auto* pFr = &(*waitList.begin());
        waitList.pop_front();
        auto exp = reinterpret_cast<std::intptr_t>(this);
        if (pFr->cvStatus.compare_exchange_strong(exp, static_cast<std::intptr_t>(-1), std::memory_order_acq_rel))
        {
            pFr->Enable();
            return;
        }
        if (exp == static_cast<std::intptr_t>(0))
        {
            pFr->Enable();
            return;
        }
    }
}

void JAMScript::ConditionVariableAny::notify_all()
{
    std::unique_lock lk(wListLock);
    while (!waitList.empty())
    {
        auto* pFr = &(*waitList.begin());
        waitList.pop_front();
        auto exp = reinterpret_cast<std::intptr_t>(this);
        if (pFr->cvStatus.compare_exchange_strong(exp, static_cast<std::intptr_t>(-1), std::memory_order_acq_rel))
        {
            pFr->Enable();
        }
        else if (exp == static_cast<std::intptr_t>(0))
        {
            pFr->Enable();
        }
    }
}