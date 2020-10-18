#include <utility>

template <typename T>
constexpr jamc::Channel<T>::Channel(const size_type capacity) : cap{capacity}, is_closed{false}
{
}

template <typename T>
constexpr typename jamc::Channel<T>::size_type jamc::Channel<T>::size() const noexcept
{
    return queue.size();
}

template <typename T>
constexpr bool jamc::Channel<T>::empty() const noexcept
{
    return queue.empty();
}

template <typename T>
void jamc::Channel<T>::close() noexcept
{
    cnd.notify_one();
    is_closed.store(true);
}

template <typename T>
bool jamc::Channel<T>::closed() const noexcept
{
    return is_closed.load();
}

template <typename T>
BlockingIterator<jamc::Channel<T>> jamc::Channel<T>::begin() noexcept
{
    return BlockingIterator<Channel<T>>{*this};
}

template <typename T>
BlockingIterator<jamc::Channel<T>> jamc::Channel<T>::end() noexcept
{
    return BlockingIterator<Channel<T>>{*this};
}

template <typename T>
void operator>>(const T& in, jamc::Channel<T>& ch)
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
void operator>>(T&& in, jamc::Channel<T>& ch)
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
void operator<<(T& out, jamc::Channel<T>& ch)
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