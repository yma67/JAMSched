#ifndef JAMSCRIPT_JAMSCRIPT_DECIDER_H
#define JAMSCRIPT_JAMSCRIPT_DECIDER_H
#include <cstdint>
#include <vector>

namespace JAMScript {
    struct RTaskEntry {
        uint64_t startTime, endTime;
        uint32_t taskId;
        RTaskEntry(uint64_t startTime, uint64_t endTime, uint32_t taskId)
            : startTime(startTime), endTime(endTime), taskId(taskId) {}
    };
    struct ITaskEntry {
        long deadline, burst;
        ITaskEntry(long deadline, long burst) : deadline(deadline), burst(burst) {}
    };
    class RIBScheduler;
    struct RealTimeSchedule;
    class Decider {
    public:
        void NotifyChangeOfSchedule(const std::vector<RealTimeSchedule>& normal,
                                    const std::vector<RealTimeSchedule>& greedy);
        bool DecideNextScheduleToRun();
        void RecordInteractiveJobArrival(const ITaskEntry& aInteractiveTaskRecord);
        Decider(RIBScheduler* schedule);

    private:
        Decider() = delete;
        RIBScheduler* scheduler;
        std::vector<uint64_t> normalSpoadicServerAccumulator, greedySpoadicServerAccumulator;
        std::vector<ITaskEntry> interactiveTaskRecord;
        std::vector<uint64_t> CalculateAccumulator(const std::vector<RealTimeSchedule>& schedule);
    };

}  // namespace JAMScript
#endif