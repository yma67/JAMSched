#ifndef JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#define JAMSCRIPT_JAMSCRIPT_SPINLOCK_HH
#include <atomic>
namespace JAMScript {

    class SpinLock {

        std::atomic_flag flag;

    public:

        SpinLock() : flag{false} {}

        inline void lock() {
            while (flag.test_and_set(std::memory_order_acquire))
                ;
        }

        inline bool try_lock() { return !flag.test_and_set(std::memory_order_acquire); }

        inline void unlock() { flag.clear(std::memory_order_release); }
        
    };

}  // namespace JAMScript
#endif