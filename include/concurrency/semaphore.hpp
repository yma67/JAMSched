#ifndef JAMSCRIPT_SEMAPHORE_HH
#define JAMSCRIPT_SEMAPHORE_HH
#include "concurrency/condition_variable.hpp"

#include <cstdint>
namespace JAMScript
{
    class ConditionVariable;
    class SpinMutex;

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

        Semaphore(int c = 1) : count(c) {}

    private:
        SpinMutex mutex;
        ConditionVariable queue;
        int count;
    };

} // namespace JAMScript

#endif