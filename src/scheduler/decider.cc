#include "scheduler/scheduler.hpp"
#include "scheduler/decider.hpp"
#include <algorithm>

JAMScript::Decider::Decider(RIBScheduler *schedule) : scheduler(schedule)
{
#ifdef JAMSCRIPT_SCHED_AI_EXP
    srand(0);
#else
    srand(time(nullptr));
#endif
}

void JAMScript::Decider::NotifyChangeOfSchedule(const std::vector<RealTimeSchedule> &normal,
                                                const std::vector<RealTimeSchedule> &greedy)
{
    normalSSSlot.clear();
    greedySSSlot.clear();
    for (const auto &x : normal)
    {
        if (x.taskId == 0)
        {
            normalSSSlot.push_back({std::chrono::duration_cast<std::chrono::microseconds>(x.sTime).count(),
                                    std::chrono::duration_cast<std::chrono::microseconds>(x.eTime).count()});
        }
    }
    for (const auto &x : greedy)
    {
        if (x.taskId == 0)
        {
            greedySSSlot.push_back({std::chrono::duration_cast<std::chrono::microseconds>(x.sTime).count(),
                                    std::chrono::duration_cast<std::chrono::microseconds>(x.eTime).count()});
        }
    }
    period = std::chrono::duration_cast<std::chrono::microseconds>(normal.back().eTime).count();
}

uint64_t JAMScript::Decider::CalculateMatchCount(const std::vector<QType> &schedule)
{
    uint64_t mCount = 0;
    std::deque<ITaskEntry> tRecord(interactiveTaskRecord.begin(), interactiveTaskRecord.end());
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "Arrived: " << std::endl;
    for (auto &r : tRecord)
    {
        std::cout << "(" << r.arrival << ", " << r.burst << ", " << r.deadline << ")" << std::endl;
    }
#endif
    std::priority_queue<QType, std::deque<QType>, std::greater<QType>> edfPQueue;
    for (auto &slot : schedule)
    {
        while (!tRecord.empty())
        {
            if (tRecord.front().arrival >= slot.second)
            {
                break;
            }
            else
            {
                edfPQueue.push({tRecord.front().deadline, tRecord.front().burst});
                tRecord.pop_front();
            }
        }
        while (!edfPQueue.empty())
        {
            if (edfPQueue.top().first <= slot.first)
            {
                edfPQueue.pop();
            }
            else
            {
                break;
            }
        }
        long tSlot = slot.second - slot.first;
        while (tSlot > 0 && !edfPQueue.empty())
        {
            if (edfPQueue.top().second <= tSlot)
            {
                tSlot -= edfPQueue.top().second;
                edfPQueue.pop();
                mCount++;
            }
            else
            {
                auto tRef = edfPQueue.top();
                tRef.second -= tSlot;
                edfPQueue.pop();
                tSlot = 0;
                edfPQueue.push(tRef);
            }
        }
        if (edfPQueue.empty() && tRecord.empty())
        {
            break;
        }
    }
    return mCount;
}

void JAMScript::Decider::RecordInteractiveJobArrival(const ITaskEntry &aInteractiveTaskRecord)
{
    interactiveTaskRecord.push_back({aInteractiveTaskRecord.deadline - scheduler->numberOfPeriods * period, aInteractiveTaskRecord.burst,
                                     std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::high_resolution_clock::now() -
                                         scheduler->GetCycleStartTime())
                                         .count()});
}

bool JAMScript::Decider::DecideNextScheduleToRun()
{
    if (interactiveTaskRecord.empty())
    {
        return rand() % 2 == 0;
    }
    std::sort(
        interactiveTaskRecord.begin(), interactiveTaskRecord.end(),
        [](const ITaskEntry &x1, const ITaskEntry &x2) {
            return x1.arrival < x2.arrival;
        });
    auto successCountNormal = CalculateMatchCount(normalSSSlot);
    auto successCountGreedy = CalculateMatchCount(greedySSSlot);
#ifdef JAMSCRIPT_SCHED_AI_EXP
    std::cout << "Normal: " << successCountNormal << std::endl;
    std::cout << "Greedy: " << successCountGreedy << std::endl;
#endif
    interactiveTaskRecord.clear();
    if (successCountGreedy <= successCountNormal)
    {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "MIN GI NORMAL" << std::endl;
#endif
        return true;
    }
    else
    {
#ifdef JAMSCRIPT_SCHED_AI_EXP
        std::cout << "MIN GI GREEDY" << std::endl;
#endif
        return false;
    }
}