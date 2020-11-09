#pragma once
#ifdef __APPLE__
#include <poll.h>
#include <cstdint>
#include <algorithm>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <type_traits>
#include "io/iocp_wrapper.h"
#include "core/task/task.hpp"
#include "concurrency/future.hpp"
namespace jamc
{
    // to 🐑 @yoo_yyx for contributing idea of #include <io.h>
    // timeout < 0: block forever
    // timeout > 0: timeout in ms
    // timeout == 0: non-blocking

    template <bool PreValidate = true,
              typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    int PollImpl(struct pollfd *fds, nfds_t nfds,
                 std::chrono::duration<TClock, TDur> tout = std::chrono::duration<TClock, TDur>::max())
    {
        if constexpr(PreValidate)
        {
            auto preValidationRes = poll(fds, nfds, 0);
            if (tout == std::chrono::duration<TClock, TDur>(0) || preValidationRes != 0)
            {
                return preValidationRes;
            }
        }
        auto evPollResult = std::make_unique<jamc::promise<std::unordered_map<int, std::uint16_t>>>();
        auto evPollResultFuture = evPollResult->get_future();
        std::vector<std::pair<int, std::uint16_t>> vecMergedFd;
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
                vecMergedFd[it->second].second |= std::uint16_t(fds[i].events);
            }
        }
        auto* evm = jamc::TaskInterface::GetIOCPAgent();
        evm->Add(vecMergedFd, evPollResult.get());
        if (tout == std::chrono::duration<TClock, TDur>::max())
        {
            evPollResultFuture.wait();
        }
        else
        {
            if (evPollResultFuture.wait_for(tout) == jamc::future_status::timeout)
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
                    fds[i].revents = std::int16_t(it->second & std::uint16_t(fds[i].events));
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

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    int Poll(struct pollfd *fds, nfds_t nfds,
             std::chrono::duration<TClock, TDur> tout = std::chrono::duration<TClock, TDur>::max())
    {
        return PollImpl(fds, nfds, tout);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    int PollWithoutPreValidation(struct pollfd *fds, nfds_t nfds,
                                 std::chrono::duration<TClock, TDur> tout = std::chrono::duration<TClock, TDur>::max())
    {
        return PollImpl<false>(fds, nfds, tout);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period,
              typename Fn, typename ...Args>
    auto CallWrapper(int fd, short int event, std::chrono::duration<TClock, TDur> tout,
                     Fn fn, Args && ... args) -> typename std::invoke_result<Fn, Args...>::type
    {
        if (tout == std::chrono::duration<TClock, TDur>(0))
        {
            return fn(std::forward<Args>(args)...);
        }
        struct pollfd fds{};
        fds.fd = fd;
        fds.events = event;
        fds.revents = 0;
        while (true)
        {
            int triggers = jamc::Poll(&fds, 1, tout);
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

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    int Accept(int socketFd, struct sockaddr *addr, socklen_t *addrlen,
               std::chrono::duration<TClock, TDur> tout = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socketFd, POLLIN, tout, accept, socketFd, addr, addrlen);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    int Connect(int socket, const struct sockaddr *address, socklen_t address_len,
                std::chrono::duration<TClock, TDur> tout = std::chrono::duration<TClock, TDur>::max())
    {
        if (tout == std::chrono::duration<TClock, TDur>(0))
        {
            return connect(socket, address, address_len);
        }
        int isNonBlocking = fcntl(socket, F_GETFL, 0);
        fcntl(socket, unsigned(F_SETFL), unsigned(isNonBlocking) | unsigned(O_NONBLOCK));
        int res = connect(socket, address, address_len);
        fcntl(socket, unsigned(F_SETFL), isNonBlocking);
        if (res == -1 && errno == EINPROGRESS)
        {
            struct pollfd pfd{};
            pfd.fd = socket;
            pfd.events = POLLOUT;
            int triggers = PollImpl<false>(&pfd, 1, tout);
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

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Read(int fildes, void *buf, size_t nByte,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(fildes, std::uint16_t(POLLIN) | std::uint16_t(POLLHUP),
                           timeoutMS, read, fildes, buf, nByte);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Write(int fildes, const void *buf, size_t nByte,
                  std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(fildes, std::uint16_t(POLLOUT) | std::uint16_t(POLLHUP),
                           timeoutMS, write, fildes, buf, nByte);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Send(int socket, const void *buffer, size_t length, int flags,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socket, std::uint16_t(POLLOUT) | std::uint16_t(POLLHUP),
                           timeoutMS, send, socket, buffer, length, flags);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Send(int socket, const void *message, size_t length,
                 int flags, const struct sockaddr *dest_addr, socklen_t dest_len,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socket, std::uint16_t(POLLOUT) | std::uint16_t(POLLHUP),
                           timeoutMS, sendto, socket, message, length, flags, dest_addr, dest_len);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Recv(int socket, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_len,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socket, std::uint16_t(POLLIN) | std::uint16_t(POLLHUP),
                           timeoutMS, recvfrom, socket, buffer, length, flags, address, address_len);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Recv(int socket, void *buffer, size_t length, int flags,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socket, std::uint16_t(POLLIN) | std::uint16_t(POLLHUP),
                           timeoutMS, recv, socket, buffer, length, flags);
    }
}
#endif