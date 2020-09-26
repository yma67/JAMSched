#include "concurrency/spinlock.hpp"
#include <core/task/task.hpp>

void JAMScript::SpinMutex::lock()
{
    int cnt = 1;
    while (flag.test_and_set(std::memory_order_acquire))
    {
        if (TaskInterface::Active() != nullptr && (cnt) % 20 == 0)
        {
            ThisTask::Yield();
        }
        else if ((cnt) % 20 == 0)
        {
            std::this_thread::yield();
        }
        cnt = (cnt + 1) % 20;
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

void JAMScript::SpinOnlyMutex::lock()
{
    int cnt = 0;
    while (flag.test_and_set(std::memory_order_acquire));
}

bool JAMScript::SpinOnlyMutex::try_lock()
{
    return !flag.test_and_set(std::memory_order_acquire);
}

void JAMScript::SpinOnlyMutex::unlock()
{
    flag.clear(std::memory_order_release);
}