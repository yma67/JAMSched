#ifndef WAITGROUP_
#define WAITGROUP_

#include <concurrency/condition_variable.hpp>
#include <atomic>
#include <thread>

namespace JAMScript {
    class WaitGroup {
    public:
        void Add(int i = 1);
        void Done();
        void Wait();

    private:
        SpinOnlyMutex mu_;
        std::atomic<int> counter_;
        ConditionVariable cv_;
    };
}


#endif // WAITGROUP_