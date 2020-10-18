#pragma once
#include <atomic>
namespace jamc
{
class SharedSpinMutex
{
    using spinlock_t = std::atomic_int;
    spinlock_t l;
public:
    SharedSpinMutex();
    void lock();
    bool try_lock();
    void unlock();
    void lock_shared();
    bool try_lock_shared();
    void unlock_shared();
};
}