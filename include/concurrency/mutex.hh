#ifndef JAMSCRIPT_MUTEX_HH
#define JAMSCRIPT_MUTEX_HH
#include <mutex>

#include "concurrency/notifier.hh"
#include "concurrency/spinlock.hh"
namespace JAMScript {
    class FIFOTaskMutex {
    public:
        bool try_lock();
        void lock();
        void unlock();
        FIFOTaskMutex() {}

    private:
        SpinLock qLock;
        int m_state;
        static constexpr int UNLOCKED = 0;
        static constexpr int LOCKED = 1;
        JAMStorageTypes::NotifierSetType waitSet;
    };
}  // namespace JAMScript
#endif