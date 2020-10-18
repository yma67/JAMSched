#include <mutex>
#include <iostream>
#include "concurrency/notifier.hpp"

void jamc::Notifier::Join()
{
    std::unique_lock lk(m);
    while (!isFinished) cv.wait(lk);
}

void jamc::Notifier::Notify()
{
    std::unique_lock lk(m);
    isFinished = true;
    cv.notify_all();
}