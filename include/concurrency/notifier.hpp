#ifndef JAMSCRIPT_NOTIFIER_HH
#define JAMSCRIPT_NOTIFIER_HH
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <concurrency/spinlock.hpp>
#include <boost/intrusive/set.hpp>

namespace JAMScript {

    class TaskInterface;

    namespace JAMHookTypes {

        struct NotifierSetTag;
        typedef boost::intrusive::set_member_hook<
            boost::intrusive::tag<NotifierSetTag>
        >   NotifierSetHook;

        struct NotifierSetCVTag;
        typedef boost::intrusive::set_member_hook<
            boost::intrusive::tag<NotifierSetCVTag>
        >   NotifierSetCVHook;

    }  // namespace JAMHookTypes

    class Notifier {
    public:

        friend class TaskInterface;

        JAMHookTypes::NotifierSetHook notifierHook;
        JAMHookTypes::NotifierSetCVHook notifierHookCV;

        void Join();
        void Notify();
        void Join(std::unique_lock<JAMScript::SpinLock>& iLock);
        void Notify(std::unique_lock<JAMScript::SpinLock>& iLock);

        Notifier() : ownerTask(nullptr), lockWord(0) {}
        Notifier(TaskInterface* ownerTask) : ownerTask(ownerTask), lockWord(0) {}

    protected:

        TaskInterface* ownerTask;
        uint32_t lockWord;
        SpinLock m;
        std::condition_variable_any cv;
    
    };

    struct NotifierIdKeyType {
        typedef uintptr_t type;
        const type operator()(const Notifier& v) const { return reinterpret_cast<uintptr_t>(&v); }
    };

    namespace JAMStorageTypes {

        typedef boost::intrusive::set<
            Notifier, boost::intrusive::member_hook<Notifier, JAMHookTypes::NotifierSetHook, &Notifier::notifierHook>,
            boost::intrusive::key_of_value<NotifierIdKeyType>>
            NotifierSetType;

        typedef boost::intrusive::set<
            Notifier,
            boost::intrusive::member_hook<Notifier, JAMHookTypes::NotifierSetCVHook, &Notifier::notifierHookCV>,
            boost::intrusive::key_of_value<NotifierIdKeyType>>
            NotifierCVSetType;
            
    }  // namespace JAMStorageTypes

}  // namespace JAMScript
#endif