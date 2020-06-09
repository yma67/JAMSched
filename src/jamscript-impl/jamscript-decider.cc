/// Copyright 2020 Yuxiang Ma, Muthucumaru Maheswaran
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "jamscript-impl/jamscript-decider.hh"
#include "jamscript-impl/jamscript-scheduler.hh"

JAMScript::ScheduleDecider::ScheduleDecider(Scheduler *schedule) : scheduler(schedule) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
    srand(0);
#else
    srand(time(nullptr));
#endif
}

void JAMScript::ScheduleDecider::NotifyChangeOfSchedule(
    const std::vector<RealTimeTaskScheduleEntry> &normal,
    const std::vector<RealTimeTaskScheduleEntry> &greedy) {
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

std::vector<uint64_t> JAMScript::ScheduleDecider::CalculateAccumulator(
    const std::vector<RealTimeTaskScheduleEntry> &schedule) {
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
                    accv[entry.endTime / 1000] =
                        prevAcc + (entry.endTime / 1000) * 1000 - entry.startTime;
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

void JAMScript::ScheduleDecider::RecordInteractiveJobArrival(
    const InteractiveTaskExtender &aInteractiveTaskRecord) {
    interactiveTaskRecord.push_back(aInteractiveTaskRecord);
}

bool JAMScript::ScheduleDecider::DecideNextScheduleToRun() {
    if (interactiveTaskRecord.empty()) {
        return rand() % 2 == 0;
    }
    std::sort(interactiveTaskRecord.begin(), interactiveTaskRecord.end(),
              [](const InteractiveTaskExtender &e1, const InteractiveTaskExtender &e2) {
                  return e1.deadline < e2.deadline;
              });
    uint64_t accNormal = 0, successCountNormal = 0, accGreedy = 0, successCountGreedy = 0;
    std::vector<InteractiveTaskExtender> scg, scn;
    for (auto &r : interactiveTaskRecord) {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "b: " << r.burst << ", acc: " << accNormal << ", ddl: " << r.deadline
                  << std::endl;
#endif
        if (scheduler->multiplier * scheduler->normalSchedule.back().endTime <= r.deadline &&
            r.deadline <= (scheduler->multiplier + 1) * scheduler->normalSchedule.back().endTime &&
            r.burst + accNormal <=
                normalSpoadicServerAccumulator[(r.deadline -
                                                scheduler->multiplier *
                                                    scheduler->normalSchedule.back().endTime) /
                                               1000]) {
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
        std::cout << "b: " << r.burst << ", acc: " << accGreedy << ", ddl: " << r.deadline
                  << std::endl;
#endif
        if (scheduler->multiplier * scheduler->greedySchedule.back().endTime <= r.deadline &&
            r.deadline <= (scheduler->multiplier + 1) * scheduler->greedySchedule.back().endTime &&
            r.burst + accGreedy <=
                greedySpoadicServerAccumulator[(r.deadline -
                                                scheduler->multiplier *
                                                    scheduler->greedySchedule.back().endTime) /
                                               1000]) {
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