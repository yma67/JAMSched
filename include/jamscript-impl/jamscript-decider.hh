#ifndef JAMSCRIPT_JAMSCRIPT_DECIDER_H
#define JAMSCRIPT_JAMSCRIPT_DECIDER_H
#include <vector>
#include <cstdint>

namespace JAMScript {

class Scheduler;
class RealTimeTaskScheduleEntry;
class InteractiveTaskExtender;

class ScheduleDecider {
public:
    void NotifyChangeOfSchedule(const std::vector<RealTimeTaskScheduleEntry>& normal,
                         const std::vector<RealTimeTaskScheduleEntry>& greedy);
    bool DecideNextScheduleToRun();
    void RecordInteractiveJobArrival(const InteractiveTaskExtender& aInteractiveTaskRecord);
    ScheduleDecider(Scheduler* schedule);
private:
    ScheduleDecider() = delete;
    Scheduler* scheduler;
    std::vector<uint64_t> normalSpoadicServerAccumulator, greedySpoadicServerAccumulator;
    std::vector<InteractiveTaskExtender> interactiveTaskRecord;
    std::vector<uint64_t> CalculateAccumulator(
        const std::vector<RealTimeTaskScheduleEntry>& schedule
    );
};

}
#endif