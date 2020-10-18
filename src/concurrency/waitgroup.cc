#include "concurrency/waitgroup.hpp"

void jamc::WaitGroup::Add(int incr)
{
    std::scoped_lock l(m);
    c += incr;
}

void jamc::WaitGroup::Done()
{
    std::scoped_lock l(m);
    c--;
    if (c <= 0) cv.notify_all();
}

void jamc::WaitGroup::Wait()
{
    std::unique_lock l(m);
    while (c > 0) cv.wait(m);
}