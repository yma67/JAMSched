#include <mutex>
#include <iostream>
#include "core/task/task.hpp"
#include "concurrency/notifier.hpp"
#include "concurrency/spinlock.hpp"

void JAMScript::Notifier::Join()
{
    if (ownerTask == nullptr)
    {
        std::unique_lock<SpinLock> lock(m);
        while (lockWord < 0x80000000)
            cv.wait(lock);
        return;
    }
    std::unique_lock<SpinLock> lock(m);
    while (lockWord < 0x80000000)
    {
        ownerTask->scheduler->Disable(ownerTask);
        lock.unlock();
        ownerTask->SwapOut();
        lock.lock();
    }
}

void JAMScript::Notifier::Notify()
{
    if (ownerTask == nullptr)
    {
        std::unique_lock<SpinLock> lock(m);
        lockWord |= 0x80000000;
        cv.notify_all();
        return;
    }
    std::lock_guard l(m);
    lockWord |= 0x80000000;
    ownerTask->scheduler->Enable(ownerTask);
}

void JAMScript::Notifier::Notify(std::unique_lock<JAMScript::SpinLock> &iLock)
{
    if (ownerTask == nullptr)
    {
        lockWord |= 0x80000000;
        cv.notify_all();
        return;
    }
    lockWord |= 0x80000000;
    ownerTask->scheduler->Enable(ownerTask);
}

void JAMScript::Notifier::Join(std::unique_lock<JAMScript::SpinLock> &iLock)
{
    if (ownerTask == nullptr)
    {
        while (lockWord < 0x80000000)
            cv.wait(iLock);
        return;
    }
    while (lockWord < 0x80000000)
    {
        ownerTask->scheduler->Disable(ownerTask);
        iLock.unlock();
        auto cs = std::chrono::high_resolution_clock::now();
        ownerTask->SwapOut();
        iLock.lock();
    }
}