#ifndef JAMSCRIPT_FUTURE_HH
#define JAMSCRIPT_FUTURE_HH
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

#include "concurrency/condition_variable.hpp"

namespace JAMScript
{

    using std::future_status;
    // using LockType = FIFOTaskMutex;
    using LockType = SpinMutex;

    namespace detail
    {
        // quick-and-dirty optional-like brick type
        template <typename T>
        struct LateInitialized
        {
        public:
            LateInitialized() = default;

            LateInitialized(LateInitialized &&) = delete;
            LateInitialized &operator=(LateInitialized &&) = delete;

            ~LateInitialized()
            {
                if (initialized)
                    ptr()->~T();
            }

            template <typename... Args>
            void Initialize(Args &&... args)
            {
                ::new (ptr()) T(std::forward<Args>(args)...);
                initialized = true;
            }

            explicit operator bool() const { return initialized; }

            T &operator*() { return *ptr(); }
            T const &operator*() const { return ptr(); }
            T *operator->() { return *ptr(); }
            T const *operator->() const { return ptr(); }

        private:
            T *ptr() { return static_cast<T *>(static_cast<void *>(&storage)); }
            T const *ptr() const { return static_cast<T *>(static_cast<void *>(&storage)); }

            using storage_type = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
            bool initialized = false;
            storage_type storage;
        };

        template <typename T>
        struct future_shared_state
        {
        public:
            void wait() const
            {
                std::unique_lock<LockType> ul(mtx);
                available.wait(ul, [&] { return state || error; });
            }

            T &get()
            {
                std::unique_lock<LockType> ul(mtx);
                available.wait(ul, [&] { return state || error; });
                if (state)
                    return *state;
                if (error)
                    std::rethrow_exception(error);
                throw std::runtime_error("WTF");
            }

            template <typename U>
            void set_value(U &&value)
            {
                std::unique_lock<LockType> ul(mtx);
                state.Initialize(std::forward<U>(value));
                available.notify_all();
            }
            void set_exception(std::exception_ptr e)
            {
                std::unique_lock<LockType> ul(mtx);
                error = e;
                available.notify_all();
            }

        private:
            mutable ConditionVariableAny available;
            mutable LockType mtx;
            LateInitialized<T> state;
            std::exception_ptr error;
        };

        template <typename T>
        using future_shared_state_box = future_shared_state<T>;
    } // namespace detail

    template <typename T>
    struct Promise;

    template <typename T>
    struct Future
    {
    public:
        Future() noexcept = default;
        Future(Future &&) noexcept = default;
        Future(Future const &other) = delete;

        ~Future() = default;

        Future &operator=(Future &&other) noexcept = default;
        Future &operator=(Future const &other) = delete;

        // shared_future<T> share();

        T Get() { return std::move(box->get()); }

        bool Valid() const noexcept { return box != nullptr; }

        void Wait() const { box->wait(); }

    private:
        std::shared_ptr<detail::future_shared_state_box<T>> box = nullptr;

        friend struct Promise<T>;
        Future(std::shared_ptr<detail::future_shared_state_box<T>> const &box) : box(box) {}
        Future(std::shared_ptr<detail::future_shared_state_box<T>> &&box) : box(std::move(box)) {}
    };

    template <typename T>
    struct Promise
    {
    public:
        Promise() : box(std::make_shared<detail::future_shared_state_box<T>>()) {}
        template <typename Alloc>
        Promise(std::allocator_arg_t, Alloc const &alloc)
            : box(std::allocate_shared<detail::future_shared_state_box<T>>(alloc)) {}
        Promise(Promise &&other) noexcept = default;
        Promise(Promise const &other) = delete;

        ~Promise() = default;

        Promise &operator=(Promise &&other) noexcept = default;
        Promise &operator=(Promise const &rhs) = delete;

        void swap(Promise &other) noexcept { box.swap(other.box); }

        Future<T> GetFuture() { return {box}; }

        void SetValue(T const &value) { box->set_value(value); }
        void SetValue(T &&value) { box->set_value(std::move(value)); }

        // void set_value_at_thread_exit(T const& value);
        // void set_value_at_thread_exit(T&& value);

        void SetException(std::exception_ptr e) { box->set_exception(std::move(e)); }
        // void set_exception_at_thread_exit(std::exception_ptr e);

    private:
        std::shared_ptr<detail::future_shared_state_box<T>> box;
    };

    template <typename T>
    void swap(Promise<T> &lhs, Promise<T> &rhs)
    {
        lhs.swap(rhs);
    }

    // for void
    namespace detail
    {
        template <>
        struct future_shared_state<void>
        {
        public:
            void wait() const
            {
                std::unique_lock<LockType> ul(mtx);
                available.wait(ul, [&] { return state || error; });
            }

            void get()
            {
                std::unique_lock<LockType> ul(mtx);
                available.wait(ul, [&] { return state || error; });
                if (state)
                    return;
                if (error)
                    std::rethrow_exception(error);
                throw std::runtime_error("WTF");
            }

            void set_value()
            {
                std::unique_lock<LockType> ul(mtx);
                state = true;
                available.notify_all();
            }
            void set_exception(std::exception_ptr e)
            {
                std::unique_lock<LockType> ul(mtx);
                error = e;
                available.notify_all();
            }

        private:
            mutable ConditionVariableAny available;
            mutable LockType mtx;
            bool state;
            std::exception_ptr error;
        };
    } // namespace detail

    template <>
    struct Promise<void>;

    template <>
    struct Future<void>
    {
    public:
        Future() noexcept = default;
        Future(Future &&) noexcept = default;
        Future(Future const &other) = delete;

        ~Future() = default;

        Future &operator=(Future &&other) noexcept = default;
        Future &operator=(Future const &other) = delete;

        // shared_future<T> share();

        void Get() { return box->get(); }

        bool Valid() const noexcept { return box != nullptr; }

        void Wait() const { box->wait(); }

    private:
        std::shared_ptr<detail::future_shared_state_box<void>> box = nullptr;

        friend struct Promise<void>;
        Future(std::shared_ptr<detail::future_shared_state_box<void>> const &box) : box(box) {}
        Future(std::shared_ptr<detail::future_shared_state_box<void>> &&box) : box(std::move(box)) {}
    };

    template <>
    struct Promise<void>
    {
    public:
        Promise() : box(std::make_shared<detail::future_shared_state_box<void>>()) {}
        template <typename Alloc>
        Promise(std::allocator_arg_t, Alloc const &alloc)
            : box(std::allocate_shared<detail::future_shared_state_box<void>>(alloc)) {}
        Promise(Promise &&other) noexcept = default;
        Promise(Promise const &other) = delete;

        ~Promise() = default;

        Promise &operator=(Promise &&other) noexcept = default;
        Promise &operator=(Promise const &rhs) = delete;

        void swap(Promise &other) noexcept { box.swap(other.box); }

        Future<void> GetFuture() { return {box}; }

        void SetValue() { box->set_value(); }

        // void set_value_at_thread_exit(T const& value);
        // void set_value_at_thread_exit(T&& value);

        void SetException(std::exception_ptr e) { box->set_exception(std::move(e)); }
        // void set_exception_at_thread_exit(std::exception_ptr e);

    private:
        std::shared_ptr<detail::future_shared_state_box<void>> box;
    };

    //template <>
    //void swap(Promise<void> &lhs, Promise<void> &rhs)
    //{
    //    lhs.swap(rhs);
    //}

    // for reference type
    namespace detail
    {
        template <typename R>
        struct future_shared_state<R &>
        {
        public:
            void wait() const
            {
                std::unique_lock<LockType> ul(mtx);
                available.wait(ul, [&] { return state || error; });
            }

            R &get()
            {
                std::unique_lock<LockType> ul(mtx);
                available.wait(ul, [&] { return state || error; });
                if (state)
                    return *state;
                if (error)
                    std::rethrow_exception(error);
                throw std::runtime_error("WTF");
            }

            template <typename U>
            void set_value(const U &value)
            {
                std::unique_lock<LockType> ul(mtx);
                state = const_cast<U *>(&value);
                available.notify_all();
            }
            void set_exception(std::exception_ptr e)
            {
                std::unique_lock<LockType> ul(mtx);
                error = e;
                available.notify_all();
            }

        private:
            mutable ConditionVariableAny available;
            mutable LockType mtx;
            R *state{nullptr};
            std::exception_ptr error;
        };
    } // namespace detail

    template <typename R>
    struct Promise<R &>;

    template <typename R>
    struct Future<R &>
    {
    public:
        Future() noexcept = default;
        Future(Future &&) noexcept = default;
        Future(Future const &other) = delete;

        ~Future() = default;

        Future &operator=(Future &&other) noexcept = default;
        Future &operator=(Future const &other) = delete;

        // shared_future<T> share();

        R &Get() { return box->get(); }

        bool Valid() const noexcept { return box != nullptr; }

        void Wait() const { box->wait(); }

    private:
        std::shared_ptr<detail::future_shared_state_box<R &>> box = nullptr;

        friend struct Promise<R &>;
        Future(std::shared_ptr<detail::future_shared_state_box<R &>> const &box) : box(box) {}
        Future(std::shared_ptr<detail::future_shared_state_box<R &>> &&box) : box(std::move(box)) {}
    };

    template <typename R>
    struct Promise<R &>
    {
    public:
        Promise() : box(std::make_shared<detail::future_shared_state_box<R &>>()) {}
        template <typename Alloc>
        Promise(std::allocator_arg_t, Alloc const &alloc)
            : box(std::allocate_shared<detail::future_shared_state_box<R &>>(alloc)) {}
        Promise(Promise &&other) noexcept = default;
        Promise(Promise const &other) = delete;

        ~Promise() = default;

        Promise &operator=(Promise &&other) noexcept = default;
        Promise &operator=(Promise const &rhs) = delete;

        void swap(Promise &other) noexcept { box.swap(other.box); }

        Future<R &> GetFuture() { return {box}; }

        void SetValue(R const &value) { box->set_value(value); }
        void SetValue(R &&value) { box->set_value(std::move(value)); }

        // void set_value_at_thread_exit(T const& value);
        // void set_value_at_thread_exit(T&& value);

        void SetException(std::exception_ptr e) { box->set_exception(std::move(e)); }
        // void set_exception_at_thread_exit(std::exception_ptr e);

    private:
        std::shared_ptr<detail::future_shared_state_box<R &>> box;
    };

    template <typename R>
    void swap(Promise<R &> &lhs, Promise<R &> &rhs)
    {
        lhs.swap(rhs);
    }
} // namespace JAMScript
#endif