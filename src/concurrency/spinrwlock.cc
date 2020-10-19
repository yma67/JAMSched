#include "concurrency/spinrwlock.hpp"
#include "core/task/task.hpp"
#include<atomic>

#define SPIN_LOCK_UNLOCK 0
#define SPIN_LOCK_WRITE_LOCK -1

using std::atomic;
using std::atomic_int;
using std::atomic_store_explicit;
using std::atomic_load_explicit;
using std::atomic_compare_exchange_weak_explicit;
using std::memory_order_relaxed;
using std::memory_order_acquire;
using std::memory_order_release;

jamc::SharedSpinMutex::SharedSpinMutex()
{
    atomic_store_explicit(&l, SPIN_LOCK_UNLOCK, memory_order_relaxed);
}

void jamc::SharedSpinMutex::lock()
{
    int expected;
    int desired;
    int cnt = 0;
    while(true)
    {
        expected = atomic_load_explicit(&l, memory_order_relaxed);

        if(expected == SPIN_LOCK_UNLOCK)
        {
            desired = SPIN_LOCK_WRITE_LOCK;
            if(atomic_compare_exchange_weak_explicit(&l, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }
        if (++cnt % 20 == 0)
        {
            cnt = 0;
            jamc::ctask::Yield();
        }
    }

    atomic_thread_fence(memory_order_release); // sync
}

bool jamc::SharedSpinMutex::try_lock()
{
    int expected;
    int desired;

    expected = atomic_load_explicit(&l, memory_order_relaxed);

    if(expected == SPIN_LOCK_UNLOCK)
    {
        desired = SPIN_LOCK_WRITE_LOCK;
        if(atomic_compare_exchange_weak_explicit(&l, &expected, desired, memory_order_relaxed, memory_order_relaxed)) {
            atomic_thread_fence(memory_order_release); // sync
            return true;
        }
    }
    return false;
}

void jamc::SharedSpinMutex::unlock()
{
    int expected;
    int desired;

    while(true)
    {
        expected = atomic_load_explicit(&l, memory_order_relaxed);

        if(expected == SPIN_LOCK_WRITE_LOCK)
        {
            desired = SPIN_LOCK_UNLOCK;

            atomic_thread_fence(memory_order_release); // sync
            if(atomic_compare_exchange_weak_explicit(&l, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }
        else
        {
            assert(false);
        }
    }
}

void jamc::SharedSpinMutex::lock_shared()
{
    int expected;
    int desired;
    int cnt = 0;
    while(true)
    {
        expected = atomic_load_explicit(&l, memory_order_relaxed);

        if(expected >= 0)
        {
            desired = 1 + expected;
            if(atomic_compare_exchange_weak_explicit(&l, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }
        if (++cnt % 20 == 0)
        {
            cnt = 0;
            jamc::ctask::Yield();
        }
    }

    atomic_thread_fence(memory_order_acquire); // sync
}

bool jamc::SharedSpinMutex::try_lock_shared()
{
    int expected;
    int desired;

    expected = atomic_load_explicit(&l, memory_order_relaxed);

    if(expected >= 0)
    {
        desired = 1 + expected;
        if(atomic_compare_exchange_weak_explicit(&l, &expected, desired, memory_order_relaxed, memory_order_relaxed))
        {
            atomic_thread_fence(memory_order_acquire); // sync
            return true; // success
        }
    }
    return false;
}

void jamc::SharedSpinMutex::unlock_shared()
{
    int expected;
    int desired;
    
    while(true)
    {
        expected = atomic_load_explicit(&l, memory_order_relaxed);

        if(expected > 0)
        {
            desired = expected - 1;

            atomic_thread_fence(memory_order_release); // sync
            if(atomic_compare_exchange_weak_explicit(&l, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }
        else
        {
            assert(false);
        }
    }
}