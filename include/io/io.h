#pragma once
#if defined(__APPLE__) || defined(__linux__)
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
        auto evPollResult = std::make_unique<jamc::promise<std::vector<std::pair<int, std::uint16_t>>>>();
        auto evPollResultFuture = evPollResult->get_future();
        std::vector<std::pair<int, std::uint16_t>> vecMergedFd;
        std::unordered_map<int, nfds_t> fd2poll;
        for (nfds_t i = 0; i < nfds; i++)
        {
            fds[i].revents = 0;
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
            int ret = mapPollResult.size();
            for (auto& [fdRes, evRes]: mapPollResult) 
            {
                auto idxFdRes = fd2poll[fdRes];
                fds[idxFdRes].revents = evRes & std::uint16_t(fds[idxFdRes].events);
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

    template <int NumSpin = 2, bool PreYield = true, typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period,
              typename Fn, typename ...Args>
    auto CallWrapper(int fd, short int event, std::chrono::duration<TClock, TDur> tout,
                     Fn fn, Args && ... args) -> typename std::invoke_result<Fn, Args...>::type
    {
        if (tout == std::chrono::duration<TClock, TDur>(0))
        {
            return fn(args...);
        }
        if constexpr(PreYield) 
        {
            jamc::ctask::Yield();
        }
        for (int i = 0; i < NumSpin; i++) 
        {
            auto ret = fn(args...);
            if ((ret >= 0) or (ret == -1 and errno != EAGAIN and errno != ENOENT))
            {
                return ret;
            }
            if (errno == ENOENT)
            {
                break;
            }
            if (i < 2) jamc::ctask::Yield();
            else jamc::ctask::SleepFor(std::chrono::microseconds(10 * i));
        }
        struct pollfd fds{};
        fds.fd = fd;
        fds.events = event;
        fds.revents = 0;
        while (true)
        {
            auto triggers = jamc::PollWithoutPreValidation(&fds, 1, tout);
            if ((triggers == -1) && (errno == EINTR))
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
            auto res = fn(args...);
            if ((res == -1) && (errno == EINTR))
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
        return CallWrapper<1, false>(socketFd, POLLIN, tout, accept4, socketFd, addr, addrlen, SOCK_NONBLOCK);
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
        fcntl(socket, unsigned(F_SETFL), isNonBlocking);
        for (int i = 0; i < 16; i++)
        {
            int ret = connect(socket, address, address_len);
            if (ret >= 0) 
            {
                return ret;
            }
            if (i > 0) 
            {
                jamc::ctask::SleepFor(std::chrono::microseconds(i * 10));
            }
            else
            {
                jamc::ctask::Yield();
            }
        }
        if (errno == EINPROGRESS)
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
        return -1;
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Read(int fildes, void *buf, size_t nByte,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(fildes, std::uint16_t(POLLIN), timeoutMS, read, fildes, buf, nByte);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Write(int fildes, const void *buf, size_t nByte,
                  std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper<1, false>(fildes, std::uint16_t(POLLOUT), timeoutMS, write, fildes, buf, nByte);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Send(int socketFd, const void *buffer, size_t length, int flags,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper<1, false>(socketFd, std::uint16_t(POLLOUT), timeoutMS, send, 
                                     socketFd, buffer, length, flags);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Send(int socketFd, const void *message, size_t length,
                 int flags, const struct sockaddr *dest_addr, socklen_t dest_len,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper<1, false>(socketFd, std::uint16_t(POLLOUT), timeoutMS, sendto, 
                                     socketFd, message, length, flags, dest_addr, dest_len);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Recv(int socketFd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_len,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socketFd, std::uint16_t(POLLIN), timeoutMS, recvfrom, 
                           socketFd, buffer, length, flags, address, address_len);
    }

    template <typename TClock = std::chrono::microseconds::rep, typename TDur = std::chrono::microseconds::period>
    ssize_t Recv(int socketFd, void *buffer, size_t length, int flags,
                 std::chrono::duration<TClock, TDur> timeoutMS = std::chrono::duration<TClock, TDur>::max())
    {
        return CallWrapper(socketFd, std::uint16_t(POLLIN), timeoutMS, recv, 
                           socketFd, buffer, length, flags);
    }
}
#endif
