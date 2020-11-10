#include "concurrency/spinlock.hpp"

void jamc::SpinMutex::lock()
{
    while (flag.test_and_set(std::memory_order_acquire));
}

bool jamc::SpinMutex::try_lock()
{
    return !flag.test_and_set(std::memory_order_acquire);
}

void jamc::SpinMutex::unlock()
{
    flag.clear(std::memory_order_release);
}