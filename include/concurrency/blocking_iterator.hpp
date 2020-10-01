#ifndef BLOCKING_ITERATOR_HPP_
#define BLOCKING_ITERATOR_HPP_

#include <iterator>
/**
 *  @brief An iterator that block the current thread,
 *  waiting to fetch elements from the channel.
 *
 *  Used to implement channel range-based for loop.
 *
 *  @tparam Channel Instance of channel.
 */
template <typename Channel>
class BlockingIterator {
public:
    using value_type = typename Channel::value_type;

    explicit BlockingIterator(Channel& ch) : ch{ch} {}

    /**
     * Advances to next element in the channel.
     */
    BlockingIterator<Channel> operator++() const noexcept { return *this; }

    /**
     * Returns an element from the channel.
     */
    value_type operator*() const
    {
        value_type value{};
        value << ch;

        return value;
    }

    /**
     * Makes iteration continue until the channel is closed and empty.
     */
    bool operator!=(BlockingIterator<Channel>) const noexcept { return !(ch.closed() && ch.empty()); }

private:
    Channel& ch;
};

/**
 * @brief Output iterator specialization
 */
template <typename T>
struct std::iterator_traits<BlockingIterator<T>> {
    using value_type = typename BlockingIterator<T>::value_type;
    using iterator_category = std::output_iterator_tag;
};

#endif  // BLOCKING_ITERATOR_HPP_