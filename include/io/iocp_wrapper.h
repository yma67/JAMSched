#pragma once
#ifdef __APPLE__
#include <vector>
#include <atomic>
#include "concurrency/mutex.hpp"
namespace jamc
{
    class SchedulerBase;
    class IOCPAgent
    {
        Mutex m;
        ConditionVariableAny cv;
        int kqFileDescriptor;
        SchedulerBase *scheduler;
        std::unordered_map<uintptr_t, std::vector<std::pair<int, std::uint16_t>>> pendingEvents;
        bool CancelOne(int fd, std::uint16_t cancelEvent, void* uData) const;
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