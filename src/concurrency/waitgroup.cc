#include "concurrency/waitgroup.hpp"

void JAMScript::WaitGroup::Add(int incr)
{
    std::scoped_lock l(m);
    c += incr;
}

void JAMScript::WaitGroup::Done()
{
    std::scoped_lock l(m);
    c--;
    if (c <= 0) cv.notify_all();
}

void JAMScript::WaitGroup::Wait()
{
    std::unique_lock l(m);
    while (c > 0) cv.wait(m);
}