#ifndef WAITGROUP_
#define WAITGROUP_

#include <atomic>
#include <thread>
#include "concurrency/condition_variable.hpp"

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