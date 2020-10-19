#ifndef JAMSCRIPT_SEMAPHORE_HH
#define JAMSCRIPT_SEMAPHORE_HH
#include "concurrency/condition_variable.hpp"
#include <cstdint>

namespace jamc
{
    class ConditionVariable;
    class SpinMutex;

    template <std::size_t SemaphoreSize>
    class Semaphore
    {
    public:
        void Signal()
        {
            std::unique_lock lock(mutex);
            count += 1;
            queue.notify_one();
        }

        void Wait()
        {
            std::unique_lock lock(mutex);
            queue.wait(lock, [this]() -> bool {
                return count > 0;
            });
            count -= 1;
        }

        int GetCount()
        {
            std::unique_lock lock(mutex);
            return count;
        }

        bool TryWait()
        {
            std::unique_lock lock(mutex);
            if (count > 0)
            {
                count--;
                return true;
            }
            return false;
        }

        Semaphore() : count(SemaphoreSize) {}

    private:
        SpinMutex mutex;
        ConditionVariable queue;
        int count;
    };

} // namespace jamc

#endif