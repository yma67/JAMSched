#ifndef JAMSCRIPT_JAMSCRIPT_FUTURE_H
#define JAMSCRIPT_JAMSCRIPT_FUTURE_H
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <iostream>
#include <atomic>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include "future/future.h"
#include "core/scheduler/task.h"

namespace jamscript {

void interactive_task_handle_post_callback(jamfuture_t *self);

using std::future_status;

template <typename T>
struct late_initialized {
public:
    late_initialized() = default;
    late_initialized(late_initialized&&) = delete;
    late_initialized& operator=(late_initialized&&) = delete;
    ~late_initialized() {
        if(initialized) ptr()->~T(); 
    }
    template <typename... Args>
    void initialize(Args&&... args) {
        ::new(ptr()) T(std::forward<Args>(args)...);
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
    using storage_type = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
    bool initialized = false;
    storage_type storage;
};

template <typename T>
struct future : public std::enable_shared_from_this<future<T>> {
public:
    future() : f(std::make_unique<jamfuture_t>()) {
        make_future(f.get(), this_task(), nullptr, interactive_task_handle_post_callback);
    }
    void wait() const {
        get_future(f.get());
    }
    T& get() {
        get_future(f.get());
        if (error || f->status != ack_finished) 
            std::rethrow_exception(error);
        if (state) return *state;
        throw std::runtime_error("abnormal");
    }
    template <typename U>
    void set_value(U&& value) {
        state.initialize(std::forward<U>(value));
        notify_all();
    }
    void set_exception(std::exception_ptr e) {
        error = e;
        f->status = ack_failed;
        notify_all();
    }
    std::shared_ptr<future<T>> getptr() {
        return this->shared_from_this();
    }
private:
    mutable std::unique_ptr<jamfuture_t> f;
    late_initialized<T> state;
    std::exception_ptr error;
    void notify_all() {
        f->status = ack_finished;
        notify_future(f.get());
    }
};
    
}
#endif