#include <core/task/task.hh>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <scheduler/scheduler.hh>
#include <thread>
#include <vector>
#include <concurrency/future.hh>

int nrounds, batch_count = 0, interactive_count = 0, preempt_tslice = 0, _bc, _ic;
std::vector<JAMScript::RealTimeSchedule> normal_sched, greedy_sched;

int RealTimeTaskFunction(JAMScript::RIBScheduler& jSched, std::vector<uint64_t>& tasks, int i) {
    auto tStart = std::chrono::high_resolution_clock::now();
    std::cout << "RT TASK-" << i << " start" << std::endl;
    jSched.CreateRealTimeTask(
        { true, 0 }, i,
        std::function<int(JAMScript::RIBScheduler&, std::vector<uint64_t>&, int)>(
            RealTimeTaskFunction),
        std::ref(jSched), std::ref(tasks), i)->Detach();
    while (std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now() - tStart)
                   .count() +
               500000 <=
           tasks[i] * 1000) {
        // std::this_thread::sleep_for(std::chrono::nanoseconds(50));
    }
    return 8;
}

int main(int argc, char* argv[]) {
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
    std::vector<std::tuple<std::chrono::high_resolution_clock::duration,
                           std::chrono::high_resolution_clock::duration,
                           std::chrono::high_resolution_clock::duration>>
        interactive_tasks;
    std::vector<std::pair<std::chrono::high_resolution_clock::duration,
                          std::chrono::high_resolution_clock::duration>>
        batch_tasks;
    std::vector<JAMScript::RealTimeSchedule> normal_sched, greedy_sched;
    if (trace_file.is_open()) {
        trace_file >> ntask;
        tasks.resize(ntask + 1);
        tasks_exec_count.resize(ntask + 1);
        for (auto& c : tasks_exec_count) c = 0;
        trace_file >> nn;
        while (nn--) {
            trace_file >> s >> e >> id;
            tasks[id] = (e - s);
            tasks_exec_count[id]++;
            normal_sched.push_back(
                {std::chrono::high_resolution_clock::duration(std::chrono::microseconds(s)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(e)), id});
        }
        trace_file >> ng;
        while (ng--) {
            trace_file >> s >> e >> id;
            tasks[id] = (e - s);
            greedy_sched.push_back(
                {std::chrono::high_resolution_clock::duration(std::chrono::microseconds(s)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(e)), id});
        }
        trace_file >> nitask;
        _ic = nitask;
        while (nitask--) {
            int arr, ddl, burst;
            trace_file >> arr >> ddl >> burst;
            interactive_tasks.push_back(
                {std::chrono::high_resolution_clock::duration(std::chrono::microseconds(arr)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(ddl)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(burst))});
        }
        trace_file >> nbtask;
        _bc = nbtask;
        while (nbtask--) {
            int arr, burst;
            trace_file >> arr >> burst;
            batch_tasks.push_back(
                {std::chrono::high_resolution_clock::duration(std::chrono::microseconds(arr)),
                 std::chrono::high_resolution_clock::duration(std::chrono::microseconds(burst))});
        }
        JAMScript::RIBScheduler jRIBScheduler(1024 * 256);
        jRIBScheduler.CreateBatchTask({true, 0}, std::chrono::milliseconds(996), [&]() {
            while (std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::high_resolution_clock::now() -
                       jRIBScheduler.GetSchedulerStartTime())
                       .count() < nrounds * std::chrono::duration_cast<std::chrono::microseconds>(
                                                normal_sched.back().eTime)
                                                .count()) {
                for (auto& [arrival, deadline, burst] : interactive_tasks) {
                    auto cBurst = burst;
                    if (std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() -
                            jRIBScheduler.GetSchedulerStartTime())
                            .count() >=
                        std::chrono::duration_cast<std::chrono::microseconds>(arrival)
                            .count()) {
                        jRIBScheduler.CreateInteractiveTask({ true, 0 }, deadline, burst,
                [&jRIBScheduler, cBurst]() { std::cout << "Interac Exec" << std::endl; /*std::cout <<
                "JSleep Start" << std::endl; auto ct = std::chrono::high_resolution_clock::now();
                                auto delta = 1000;
                                JAMScript::ThisTask::SleepFor(std::chrono::microseconds(delta));
                                std::cout
                                    << "JSleep Jitter: "
                                    << std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::high_resolution_clock::now() - ct)
                                               .count() -
                                           delta
                                    << "us" << std::endl;*/
                                auto tStart = std::chrono::high_resolution_clock::now();
                                while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::high_resolution_clock::now() - tStart)
                                           .count() //+ 500000
                                       <= std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              cBurst)
                                              .count()) {
                                    std::this_thread::sleep_for(std::chrono::nanoseconds(50));
                                    JAMScript::ThisTask::Yield();
                                }
                                return 8;
                            })->Detach();
                        arrival = std::chrono::high_resolution_clock::duration::max();
                    }
                }
                for (auto& [arrival, burst] : batch_tasks) {
                    auto cBurst = burst;
                    if (std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() -
                            jRIBScheduler.GetSchedulerStartTime()) >= arrival) {
                        jRIBScheduler.CreateBatchTask(
                            { true, 0 },
                            std::chrono::duration_cast<std::chrono::microseconds>(cBurst),
                            [&jRIBScheduler, cBurst]() {
                                auto tStart = std::chrono::high_resolution_clock::now();
                                std::cout << "Batch Exec" << std::endl;
                                while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::high_resolution_clock::now() - tStart)
                                               .count() +
                                           500000 <=
                                       std::chrono::duration_cast<std::chrono::nanoseconds>(cBurst)
                                           .count()) {
                                    std::this_thread::sleep_for(std::chrono::nanoseconds(50));
                                    JAMScript::ThisTask::Yield();
                                }
                                return 8;
                            })->Detach();
                        arrival = std::chrono::high_resolution_clock::duration::max();
                    }
                }
                std::this_thread::sleep_for(std::chrono::nanoseconds(50));
                JAMScript::ThisTask::Yield();
            }
            auto p = std::make_shared<JAMScript::Promise<std::string>>();
            auto* fx = jRIBScheduler.CreateInteractiveTask({ true, 0 },
            std::chrono::nanoseconds(38000000), std::chrono::nanoseconds(99), [p]() { 
                std::cout << "Start Joining Task" << std::endl; 
                p->SetValue("I like Java");
                std::cout << "End Joining Task" << std::endl;
            });
            fx->Detach();
            auto fp = p->GetFuture();
            std::cout << "Before Join" << std::endl;
            // fx->Join();
            fp.Wait();
            std::cout << "After Join" << std::endl;
            
            std::cout << "Secret is: \"" << fp.Get() << "\"" << std::endl;
            jRIBScheduler.ShutDown();
            return 3;
        });
        for (uint32_t i = 1; i < tasks.size(); i++) {
            jRIBScheduler
                .CreateRealTimeTask(
                    {true, 0}, i,
                    std::function<int(JAMScript::RIBScheduler&, std::vector<uint64_t>&, int)>(
                        RealTimeTaskFunction),
                    std::ref(jRIBScheduler), std::ref(tasks), i)
                ->Detach();
        }

        jRIBScheduler.rtScheduleNormal = normal_sched;
        jRIBScheduler.rtScheduleGreedy = greedy_sched;

        jRIBScheduler();
        std::cout << "end" << std::endl;
    }
    return 0;
}