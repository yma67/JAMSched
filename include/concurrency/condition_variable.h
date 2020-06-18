#ifndef JAMSCRIPT_CV_HH
#define JAMSCRIPT_CV_HH
#include <mutex>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <boost/assert.hpp>

#include "time/time.h"
#include "core/task/task.h"
#include "concurrency/notifier.h"
#include "concurrency/spinlock.h"

namespace JAMScript {

    template <typename Clock, typename Duration>
    std::chrono::steady_clock::time_point convert(std::chrono::time_point<Clock, Duration> const &timeout_time) {
        return std::chrono::steady_clock::now() + (timeout_time - Clock::now());
    }

    class ConditionVariableAny {
    public:

        void notify_one() noexcept;
        void notify_all() noexcept;

        template <typename Tl>
        void wait(Tl &li) {
            std::unique_lock<SpinLock> lkList(wListLock);
            Notifier *f = new Notifier(ThisTask::Active());
            waitSet.insert(*f);
            li.unlock();
            f->Join(lkList);
            li.lock();
            delete f;
        }

        template <typename Tl, typename Tp>
        void wait(Tl &li, Tp pred) {
            while (!pred()) {
                wait(li);
            }
        }

        template <typename Tl, typename _Clock, typename _Dur>
        std::cv_status wait_until(Tl &lt, std::chrono::time_point<_Clock, _Dur> const &timeoutTime_) {
            std::cv_status isTimeout = std::cv_status::no_timeout;
            TimePoint timeoutTime = convert(timeoutTime_);
            std::unique_lock<SpinLock> lk(wListLock);
            Notifier *n = new Notifier(ThisTask::Active());
            waitSet.insert(*n);
            lt.unlock();
            ThisTask::SleepUntil(timeoutTime, lk, n);
            delete n;
            if (Clock::now() >= timeoutTime)
                isTimeout = std::cv_status::timeout;
            return isTimeout;
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        bool wait_until(Tl &lt, std::chrono::time_point<_Clock, _Dur> const &timeout_time, Tp pred) {
            while (!pred()) {
                if (std::cv_status::timeout == wait_until(lt, timeout_time)) {
                    return pred();
                }
            }
            return true;
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        bool wait_for(Tl &lt, std::chrono::duration<_Clock, _Dur> const &timeout_duration, Tp pred) {
            return wait_until(lt, std::chrono::steady_clock::now() + timeout_duration, pred);
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        std::cv_status wait_for(Tl &lt, std::chrono::duration<_Clock, _Dur> const &timeout_duration) {
            return wait_until(lt, std::chrono::steady_clock::now() + timeout_duration);
        }

        ConditionVariableAny() = default;

        ~ConditionVariableAny() {
            waitSet.clear_and_dispose([](Notifier *nf) { delete nf; });
        }

    private:

        ConditionVariableAny(ConditionVariableAny const&) = delete;
        ConditionVariableAny& operator=(ConditionVariableAny const&) = delete;
        ConditionVariableAny& operator=(ConditionVariableAny&&) = delete;
        ConditionVariableAny(ConditionVariableAny&&) = delete;

        JAMStorageTypes::NotifierCVSetType waitSet;
        SpinLock wListLock;

    };

    class ConditionVariable {
    public:

        void notify_one() noexcept {
            cv.notify_one();
        }
        
        void notify_all() noexcept {
            cv.notify_all();
        }

        template <typename Tl>
        void wait(Tl &li) {
            cv.wait(li);
        }

        template <typename Tl, typename Tp>
        void wait(Tl &li, Tp pred) {
            cv.wait(li, pred);
        }

        template <typename Tl, typename _Clock, typename _Dur>
        std::cv_status wait_until(Tl &lt, std::chrono::time_point<_Clock, _Dur> const &timeoutTime_) {
            return cv.wait_until(lt, timeoutTime_);
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        bool wait_until(Tl &lt, std::chrono::time_point<_Clock, _Dur> const &timeout_time, Tp pred) {
            return cv.wait_until(lt, timeout_time, pred);
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        bool wait_for(Tl &lt, std::chrono::duration<_Clock, _Dur> const &timeout_duration, Tp pred) {
            return cv.wait_for(lt, timeout_duration, pred);
        }

        template <typename Tl, typename _Clock, typename _Dur, typename Tp>
        std::cv_status wait_for(Tl &lt, std::chrono::duration<_Clock, _Dur> const &timeout_duration) {
            return cv.wait_for(lt, timeout_duration);
        }

        ConditionVariable() = default;

    private:

        ConditionVariable(ConditionVariable const&) = delete;
        ConditionVariable& operator=(ConditionVariable const&) = delete;
        ConditionVariable& operator=(ConditionVariable&&) = delete;
        ConditionVariable(ConditionVariable&&) = delete;

        ConditionVariableAny cv;

    };

}  // namespace JAMScript
#endif