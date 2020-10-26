#pragma once
#ifdef __APPLE__
#include <vector>
#include <atomic>
#include "concurrency/mutex.hpp"
namespace jamc
{
    class IOCPAgent
    {
        Mutex m;
        ConditionVariableAny cv;
        int kqFileDescriptor;
        std::unordered_map<void*, std::vector<std::pair<int, short int>>> pendingEvents;
        bool CancelOne(int fd, short int cancelEvent, void* uData) const;
        bool CancelByData(void* uData);
    public:
        IOCPAgent();
        ~IOCPAgent();
        bool Add(const std::vector<std::pair<int, short int>>& evesToAdd, void* uData);
        bool Cancel(void* uData);
        void Run();
    };
}
#endif