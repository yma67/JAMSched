#ifndef JAMSCRIPT_JAMSCRIPT_FUTURE_H
#define JAMSCRIPT_JAMSCRIPT_FUTURE_H
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include "core/scheduler/task.h"
#include "future/future.h"

namespace JAMScript {

    void InteractiveTaskHandlePostCallback(CFuture* self);

    using std::future_status;

    template <typename T>
    struct LateInitialized {
    public:
        LateInitialized() = default;
        LateInitialized(LateInitialized&&) = delete;
        LateInitialized& operator=(LateInitialized&&) = delete;
        ~LateInitialized() {
            if (initialized)
                ptr()->~T();
        }
        template <typename... Args>
        void Initialize(Args&&... args) {
            ::new (ptr()) T(std::forward<Args>(args)...);
            initialized = true;
        }
        explicit operator bool() const { return initialized; }
        T& operator*() { return *ptr(); }
        T const& operator*() const { return ptr(); }
        T* operator->() { return *ptr(); }
        T const* operator->() const { return ptr(); }

    private:
        T* ptr() { return static_cast<T*>(static_cast<void*>(&storage)); }
        T const* ptr() const { return static_cast<T*>(static_cast<void*>(&storage)); }
        using StorageType = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
        bool initialized = false;
        StorageType storage;
    };

    template <typename T>
    struct Future : public std::enable_shared_from_this<Future<T>> {
    public:
        Future() : f(std::make_unique<CFuture>()) {
            CreateFuture(f.get(), GetCurrentTaskRunning(), nullptr, InteractiveTaskHandlePostCallback);
        }
        void Wait() const { WaitForValueFromFuture(f.get()); }
        T& Get() {
            WaitForValueFromFuture(f.get());
            if (error || f->status != ACK_FINISHED)
                std::rethrow_exception(error);
            if (state)
                return *state;
            throw std::runtime_error("abnormal");
        }
        template <typename U>
        void SetValue(U&& value) {
            state.Initialize(std::forward<U>(value));
            notify_all();
        }
        void SetException(std::exception_ptr e) {
            error = e;
            f->status = ACK_FAILED;
            notify_all();
        }
        std::shared_ptr<Future<T>> getptr() { return this->shared_from_this(); }

    private:
        Future(Future const&) = delete;
        Future(Future&&) = delete;
        Future& operator=(Future const&) = delete;
        Future& operator=(Future&&) = delete;
        mutable std::unique_ptr<CFuture> f;
        LateInitialized<T> state;
        std::exception_ptr error;
        void notify_all() {
            f->status = ACK_FINISHED;
            NotifyFinishOfFuture(f.get());
        }
    };

}  // namespace JAMScript
#endif