#ifndef JAMSCRIPT_JAMSCRIPT_DECIDER_H
#define JAMSCRIPT_JAMSCRIPT_DECIDER_H
#include <vector>
#include <cstdint>

namespace jamscript {

class c_side_scheduler;
class task_schedule_entry;
class interactive_extender;

class decider {
public:
    void schedule_change(const std::vector<task_schedule_entry>& normal,
                         const std::vector<task_schedule_entry>& greedy);
    bool decide();
    void record_interac(const interactive_extender& irecord);
    decider(c_side_scheduler* sched);
private:
    decider() = delete;
    c_side_scheduler* scheduler;
    std::vector<uint64_t> normal_ss_acc, greedy_ss_acc;
    std::vector<interactive_extender> interactive_record;
    std::vector<uint64_t> calculate_acc(
        const std::vector<task_schedule_entry>& sched
    );
};

}
#endif