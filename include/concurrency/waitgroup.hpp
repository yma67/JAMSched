#ifndef WAITGROUP_
#define WAITGROUP_

#include <concurrency/condition_variable.hpp>
#include <atomic>
#include <thread>

namespace JAMScript
{
    class WaitGroup 
    {
    public:
        void Add(int incr = 1);
        void Done();
        void Wait();

    private:
        int c;
        SpinOnlyMutex m;
        ConditionVariable cv;
    };
}


#endif // WAITGROUP_