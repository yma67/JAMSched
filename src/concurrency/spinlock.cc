#include "concurrency/spinlock.hpp"
#include <core/task/task.hpp>

void JAMScript::SpinMutex::lock()
{
    int cnt = 1;
    while (flag.test_and_set(std::memory_order_acquire))
    {
        if (ThisTask::Active() != nullptr && (cnt) % 2000 == 0)
        {
            ThisTask::Yield();
        }
        cnt = (cnt + 1) % 2000;
    }
}

bool JAMScript::SpinMutex::try_lock()
{
    return !flag.test_and_set(std::memory_order_acquire);
}

void JAMScript::SpinMutex::unlock()
{
    flag.clear(std::memory_order_release);
}