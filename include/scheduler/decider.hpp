#ifndef JAMSCRIPT_JAMSCRIPT_DECIDER_H
#define JAMSCRIPT_JAMSCRIPT_DECIDER_H
#include <cstdint>
#include <vector>

namespace jamc
{

    struct RTaskEntry
    {
        uint64_t startTime, endTime;
        uint32_t taskId;
        RTaskEntry(uint64_t startTime, uint64_t endTime, uint32_t taskId)
            : startTime(startTime), endTime(endTime), taskId(taskId) {}
    };

    struct ITaskEntry
    {
        long deadline, burst, arrival;
        ITaskEntry(long deadline, long burst) : deadline(deadline), burst(burst) {}
        ITaskEntry(long deadline, long burst, long arrival) : deadline(deadline), burst(burst), arrival(arrival) {}
    };

    class RIBScheduler;
    struct RealTimeSchedule;

    class Decider
    {
    public:

        void NotifyChangeOfSchedule(const std::vector<RealTimeSchedule> &normal,
                                    const std::vector<RealTimeSchedule> &greedy);
        bool DecideNextScheduleToRun();
        void RecordInteractiveJobArrival(const ITaskEntry &aInteractiveTaskRecord);
        Decider(RIBScheduler *schedule);

    private:

        Decider(Decider const &) = delete;
        Decider(Decider &&) = delete;
        Decider &operator=(Decider const &) = delete;
        Decider &operator=(Decider &&) = delete;

        Decider() = delete;
        using QType = std::pair<uint64_t, uint64_t>;
        RIBScheduler *scheduler;
        long period;
        std::vector<QType> normalSSSlot, greedySSSlot;
        std::vector<ITaskEntry> interactiveTaskRecord;
        uint64_t CalculateMatchCount(const std::vector<QType> &schedule);

    };

} // namespace jamc
#endif