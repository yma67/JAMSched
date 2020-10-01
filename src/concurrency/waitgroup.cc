#include "concurrency/waitgroup.hpp"

void JAMScript::WaitGroup::Add(int i)
{
    std::scoped_lock l(mu_);
    counter_ += i;
}

void JAMScript::WaitGroup::Done()
{
    {
        std::scoped_lock l(mu_);
        counter_--;
        if (counter_ <= 0) cv_.notify_all();
    }
}

void JAMScript::WaitGroup::Wait()
{
    std::unique_lock l(mu_);
    cv_.wait(l, [&] { return counter_ <= 0; });
}