#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include "io/io.h"
#include <type_traits>
#include "io/iocp_wrapper.h"
#include "core/task/task.hpp"
#include "concurrency/future.hpp"

#define JAMC_IO_SLEEP_INTERVAL_US 10

template <bool PreValidate = true>
int PollImpl(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if constexpr (PreValidate)
    {
        auto preValidationRes = poll(fds, nfds, 0);
        if (timeout == 0 || preValidationRes != 0)
        {
            return preValidationRes;
        }
    }
    auto evPollResult = std::make_unique<jamc::promise<std::unordered_map<int, short int>>>();
    auto evPollResultFuture = evPollResult->get_future();
    std::vector<std::pair<int, short int>> vecMergedFd;
    std::unordered_map<int, nfds_t> fd2poll;
    for (nfds_t i = 0; i < nfds; i++)
    {
        auto it = fd2poll.find(fds[i].fd);
        if (it == fd2poll.end())
        {
            fd2poll[fds[i].fd] = vecMergedFd.size();
            vecMergedFd.emplace_back(fds[i].fd, fds[i].events);
        }
        else
        {
            vecMergedFd[it->second].second |= fds[i].events;
        }
    }
    auto* evm = jamc::TaskInterface::GetIOCPAgent();
    evm->Add(vecMergedFd, evPollResult.get());
    if (timeout < 0)
    {
        printf("waiting for io...\n");
        evPollResultFuture.wait();
    }
    else
    {
        if (evPollResultFuture.wait_for(std::chrono::milliseconds(timeout)) == jamc::future_status::timeout)
        {
            evm->Cancel(evPollResult.get());
        }
    }
    if (evPollResultFuture.is_ready())
    {
        auto mapPollResult = evPollResultFuture.get();
        int ret = 0;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto it = mapPollResult.find(fds[i].fd);
            if (it != mapPollResult.end())
            {
                fds[i].revents = it->second & fds[i].events;
                if (fds[i].revents > 0)
                {
                    ret++;
                }
            }
        }
        return ret;
    }
    return 0;
}

int jamc::Poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return PollImpl(fds, nfds, timeout);
}

template <typename Fn, typename ...Args>
auto CallWrapper(int fd, short int event, int timeoutMS, Fn fn, Args && ... args) -> typename std::invoke_result<Fn, Args...>::type
{
    if (timeoutMS == 0)
    {
        return fn(std::forward<Args>(args)...);
    }
    struct pollfd fds{};
    fds.fd = fd;
    fds.events = event;
    fds.revents = 0;
    while (true)
    {
        int triggers = jamc::Poll(&fds, 1, timeoutMS);
        if (triggers == -1 && errno == EINTR)
        {
            continue;
        }
        else if (triggers == 0)
        {
            errno = EAGAIN;
            return -1;
        }
        else
        {
            break;
        }
    }
    while (true)
    {
        auto res = fn(std::forward<Args>(args)...);
        if (res == -1 && errno == EINTR)
        {
            continue;
        }
        else if (res == -1)
        {
            return -1;
        }
        else
        {
            return res;
        }
    }
}

int jamc::Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int timeoutMS)
{
    return CallWrapper(sockfd, POLLIN, timeoutMS, accept, sockfd, addr, addrlen);
}

int jamc::Connect(int socket, const struct sockaddr *address, socklen_t address_len, int timeoutMS)
{
    if (timeoutMS == 0)
    {
        return connect(socket, address, address_len);
    }
    int isNonBlocking = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, isNonBlocking | O_NONBLOCK);
    int res = connect(socket, address, address_len);
    fcntl(socket, F_SETFL, isNonBlocking);
    if (res == -1 && errno == EINPROGRESS)
    {
        struct pollfd pfd{};
        pfd.fd = socket;
        pfd.events = POLLOUT;
        int triggers = PollImpl<false>(&pfd, 1, timeoutMS);
        if (triggers <= 0 || pfd.revents != POLLOUT)
        {
            errno = ETIMEDOUT;
            return -1;
        }
        int sockErr = 0;
        socklen_t len = sizeof(int);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &sockErr, &len) == -1)
        {
            return -1;
        }
        if (!sockErr)
        {
            return 0;
        }
        errno = sockErr;
        return -1;
    }
    return res;
}

ssize_t jamc::Read(int fildes, void *buf, size_t nbyte, int timeoutMS)
{
    return CallWrapper(fildes, POLLIN, timeoutMS, read, fildes, buf, nbyte);
}

ssize_t jamc::Write(int fildes, const void *buf, size_t nbyte, int timeoutMS)
{
    return CallWrapper(fildes, POLLOUT, timeoutMS, write, fildes, buf, nbyte);
}

ssize_t jamc::Send(int socket, const void *buffer, size_t length, int flags, int timeoutMS)
{
    return CallWrapper(socket, POLLOUT, timeoutMS, send, socket, buffer, length, flags);
}

ssize_t jamc::Send(int socket, const void *message, size_t length, int flags,
                   const struct sockaddr *dest_addr, socklen_t dest_len, int timeoutMS)
{
    return CallWrapper(socket, POLLOUT, timeoutMS, sendto, socket, message, length, flags, dest_addr, dest_len);
}

ssize_t jamc::Recv(int socket, void *buffer, size_t length, int flags,
                   struct sockaddr *address, socklen_t *address_len, int timeoutMS)
{
    return CallWrapper(socket, POLLIN, timeoutMS, recvfrom, socket, buffer, length, flags, address, address_len);
}

ssize_t jamc::Recv(int socket, void *buffer, size_t length, int flags, int timeoutMS)
{
    return CallWrapper(socket, POLLIN, timeoutMS, recv, socket, buffer, length, flags);
}
#endif