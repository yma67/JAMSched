#ifndef JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#define JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#include <atomic>

namespace jamc
{

    class SpinMutex
    {

        std::atomic_flag flag = ATOMIC_FLAG_INIT;

    public:

        void lock();
        bool try_lock();
        void unlock();

    };

    using SpinOnlyMutex = SpinMutex;

} // namespace jamc
#endif