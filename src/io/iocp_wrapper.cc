//
// Created by Yuxiang Ma on 2020-10-25.
//
#ifdef __APPLE__
#include "scheduler/scheduler.hpp"
#include "scheduler/taskthief.hpp"
#include "concurrency/future.hpp"
#include "io/iocp_wrapper.h"
#include <sys/event.h>
#include <poll.h>

// this value makes CPU usage relatively low
constexpr std::chrono::microseconds kSleepPerIO(5000);
constexpr int kNumSpin = 15;

jamc::IOCPAgent::IOCPAgent(SchedulerBase *s) : scheduler(s), numSpin(0)
{
    kqFileDescriptor = kqueue();
}

jamc::IOCPAgent::~IOCPAgent()
{
    close(kqFileDescriptor);
}

bool jamc::IOCPAgent::Add(const std::vector<std::pair<int, std::uint16_t>>& evesToAdd, void* uData)
{
    {
        std::scoped_lock lk(m);
        pendingEvents[uintptr_t(uData)] = evesToAdd;
    }
    bool ret = true;
    for (auto& [fd, addEvent]: evesToAdd)
    {
        struct kevent kev[3];
        int n = 0;
        if (addEvent & std::uint16_t(POLLIN))
        {
            EV_SET(&kev[n++], fd, EVFILT_READ, EV_ADD, 0, 0, uData);
        }
        if (addEvent & std::uint16_t(POLLOUT))
        {
            EV_SET(&kev[n++], fd, EVFILT_WRITE, EV_ADD, 0, 0, uData);
        }
        if (addEvent & std::uint16_t(POLLERR))
        {
            EV_SET(&kev[n++], fd, EVFILT_EXCEPT, EV_ADD, 0, 0, uData);
        }
        ret = ret && (kevent(kqFileDescriptor, kev, n, nullptr, 0, nullptr) == 0);
    }
    return ret;
}

bool jamc::IOCPAgent::CancelOne(int fd, std::uint16_t cancelEvent, void* uData) const
{
    struct kevent kev[3];
    int n = 0;
    if (cancelEvent & std::uint16_t(POLLIN))
    {
        EV_SET(&kev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, uData);
    }
    if (cancelEvent & std::uint16_t(POLLOUT))
    {
        EV_SET(&kev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, uData);
    }
    if (cancelEvent & std::uint16_t(POLLERR))
    {
        EV_SET(&kev[n++], fd, EVFILT_EXCEPT, EV_DELETE, 0, 0, uData);
    }
    return kevent(kqFileDescriptor, kev, n, nullptr, 0, nullptr) == 0;
}

bool jamc::IOCPAgent::CancelByData(void* uData)
{
    bool ret = true;
    for (auto& [fileDes, eventsToCancel]: pendingEvents[uintptr_t(uData)])
    {
        ret = ret & CancelOne(fileDes, eventsToCancel, uData);
    }
    pendingEvents.erase(uintptr_t(uData));
    return ret;
}

bool jamc::IOCPAgent::Cancel(void* uData)
{
    std::scoped_lock lk(m);
    return CancelByData(uData);
}

void jamc::IOCPAgent::Run()
{
    constexpr std::size_t cEvent = 1024;
    struct kevent kev[cEvent];
    struct timespec timeout{};
    timeout.tv_sec = 0;
    timeout.tv_nsec = 0;
    {
        std::unique_lock lk(m);
        int n = kevent(kqFileDescriptor, nullptr, 0, kev, cEvent, &timeout);
        if (n == 0)
        {
            lk.unlock();
            if (numSpin < kNumSpin)
            {
                jamc::ctask::SleepFor(std::chrono::microseconds(numSpin * 10));
                numSpin++;
            }
            else
            {
                scheduler->GetRIBScheduler()->GetTimer().RequestIO(kqFileDescriptor);
                numSpin = 0;
            }
            return;
        }
        std::unordered_map<void*, std::unordered_map<int,  std::uint16_t>> wakeupMap;
        for (int i = 0; i < n; ++i)
        {
            struct kevent & ev = kev[i];
            int fd = ev.ident;
            std::uint16_t pollEvent = 0;
            if (ev.filter == EVFILT_READ)
            {
                pollEvent = POLLIN;
            }
            else if (ev.filter == EVFILT_WRITE)
            {
                pollEvent = POLLOUT;
            }
            if (ev.flags & std::uint16_t(EV_EOF))
            {
                pollEvent |= std::uint16_t(POLLHUP);
            }
            if (ev.flags & std::uint16_t(EV_ERROR))
            {
                pollEvent |= std::uint16_t(POLLERR);
            }
            auto it = wakeupMap.find(ev.udata);
            if (it == wakeupMap.end())
            {
                wakeupMap.insert({ev.udata, {}});
                it = wakeupMap.find(ev.udata);
            }
            auto it2 = it->second.find(fd);
            if (it2 == it->second.end())
            {
                it->second.insert({fd, {}});
                it2 = it->second.find(fd);
            }
            it2->second |= pollEvent;
        }
        for (auto& [ptrFut, fdMap]: wakeupMap)
        {
            CancelByData(ptrFut);
            auto* prom = reinterpret_cast<jamc::promise<std::unordered_map<int, std::uint16_t>> *>(ptrFut);
            prom->set_value(fdMap);
        }
    }
}
#elif defined(__linux__)
#include "scheduler/scheduler.hpp"
#include "scheduler/taskthief.hpp"
#include "concurrency/future.hpp"
#include "io/iocp_wrapper.h"
#include <sys/epoll.h>
#include <poll.h>
// this value makes CPU usage relatively low
constexpr std::chrono::microseconds kSleepPerIO(5000);
constexpr int kNumSpin = 15;

jamc::IOCPAgent::IOCPAgent(SchedulerBase *s) : scheduler(s), numSpin(0)
{
    kqFileDescriptor = epoll_create(117);
}

jamc::IOCPAgent::~IOCPAgent()
{
    close(kqFileDescriptor);
}

bool jamc::IOCPAgent::Add(const std::vector<std::pair<int, std::uint16_t>>& evesToAdd, void* uData)
{
    {
        std::scoped_lock lk(m);
        pendingEvents[uintptr_t(uData)] = evesToAdd;
    }
    bool ret = true;
    for (auto& [fd, addEvent]: evesToAdd)
    {
        struct epoll_event kev{};
        int n = 0;
        kev.events = EPOLLET;
        if (addEvent & std::uint16_t(POLLIN))
        {
            kev.events |= EPOLLIN;
        }
        if (addEvent & std::uint16_t(POLLOUT))
        {
            kev.events |= EPOLLOUT;
        }
        if (addEvent & std::uint16_t(POLLERR))
        {
            kev.events |= EPOLLERR;
        }
        if (addEvent & std::uint16_t(POLLHUP))
        {
            kev.events |= EPOLLHUP;
        }
        {
            std::scoped_lock lk(m);
            if (pendingRev.find(fd) == pendingRev.end()) pendingRev[fd] = {};
            pendingRev[fd].insert({uintptr_t(uData), {fd, uintptr_t(uData)}});
            kev.data.ptr = std::addressof(pendingRev[fd][uintptr_t(uData)]);
        }
        ret &= (epoll_ctl(kqFileDescriptor, EPOLL_CTL_ADD, fd, &kev) == 0);
    }
    return ret;
}

bool jamc::IOCPAgent::CancelOne(int fd, std::uint16_t cancelEvent, void* uData)
{
    struct epoll_event kev{};
    kev.data.ptr = std::addressof(pendingRev[fd][uintptr_t(uData)]);
    kev.events = EPOLLET;
    if (cancelEvent & std::uint16_t(POLLIN))
    {
        kev.events |= EPOLLIN;
    }
    if (cancelEvent & std::uint16_t(POLLOUT))
    {
        kev.events |= EPOLLOUT;
    }
    if (cancelEvent & std::uint16_t(POLLERR))
    {
        kev.events |= EPOLLERR;
    }
    if (cancelEvent & std::uint16_t(POLLHUP))
    {
        kev.events |= EPOLLHUP;
    }
    pendingRev[fd].erase(uintptr_t(uData));
    if (pendingRev[fd].empty()) pendingRev.erase(fd);
    return epoll_ctl(kqFileDescriptor, EPOLL_CTL_DEL, fd, &kev) == 0;
}

bool jamc::IOCPAgent::CancelByData(void* uData)
{
    bool ret = true;
    for (auto& [fileDes, eventsToCancel]: pendingEvents[uintptr_t(uData)])
    {
        ret = ret & CancelOne(fileDes, eventsToCancel, uData);
    }
    pendingEvents.erase(uintptr_t(uData));
    return ret;
}

bool jamc::IOCPAgent::Cancel(void* uData)
{
    std::scoped_lock lk(m);
    return CancelByData(uData);
}

void jamc::IOCPAgent::Run()
{
    constexpr std::size_t cEvent = 1024;
    struct epoll_event kev[cEvent];
    {
        std::unique_lock lk(m);
        int n = epoll_wait(kqFileDescriptor, kev, cEvent, 0);
        if (n == 0)
        {
            lk.unlock();
            if (numSpin < kNumSpin)
            {
                jamc::ctask::SleepFor(std::chrono::microseconds(numSpin * 10));
                numSpin++;
            }
            else
            {
                scheduler->GetRIBScheduler()->GetTimer().RequestIO(kqFileDescriptor);
                numSpin = 0;
            }
            return;
        }
        std::unordered_map<void*, std::unordered_map<int,  std::uint16_t>> wakeupMap;
        for (int i = 0; i < n; ++i)
        {
            struct epoll_event &ev = kev[i];
            auto& dataRef = *reinterpret_cast<std::pair<int, uintptr_t>*>(ev.data.ptr);
            auto dataPtr = reinterpret_cast<void*>(dataRef.second);
            int fd = dataRef.first;
            std::uint16_t pollEvent = 0;
            if (ev.events == EPOLLIN)
            {
                pollEvent = POLLIN;
            }
            else if (ev.events == EPOLLOUT)
            {
                pollEvent = POLLOUT;
            }
            if (ev.events & std::uint16_t(EPOLLHUP))
            {
                pollEvent |= std::uint16_t(POLLHUP);
            }
            if (ev.events & std::uint16_t(EPOLLERR))
            {
                pollEvent |= std::uint16_t(POLLERR);
            }
            auto it = wakeupMap.find(dataPtr);
            if (it == wakeupMap.end())
            {
                wakeupMap.insert({dataPtr, {}});
                it = wakeupMap.find(dataPtr);
            }
            auto it2 = it->second.find(fd);
            if (it2 == it->second.end())
            {
                it->second.insert({fd, {}});
                it2 = it->second.find(fd);
            }
            it2->second |= pollEvent;
        }
        for (auto& [ptrFut, fdMap]: wakeupMap)
        {
            CancelByData(ptrFut);
            auto* prom = reinterpret_cast<jamc::promise<std::unordered_map<int, std::uint16_t>> *>(ptrFut);
            printf("%p\n", prom);
            prom->set_value(fdMap);
        }
        printf("it done\n");
    }
}
#endif