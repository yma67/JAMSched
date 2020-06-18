#include "scheduler/scheduler.h"
#include "scheduler/decider.h"
#include <algorithm>

JAMScript::Decider::Decider(RIBScheduler *schedule) : scheduler(schedule) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
    srand(0);
#else
    srand(time(nullptr));
#endif
}

void JAMScript::Decider::NotifyChangeOfSchedule(const std::vector<RealTimeSchedule> &normal,
                                                const std::vector<RealTimeSchedule> &greedy) {
    normalSpoadicServerAccumulator = CalculateAccumulator(normal);
    greedySpoadicServerAccumulator = CalculateAccumulator(greedy);
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "GREEDY ACC: ";
    for (auto &r : greedySpoadicServerAccumulator) std::cout << r << '\t';
    std::cout << std::endl << "NORMAL ACC: ";
    for (auto &r : normalSpoadicServerAccumulator) std::cout << r << '\t';
    std::cout << std::endl;
#endif
}

std::vector<uint64_t> JAMScript::Decider::CalculateAccumulator(const std::vector<RealTimeSchedule> &schedule_) {
    std::vector<RTaskEntry> schedule;
    std::transform(
        schedule_.begin(), schedule_.end(), std::back_inserter(schedule), [](const RealTimeSchedule &x) -> RTaskEntry {
            return RTaskEntry(std::chrono::duration_cast<std::chrono::microseconds>(x.sTime).count(),
                              std::chrono::duration_cast<std::chrono::microseconds>(x.eTime).count(), x.taskId);
        });
    std::vector<uint64_t> accv(schedule.back().endTime / 1000 + 1, 0);
    uint64_t prevTime = 0, prevAcc = 0;
    for (auto &entry : schedule) {
        if (entry.taskId == 0x0) {
            for (uint64_t i = prevTime; i < entry.startTime / 1000; i++) {
                accv[i] = prevAcc;
            }
            if (entry.endTime - entry.startTime < 1000) {
                if (entry.endTime / 1000 != entry.startTime / 1000) {
                    accv[entry.startTime / 1000] = prevAcc;
                    accv[entry.endTime / 1000] = prevAcc + (entry.endTime / 1000) * 1000 - entry.startTime;
                    prevTime = entry.endTime / 1000 + 1;
                    prevAcc = accv[prevTime - 1] + entry.endTime - (entry.endTime / 1000) * 1000;
                } else {
                    accv[entry.startTime / 1000] = prevAcc;
                    prevTime = entry.endTime / 1000 + 1;
                    prevAcc = accv[prevTime - 1] + entry.endTime - entry.startTime;
                }
                continue;
            }
            for (uint64_t i = entry.startTime / 1000; i < entry.endTime / 1000; i++) {
                accv[i] = prevAcc + (i * 1000 - entry.startTime);
            }
            prevTime = entry.endTime / 1000;
            prevAcc = accv[prevTime - 1] + 1000;
        }
    }
    accv[prevTime] = prevAcc;
    return accv;
}

void JAMScript::Decider::RecordInteractiveJobArrival(const ITaskEntry &aInteractiveTaskRecord) {
    interactiveTaskRecord.push_back(aInteractiveTaskRecord);
}

bool JAMScript::Decider::DecideNextScheduleToRun() {
    if (interactiveTaskRecord.empty()) {
        return rand() % 2 == 0;
    }
    std::sort(interactiveTaskRecord.begin(), interactiveTaskRecord.end(),
              [](const ITaskEntry &e1, const ITaskEntry &e2) { return e1.deadline < e2.deadline; });
    uint64_t accNormal = 0, successCountNormal = 0, accGreedy = 0, successCountGreedy = 0;
    std::vector<ITaskEntry> scg, scn;
    uint64_t period =
        std::chrono::duration_cast<std::chrono::microseconds>(scheduler->rtScheduleNormal.back().eTime).count();
    uint64_t st = (scheduler->numberOfPeriods - 1) * period;
    uint64_t ed = st + period;
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "Start: " << st << ", End: " << ed << std::endl;
#endif
    for (auto &r : interactiveTaskRecord) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "b: " << r.burst << ", acc: " << accNormal << ", ddl: " << r.deadline << std::endl;
#endif
        if (st <= r.deadline && r.deadline <= ed &&
            r.burst + accNormal <= normalSpoadicServerAccumulator[(r.deadline - st) / 1000]) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
            std::cout << "accept" << std::endl;
#endif
            successCountNormal++;
            accNormal += r.burst;
            scn.push_back(r);
        }
    }
    for (auto &r : interactiveTaskRecord) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "b: " << r.burst << ", acc: " << accGreedy << ", ddl: " << r.deadline << std::endl;
#endif
        if (st <= r.deadline && r.deadline <= ed &&
            r.burst + accGreedy <= greedySpoadicServerAccumulator[(r.deadline - st) / 1000]) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
            std::cout << "accept" << std::endl;
#endif
            successCountGreedy++;
            accGreedy += r.burst;
            scg.push_back(r);
        }
    }
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "greedy success: " << successCountGreedy << std::endl;
    for (auto &r : scn) std::cout << "(" << r.burst << ", " << r.deadline << "), ";
    std::cout << std::endl;
    std::cout << "normal success: " << successCountNormal << std::endl;
    for (auto &r : scg) std::cout << "(" << r.burst << ", " << r.deadline << "), ";
    std::cout << std::endl;
    std::cout << "=> ";
#endif
    interactiveTaskRecord.clear();
    if (successCountGreedy == successCountNormal) {
        return rand() % 2 == 0;
    } else if (successCountGreedy < successCountNormal) {
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