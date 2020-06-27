#ifndef JAMSCRIPT_NOTIFIER_HH
#define JAMSCRIPT_NOTIFIER_HH
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <concurrency/spinlock.hpp>
#include <boost/intrusive/list.hpp>

namespace JAMScript
{

    class TaskInterface;

    class Notifier
    {
    public:

        friend class TaskInterface;
        friend class TaskHandle;

        void Join();
        void Notify();
        void Join(std::unique_lock<JAMScript::SpinMutex> &iLock);
        void Notify(std::unique_lock<JAMScript::SpinMutex> &iLock);

        Notifier() : ownerTask(nullptr), lockWord(0) {}
        Notifier(TaskInterface *ownerTask) : ownerTask(ownerTask), lockWord(0) {}

    protected:

        TaskInterface *ownerTask;
        std::atomic<uint32_t> lockWord;
        SpinMutex m;
        std::condition_variable_any cv;
        
    };

    struct NotifierIdKeyType
    {
        typedef uintptr_t type;
        const type operator()(const Notifier &v) const { return reinterpret_cast<uintptr_t>(&v); }
    };

} // namespace JAMScript
#endif