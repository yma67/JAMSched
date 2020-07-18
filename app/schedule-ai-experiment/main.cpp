#include <concurrency/future.hpp>
#include <core/task/task.hpp>
#include <exception/exception.hpp>
#include <scheduler/scheduler.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

int nrounds, batch_count = 0, interactive_count = 0, rt_count = 0, preempt_tslice = 0, _bc, _ic;
std::vector<JAMScript::RealTimeSchedule> normal_sched, greedy_sched;

int RealTimeTaskFunction(JAMScript::RIBScheduler &jSched, std::vector<uint64_t> &tasks, int i)
{
    auto tStart = std::chrono::high_resolution_clock::now();
    std::cout << "RT TASK-" << i << " start" << std::endl;
    rt_count++;
    jSched
        .CreateRealTimeTask(
            {true, 0}, i,
            std::function<int(JAMScript::RIBScheduler &, std::vector<uint64_t> &, int)>(RealTimeTaskFunction),
            std::ref(jSched), std::ref(tasks), i)
        .Detach();
    while (std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - tStart)
                   .count() +
               500000 <=
           tasks[i] * 1000) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(50));
    }
    return 8;
}

int main(int argc, char *argv[])
{
    // both of the followings are fine
    // ./schedule-ai-experiment [trace file name] [number of cycles to simulate]
    // ./schedule-ai-experiment [trace file name] [number of cycles to simulate] [psuedo-preemption
    // time slice in ns]
    if (argc < 3 || argc > 4)
        return EXIT_FAILURE;
    int nn, ng, ntask, nitask, nbtask;
    std::ifstream trace_file(argv[1]);
    nrounds = std::atoi(argv[2]);
    if (argc == 4)
        preempt_tslice = std::atoi(argv[3]);
    uint64_t s, e;
    uint32_t id;
    std::vector<uint64_t> tasks, tasks_exec_count;
    std::vector<std::tuple<std::chrono::high_resolution_clock::duration, std::chrono::high_resolution_clock::duration,
                           std::chrono::high_resolution_clock::duration>>
        interactive_tasks;
    std::vector<std::pair<std::chrono::high_resolution_clock::duration, std::chrono::high_resolution_clock::duration>>
        batch_tasks;
    std::vector<JAMScript::RealTimeSchedule> normal_sched, greedy_sched;
    if (trace_file.is_open())
    {
        trace_file >> ntask;
        tasks.resize(ntask + 1);
        tasks_exec_count.resize(ntask + 1);
        for (auto &c : tasks_exec_count)
            c = 0;
        trace_file >> nn;
        while (nn--)
        {
            trace_file >> s >> e >> id;
            tasks[id] = (e - s);
            tasks_exec_count[id]++;
            normal_sched.push_back({std::chrono::high_resolution_clock::duration(std::chrono::microseconds(s)),
                                    std::chrono::high_resolution_clock::duration(std::chrono::microseconds(e)), id});
        }
        trace_file >> ng;
        while (ng--)
        {
            trace_file >> s >> e >> id;
            tasks[id] = (e - s);
            greedy_sched.push_back({std::chrono::high_resolution_clock::duration(std::chrono::microseconds(s)),
                                    std::chrono::high_resolution_clock::duration(std::chrono::microseconds(e)), id});
        }
        trace_file >> nitask;
        _ic = nitask;
        while (nitask--)
        {
            int arr, ddl, burst;
            trace_file >> arr >> ddl >> burst;
            interactive_tasks.push_back(
                {std::chrono::high_resolution_clock::duration(std::chrono::microseconds(arr)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(ddl)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(burst))});
        }
        trace_file >> nbtask;
        _bc = nbtask;
        while (nbtask--)
        {
            int arr, burst;
            trace_file >> arr >> burst;
            batch_tasks.push_back({std::chrono::high_resolution_clock::duration(std::chrono::microseconds(arr)),
                                   std::chrono::high_resolution_clock::duration(std::chrono::microseconds(burst))});
        }
        JAMScript::RIBScheduler jRIBScheduler(1024 * 256);
        auto tuPeriod = std::chrono::microseconds(996);
        jRIBScheduler.CreateBatchTask({true, 0, false}, tuPeriod, [&]() {
            while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() -
                                                                         jRIBScheduler.GetSchedulerStartTime())
                       .count() <
                   nrounds * std::chrono::duration_cast<std::chrono::microseconds>(normal_sched.back().eTime).count())
            {
                int ax = 0;
                for (auto &[arrival, deadline, burst] : interactive_tasks)
                {
                    auto cBurst = burst;
                    if (std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() - jRIBScheduler.GetSchedulerStartTime())
                            .count() >= std::chrono::duration_cast<std::chrono::microseconds>(arrival).count())
                    {
                        jRIBScheduler
                            .CreateInteractiveTask(
                                (((ax++) % 2 == 0) ? (JAMScript::StackTraits{true, 0, false})
                                                   : (JAMScript::StackTraits{false, 1024 * 128, false})),
                                deadline, burst, []() {},
                                [&jRIBScheduler, cBurst]() {
                                    std::cout << "Interac Exec" << std::endl;
                                    interactive_count++;
                                    std::cout << "JSleep Start" << std::endl;
                                    auto ct = std::chrono::high_resolution_clock::now();
                                    auto delta = 1000;
                                    JAMScript::ThisTask::SleepFor(std::chrono::microseconds(delta));
                                    std::cout << "JSleep Jitter: "
                                              << std::chrono::duration_cast<std::chrono::microseconds>(
                                                     std::chrono::high_resolution_clock::now() - ct)
                                                         .count() -
                                                     delta
                                              << "us" << std::endl;
                                    auto tStart = std::chrono::high_resolution_clock::now();
                                    while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::high_resolution_clock::now() - tStart)
                                               .count() <=
                                           std::chrono::duration_cast<std::chrono::nanoseconds>(cBurst).count())
                                    {
                                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                                        JAMScript::ThisTask::Yield();
                                    }
                                    return 8;
                                })
                            .Detach();
                        arrival = std::chrono::high_resolution_clock::duration::max();
                    }
                }
                for (auto &[arrival, burst] : batch_tasks)
                {
                    auto cBurst = burst;
                    if (std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() - jRIBScheduler.GetSchedulerStartTime()) >=
                        arrival)
                    {
                        jRIBScheduler
                            .CreateBatchTask(
                                (((ax++) % 2 == 0) ? (JAMScript::StackTraits{true, 0, false})
                                                   : (JAMScript::StackTraits{false, 1024 * 128, false})),
                                std::chrono::duration_cast<std::chrono::microseconds>(cBurst),
                                [&jRIBScheduler, cBurst]() {
                                    auto tStart = std::chrono::high_resolution_clock::now();
                                    std::cout << "Batch Exec" << std::endl;
                                    batch_count++;
                                    while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::high_resolution_clock::now() - tStart)
                                                   .count() +
                                               500000 <=
                                           std::chrono::duration_cast<std::chrono::nanoseconds>(cBurst).count())
                                    {
                                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                                        JAMScript::ThisTask::Yield();
                                    }
                                    return 8;
                                })
                            .Detach();
                        arrival = std::chrono::high_resolution_clock::duration::max();
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(25));
                JAMScript::ThisTask::Yield();
            }
            while (std::chrono::high_resolution_clock::now() < jRIBScheduler.GetSchedulerStartTime() + nrounds * tuPeriod) {
                std::this_thread::sleep_for(std::chrono::microseconds(25));
                JAMScript::ThisTask::Yield();
            }
            auto sec3 = std::make_shared<std::string>();
            auto p = std::make_shared<JAMScript::Promise<std::string>>();
            auto ep = std::make_shared<JAMScript::Promise<void>>();
            auto rp = std::make_shared<JAMScript::Promise<std::string &>>();
            auto fx = jRIBScheduler.CreateInteractiveTask(
                {true, 0}, std::chrono::nanoseconds(38000000), std::chrono::nanoseconds(99), []() {},
                [p, ep, rp, sec3]() {
                    *sec3 = "I don't like cpp";
                    std::cout << "Start Joining Task " << &sec3 << std::endl;
                    ep->SetException(std::make_exception_ptr(JAMScript::InvalidArgumentException("cancelled")));
                    rp->SetValue(*sec3);
                    p->SetValue("I like Java");
                    std::cout << "End Joining Task" << std::endl;
                });
            fx.Detach();
            auto fp = p->GetFuture();
            auto frp = rp->GetFuture();
            std::cout << "Before Join" << std::endl;
            std::cout << "After Join" << std::endl;
            std::cout << "Secret is: \"" << fp.Get() << "\"" << std::endl;
            try
            {
                ep->GetFuture().Get();
            }
            catch (const JAMScript::InvalidArgumentException &e)
            {
                std::cout << e.what() << std::endl;
            }
            std::cout << "Secret3 " << &sec3 << " is: \"" << frp.Get() << "\"" << std::endl;
            // Test GetFor, GetUntil
            // Success
            JAMScript::Promise<std::string> prCouldArrive;
            std::thread([&prCouldArrive](){
                prCouldArrive.SetValue("GetUntil Success! ");
            }).detach();
            auto fuCouldArrive = prCouldArrive.GetFuture();
            // auto couldArriveVal = fuCouldArrive.GetUntil(std::chrono::steady_clock::now() + std::chrono::seconds(100));
            auto couldArriveVal = fuCouldArrive.GetFor(std::chrono::seconds(100));
            std::cout << couldArriveVal << std::endl;
            // Fail
            JAMScript::Promise<std::string> prNeverArrive;
            auto fuNeverArrive = prNeverArrive.GetFuture();
            try {
                fuNeverArrive.GetFor(std::chrono::milliseconds(500));
            } catch (const std::exception& e) {
                std::cout << e.what() << std::endl;
            }
            jRIBScheduler.ShutDown();
            return 3;
        });
        for (uint32_t i = 1; i < tasks.size(); i++)
        {
            jRIBScheduler
                .CreateRealTimeTask(
                    (((i) % 2 == 0) ? (JAMScript::StackTraits{true, 0}) : (JAMScript::StackTraits{false, 1024 * 128})),
                    i, std::function<int(JAMScript::RIBScheduler &, std::vector<uint64_t> &, int)>(RealTimeTaskFunction),
                    std::ref(jRIBScheduler), std::ref(tasks), i)
                .Detach();
        }
        jRIBScheduler.SetSchedule(normal_sched, greedy_sched);
        jRIBScheduler.RunSchedulerMainLoop();
        std::cout << "Exp Batch: " << _bc << ", Actual Batch" << batch_count << std::endl;
        std::cout << "Exp Interac: " << _ic << ", Actual Interac" << interactive_count << std::endl;
    }
    return 0;
}