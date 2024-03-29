#ifndef JAMSCRIPT_MUTEX_HH
#define JAMSCRIPT_MUTEX_HH
#include "concurrency/semaphore.hpp"
namespace jamc
{
    class Mutex
    {
    public:
        void lock()
        {
            s.Wait();
        }

        bool try_lock()
        {
            return s.TryWait();
        }

        void unlock()
        {
            s.Signal();
        }

    private:
        Semaphore<1> s;
    };
} // namespace jamc

#endif