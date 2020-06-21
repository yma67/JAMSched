#ifndef JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#define JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#include <atomic>

namespace JAMScript
{

    class SpinLock
    {

        std::atomic_flag flag;

    public:

        SpinLock() : flag{false} {}
        void lock();
        bool try_lock();
        void unlock();

    };

} // namespace JAMScript
#endif