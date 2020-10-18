#ifndef JAMSCRIPT_NOTIFIER_HH
#define JAMSCRIPT_NOTIFIER_HH
#include <mutex>
#include <thread>
#include <atomic>
#include "concurrency/spinlock.hpp"
#include "concurrency/condition_variable.hpp"

#include <boost/intrusive/list.hpp>

namespace jamc
{
    class ConditionVariable;
    class Notifier
    {
    public:

        void Join();
        void Notify();

    private:

        bool isFinished = false;
        SpinMutex m;
        ConditionVariable cv;
        
    };

    struct NotifierIdKeyType
    {
        typedef uintptr_t type;
        const type operator()(const Notifier &v) const { return reinterpret_cast<uintptr_t>(&v); }
    };

} // namespace jamc
#endif