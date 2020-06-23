#ifndef JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#define JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#include <atomic>

namespace JAMScript
{

    class SpinMutex
    {

        std::atomic_flag flag;

    public:

        SpinMutex() : flag{false} {}
        void lock();
        bool try_lock();
        void unlock();

    };

} // namespace JAMScript
#endif