#ifndef JAMSCRIPT_CV_HH
#define JAMSCRIPT_CV_HH
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "concurrency/notifier.hh"
#include "concurrency/spinlock.hh"
#include "core/task/task.hh"
#include "time/time.hh"

namespace JAMScript {
    template <typename Clock, typename Duration>
    std::chrono::steady_clock::time_point convert(
        std::chrono::time_point<Clock, Duration> const& timeout_time) {
        return std::chrono::steady_clock::now() + (timeout_time - Clock::now());
    }
    class ConditionVariableAny {
    public:
        void notify_one() noexcept;
        void notify_all() noexcept;
        template <typename Tl>
        void wait(Tl& li) {
            std::unique_lock<SpinLock> lkList(wListLock);
            Notifier* f = new Notifier(ThisTask::Active());
            waitSet.insert(*f);
            li.unlock();
            f->Join(lkList);
            li.lock();
            delete f;
        }
        template <typename Tl, typename Tp>
        void wait(Tl& li, Tp pred) {
            while (!pred()) {
                wait(li);
            }
        }

        template <typename Tl, typename _Clock, typename _Dur>
        std::cv_status wait_until(Tl& lt,
                                  std::chrono::time_point<_Clock, _Dur> const& timeoutTime_) {
            std::cv_status isTimeout = std::cv_status::no_timeout;
            TimePoint timeoutTime = convert(timeoutTime_);
            std::unique_lock<SpinLock> lk(wListLock);
            Notifier* n = new Notifier(ThisTask::Active());
            waitSet.insert(*n);
            lt.unlock();
            ThisTask::SleepUntil(timeoutTime, lk, n);
            delete n;
            if (Clock::now() >= timeoutTime)
                isTimeout = std::cv_status::timeout;
            return isTimeout;
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        bool wait_until(Tl& lt, std::chrono::time_point<_Clock, _Dur> const& timeout_time,
                        Tp pred) {
            while (!pred()) {
                if (std::cv_status::timeout == wait_until(lt, timeout_time)) {
                    return pred();
                }
            }
            return true;
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        bool wait_for(Tl& lt, std::chrono::duration<_Clock, _Dur> const& timeout_duration,
                      Tp pred) {
            return wait_until(lt, std::chrono::steady_clock::now() + timeout_duration, pred);
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        std::cv_status wait_for(Tl& lt,
                                std::chrono::duration<_Clock, _Dur> const& timeout_duration) {
            return wait_until(lt, std::chrono::steady_clock::now() + timeout_duration);
        }
        ~ConditionVariableAny() {
            waitSet.clear_and_dispose([](Notifier* nf) { delete nf; });
        }

    private:
        JAMStorageTypes::NotifierCVSetType waitSet;
        SpinLock wListLock;
    };
}  // namespace JAMScript
#endif