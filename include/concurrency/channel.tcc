#include <utility>

template <typename T>
constexpr JAMScript::Channel<T>::Channel(const size_type capacity) : cap{capacity}, is_closed{false}
{
}

template <typename T>
constexpr typename JAMScript::Channel<T>::size_type JAMScript::Channel<T>::size() const noexcept
{
    return queue.size();
}

template <typename T>
constexpr bool JAMScript::Channel<T>::empty() const noexcept
{
    return queue.empty();
}

template <typename T>
void JAMScript::Channel<T>::close() noexcept
{
    cnd.notify_one();
    is_closed.store(true);
}

template <typename T>
bool JAMScript::Channel<T>::closed() const noexcept
{
    return is_closed.load();
}

template <typename T>
BlockingIterator<JAMScript::Channel<T>> JAMScript::Channel<T>::begin() noexcept
{
    return BlockingIterator<Channel<T>>{*this};
}

template <typename T>
BlockingIterator<JAMScript::Channel<T>> JAMScript::Channel<T>::end() noexcept
{
    return BlockingIterator<Channel<T>>{*this};
}

template <typename T>
void operator>>(const T& in, JAMScript::Channel<T>& ch)
{
    if (ch.closed()) {
        throw ClosedChannel{"cannot write on closed channel"};
    }

    std::unique_lock lock{ch.mtx};

    if (ch.cap > 0 && ch.queue.size() == ch.cap) {
        ch.cnd.wait(lock, [&ch]() { return ch.queue.size() < ch.cap; });
    }

    ch.queue.push_back(in);

    ch.cnd.notify_one();
}

template <typename T>
void operator>>(T&& in, JAMScript::Channel<T>& ch)
{
    if (ch.closed()) {
        throw ClosedChannel{"cannot write on closed channel"};
    }

    std::unique_lock lock{ch.mtx};

    if (ch.cap > 0 && ch.queue.size() == ch.cap) {
        ch.cnd.wait(lock, [&ch]() { return ch.queue.size() < ch.cap; });
    }

    ch.queue.push_back(std::forward<T>(in));

    ch.cnd.notify_one();
}

template <typename T>
void operator<<(T& out, JAMScript::Channel<T>& ch)
{
    if (ch.closed() && ch.empty()) {
        return;
    }

    std::unique_lock lock{ch.mtx};
    ch.cnd.wait(lock, [&ch] { return ch.queue.size() > 0 || ch.closed(); });

    if (ch.queue.size() > 0) {
        out = std::move(ch.queue.front());
        ch.queue.pop_front();
    }
    ch.cnd.notify_one();
}