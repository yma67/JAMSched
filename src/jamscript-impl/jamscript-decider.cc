#include "jamscript-impl/jamscript-decider.hh"
#include "jamscript-impl/jamscript-scheduler.hh"
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <iostream>

jamscript::decider::
decider(c_side_scheduler* sched, 
        const std::vector<task_schedule_entry>& normal,
        const std::vector<task_schedule_entry>& greedy) : scheduler(sched) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
    srand(0);
#else
    srand(time(nullptr));
#endif
    schedule_change(normal, greedy);
}

void 
jamscript::decider::
schedule_change(const std::vector<task_schedule_entry>& normal,
                const std::vector<task_schedule_entry>& greedy) {
    normal_ss_acc = calculate_acc(normal);
    greedy_ss_acc = calculate_acc(greedy);
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "GREEDY ACC: ";
    for (auto& r: greedy_ss_acc) std::cout << r << '\t';
    std::cout << std::endl << "NORMAL ACC: ";
    for (auto& r: normal_ss_acc) std::cout << r << '\t';
    std::cout << std::endl;
#endif
}

std::vector<uint64_t> 
jamscript::decider::
calculate_acc(const std::vector<task_schedule_entry>& sched) {
    std::vector<uint64_t> accv(sched.back().end_time / 1000 + 1, 0);
    uint64_t prev_time = 0, prev_acc = 0;
    for (auto& entry: sched) {
        if (entry.task_id == 0x0) {
            for (uint64_t i = prev_time; i < entry.start_time / 1000; i++) {
                accv[i] = prev_acc;
            }
            if (entry.end_time - entry.start_time < 1000) {
                if (entry.end_time / 1000 != entry.start_time / 1000) {
                    accv[entry.start_time / 1000] = prev_acc;
                    accv[entry.end_time / 1000] = prev_acc + 
                                    (entry.end_time / 1000) * 1000 - 
                                     entry.start_time;
                    prev_time = entry.end_time / 1000 + 1;
                    prev_acc = accv[prev_time - 1] + 
                           entry.end_time - (entry.end_time / 1000) * 1000;
                } else {
                    accv[entry.start_time / 1000] = prev_acc;
                    prev_time = entry.end_time / 1000 + 1;
                    prev_acc = accv[prev_time - 1] + 
                           entry.end_time - entry.start_time;
                }
                continue;
            }
            for (uint64_t i = entry.start_time / 1000; 
                          i < entry.end_time / 1000; i++) {
                accv[i] = prev_acc + (i * 1000 - entry.start_time);
            }
            prev_time = entry.end_time / 1000;
            prev_acc = accv[prev_time - 1] + 1000;
        }
    }
    accv[prev_time] = prev_acc;
    return accv;
}

void 
jamscript::decider::record_interac(const interactive_extender& irecord) {
    interactive_record.push_back(irecord);
}

bool 
jamscript::decider::decide() {
if (interactive_record.empty()) {
        return rand() % 2 == 0;
    }
    std::sort(interactive_record.begin(), interactive_record.end(), 
              [](const interactive_extender& e1, 
                 const interactive_extender& e2) {
        return e1.deadline < e2.deadline;
    });
    uint64_t acc_normal = 0, currt_normal = 0, success_count_normal = 0, 
             acc_greedy = 0, currt_greedy = 0, success_count_greedy = 0;
    std::vector<interactive_extender> scg, scn;
    for (auto& r: interactive_record) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "b: " << r.burst << ", acc: " << acc_normal << ", ddl: " << 
                     r.deadline << std::endl;
#endif
        if (scheduler->multiplier * 
            scheduler->normal_schedule.back().end_time <= r.deadline &&
            r.deadline <= (scheduler->multiplier + 1) * 
            scheduler->normal_schedule.back().end_time && 
	        r.burst + acc_normal <= normal_ss_acc[
                (r.deadline - 
                 scheduler->multiplier * 
                 scheduler->normal_schedule.back().end_time) / 1000
            ]) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
            std::cout << "accept" << std::endl;
#endif
            success_count_normal++;
            acc_normal += r.burst;
            scn.push_back(r);
        }
    }
    for (auto& r: interactive_record) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "b: " << r.burst << ", acc: " << acc_greedy << ", ddl: " << 
                     r.deadline << std::endl;
#endif
        if (scheduler->multiplier * 
            scheduler->greedy_schedule.back().end_time <= r.deadline &&
            r.deadline <= (scheduler->multiplier + 1) * 
            scheduler->greedy_schedule.back().end_time && 
            r.burst + acc_greedy <= greedy_ss_acc[
                (r.deadline - 
                 scheduler->multiplier * 
                 scheduler->greedy_schedule.back().end_time) / 1000
            ]) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
            std::cout << "accept" << std::endl;
#endif
            success_count_greedy++;
            acc_greedy += r.burst;
            scg.push_back(r);
        }
    }
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "greedy success: " << success_count_greedy << std::endl;
    for (auto& r: scn) 
        std::cout << "(" << r.burst << ", " << r.deadline << "), ";
    std::cout << std::endl;
    std::cout << "normal success: " << success_count_normal << std::endl;
    for (auto& r: scg) 
        std::cout << "(" << r.burst << ", " << r.deadline << "), ";
    std::cout << std::endl;
    std::cout << "=> ";
#endif
    interactive_record.clear();
    if (success_count_greedy == success_count_normal) {
        return rand() % 2 == 0;
    } else if (success_count_greedy < success_count_normal) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "MIN GI NORMAL" << std::endl;
#endif
        return true;
    } else {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "MIN GI GREEDY" << std::endl;
#endif
        return false;
    }
}
