#pragma once
#if defined(__APPLE__) || defined(__linux__)
#include <vector>
#include <atomic>
#include "concurrency/mutex.hpp"
namespace jamc
{
    class SchedulerBase;
    class IOCPAgent
    {
        SpinMutex m;
        int kqFileDescriptor, numSpin;
        SchedulerBase *scheduler;
        std::unordered_map<uintptr_t, std::vector<std::pair<int, std::uint16_t>>> pendingEvents;
        std::unordered_map<int, std::unordered_map<uintptr_t, std::pair<int, uintptr_t>>> pendingRev;
        bool CancelOne(int fd, std::uint16_t cancelEvent, void* uData);
        bool CancelByData(void* uData);
    public:
        explicit IOCPAgent(SchedulerBase *);
        ~IOCPAgent();
        bool Add(const std::vector<std::pair<int, std::uint16_t>>& evesToAdd, void* uData);
        bool Cancel(void* uData);
        void Run();
    };
}
#endif
