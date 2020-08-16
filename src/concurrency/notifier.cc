#include <mutex>
#include <iostream>
#include "concurrency/notifier.hpp"

void JAMScript::Notifier::Join()
{
    std::unique_lock lk(m);
    while (!isFinished) cv.wait(lk);
}

void JAMScript::Notifier::Notify()
{
    std::unique_lock lk(m);
    isFinished = true;
    cv.notify_all();
}