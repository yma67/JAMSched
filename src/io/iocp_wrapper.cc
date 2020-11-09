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
#endif