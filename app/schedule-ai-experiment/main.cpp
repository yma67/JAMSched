#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include <jamscript-impl/jamscript-scheduler.hh>
#include <jamscript-impl/jamscript-time.hh>

struct task_data_transfer {
    uint32_t taskId, exec_count;
    uint64_t task_sleep;
    JAMScript::Scheduler* scheduler;
    JAMScript::CTaskType taskType;
    task_data_transfer() : 
    taskId(0), task_sleep(0), scheduler(nullptr), 
    taskType(JAMScript::REAL_TIME_TASK_T), exec_count(0) {}
    task_data_transfer(uint32_t taskId, uint64_t task_sleep, 
                       JAMScript::Scheduler* scheduler, 
                       JAMScript::CTaskType taskType) : 
    taskId(taskId), task_sleep(task_sleep), scheduler(scheduler), 
    taskType(taskType), exec_count(0) {}
};

struct bi_dto {
    std::vector<std::pair<uint64_t, JAMScript::InteractiveTaskExtender>>* 
    pinteractive_tasks;
    std::vector<std::pair<uint64_t, uint64_t>>* pbatch_tasks;
    bi_dto(std::vector<std::pair<uint64_t, JAMScript::InteractiveTaskExtender>>* 
           pi, std::vector<std::pair<uint64_t, uint64_t>>* pb) : 
           pinteractive_tasks(pi), pbatch_tasks(pb) {}
};

int nrounds, batch_count = 0, interactive_count = 0, preempt_tslice = 0, 
    _bc, _ic;
std::vector<JAMScript::RealTimeTaskScheduleEntry> normal_sched, greedy_sched;

int main(int argc, char *argv[]) {
    // both of the followings are fine
    // ./schedule-ai-experiment [trace file name] [number of cycles to simulate]
    // ./schedule-ai-experiment [trace file name] [number of cycles to simulate] [psuedo-preemption time slice in ns]
    if (argc < 3 || argc > 4) return EXIT_FAILURE;
    int nn, ng, ntask, nitask, nbtask;
    std::ifstream trace_file(argv[1]);
    nrounds = std::atoi(argv[2]);
    if (argc == 4) preempt_tslice = std::atoi(argv[3]);
    uint64_t s, e;
    uint32_t id;
    std::vector<uint64_t> tasks, tasks_exec_count;
    std::vector<task_data_transfer> task_dtos;
    std::vector<std::pair<uint64_t, JAMScript::InteractiveTaskExtender>> 
    interactive_tasks;
    std::vector<std::pair<uint64_t, uint64_t>> batch_tasks;
    bi_dto pbi({ &interactive_tasks, &batch_tasks });
    if (trace_file.is_open()) {
        trace_file >> ntask;
        tasks.resize(ntask + 1);
        tasks_exec_count.resize(ntask + 1);
        task_dtos.resize(ntask + 1);
        for (auto& c: tasks_exec_count) c = 0;
        trace_file >> nn;
        while (nn--) {
            trace_file >> s >> e >> id;
            tasks[id] = (e - s);
            tasks_exec_count[id]++;
            normal_sched.push_back({ s, e, id });
        }
        trace_file >> ng;
        while (ng--) {
            trace_file >> s >> e >> id;
            tasks[id] = (e - s);
            greedy_sched.push_back({ s, e, id });
        }
        trace_file >> nitask;
        _ic = nitask;
        while (nitask--) {
            int arr, ddl, burst;
            trace_file >> arr >> ddl >> burst;
            interactive_tasks.push_back({ 
                arr, JAMScript::InteractiveTaskExtender(burst, ddl, nullptr)
            });
        }
        trace_file >> nbtask;
        _bc = nbtask;
        while (nbtask--) {
            int arr, burst;
            trace_file >> arr >> burst;
            batch_tasks.push_back({ arr, burst });
        }

        JAMScript::Scheduler jamc_sched(normal_sched, greedy_sched, 
                                               0, 1024 * 256, &pbi, 
                                               [](CTask* self, void* args) {

            {
                auto* schedulerPointer = static_cast<JAMScript::Scheduler*>
                    (self->scheduler->GetSchedulerData(self->scheduler));
                auto* batch_interactives = static_cast<bi_dto*>(args);
                auto* interactive_tasks = batch_interactives->
                                          pinteractive_tasks;
                auto* batch_tasks = batch_interactives->pbatch_tasks;
                while (schedulerPointer->GetCurrentTimepointInScheduler() / 1000 < nrounds * normal_sched.back().endTime) {
                    auto curr_timediff = schedulerPointer->GetCurrentTimepointInScheduler() / 1000;
                    for (auto& itask: *interactive_tasks) {
                        if (curr_timediff >= itask.first && 
                            itask.second.handle == nullptr) {
                            itask.second.handle = schedulerPointer->
                            CreateInteractiveTask(itask.second.deadline, 
                                                 itask.second.burst, nullptr, 
                                                 [](CTask* self, void* args) {
                                auto _itask_start = 
                                std::chrono::high_resolution_clock::now();
                                auto* extender = 
                                static_cast<JAMScript::InteractiveTaskExtender*>(
                                    self->taskFunctionVector->GetUserData(self)
                                );    
                                std::cout << "I Task Start on "  << (ThisTask()->scheduler == self->scheduler) << std::endl;

                                long long prevns = this_scheduler()->GetCurrentTimepointInScheduler();
                                JAMScript::SleepFor(1000);
                                long long currns = this_scheduler()->GetCurrentTimepointInScheduler();
                                std::cout << ("JSleep jitter in ns: " + std::to_string(currns - prevns - 1000 * 1000)) << std::endl;
                                while (std::chrono::
                                       duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::
                                       high_resolution_clock::now()
                                       - _itask_start).count() < 1000 * 
                                       extender->burst) {
                                    if (preempt_tslice > 0)
                                    std::this_thread::sleep_for(
                                        std::chrono::
                                        nanoseconds(preempt_tslice)
                                    );
                                    YieldTask(self);
                                }
                                interactive_count++;
                                extender->handle->status = ACK_FINISHED;
                                NotifyFinishOfFuture(extender->handle.get());
                                FinishTask(self, 0);
                            });
                        }
                    }
                    for (auto& btask: *batch_tasks) {
                        if (curr_timediff >= btask.first) {
                            schedulerPointer->CreateBatchTask(btask.second, 
                                                          nullptr, 
                                                          [](CTask* self, 
                                                             void* args) {
                                auto _btask_start = 
                                std::chrono::high_resolution_clock::now();
                                auto* extender = 
                                static_cast<JAMScript::BatchTaskExtender*>(
                                    self->taskFunctionVector->GetUserData(self)
                                );
                                std::cout << "B Task Start on "  << (ThisTask()->scheduler == self->scheduler) << std::endl;
                                long long prevns = this_scheduler()->GetCurrentTimepointInScheduler();
                                JAMScript::SleepFor(1000);
                                long long currns = this_scheduler()->GetCurrentTimepointInScheduler();
                                std::cout << ("JSleep jitter in ns: " + std::to_string(currns - prevns - 1000 * 1000)) << std::endl;
                                while (std::chrono::
                                       duration_cast<std::chrono::nanoseconds>
                                       (std::chrono::
                                       high_resolution_clock::now()
                                       - _btask_start).count() < 1000 * 
                                       extender->burst) {
                                    if (preempt_tslice > 0)
                                    std::this_thread::sleep_for(
                                        std::chrono::
                                        nanoseconds(preempt_tslice));
                                    YieldTask(self);
                                }
                                batch_count++;
                                FinishTask(self, 0);
                            });
                            btask.first = std::numeric_limits<uint64_t>::max();
                        }
                    }
                    std::this_thread::sleep_for(
                        std::chrono::nanoseconds(preempt_tslice)
                    );
                    YieldTask(self);
                }
                schedulerPointer->Exit();
            }
            FinishTask(self, EXIT_SUCCESS);
        });
        for (uint32_t i = 1; i < tasks.size(); i++) {
            task_dtos[i] = { i, tasks[i], &jamc_sched, 
                             JAMScript::REAL_TIME_TASK_T };
            jamc_sched.CreateRealTimeTask(i, &(*(task_dtos.begin() + i)), 
                                          [](CTask* self, void* args) {
                {
                    auto _start_time = std::chrono::
                                       high_resolution_clock::now();
                    auto* pack = static_cast<task_data_transfer*>(args);
                    if (pack->scheduler->GetNumberOfCycleFinished() >= nrounds) {
                        pack->scheduler->Exit();
                        FinishTask(self, EXIT_SUCCESS);
                    }
                    std::cout << "TASK #" << pack->taskId << " " << 
                                 "EXEC"   << std::endl;
                    pack->exec_count++;
                    pack->scheduler->CreateRealTimeTask(pack->taskId, pack, 
                                                        self->TaskFunction);
                    while (std::chrono::duration_cast<std::chrono::nanoseconds>
                           (std::chrono::high_resolution_clock::now() - 
                            _start_time).count() < pack->task_sleep * 1000);
                }
                FinishTask(self, EXIT_SUCCESS);
            });
        }
        jamc_sched.Run();
        for (uint32_t i = 1; i < task_dtos.size(); i++) {
            std::cout << "TASK #" << i << " EXP: " << 
                         jamc_sched.GetNumberOfCycleFinished() * tasks_exec_count[i] << " " <<
                         "ACT: "  << task_dtos[i].exec_count << std::endl;
        }
        std::cout << "NORMAL LOWER BOUND: ";
        int tacc = 0, cacc = 0;
        decltype(std::chrono::high_resolution_clock::now()) test_start = 
        std::chrono::high_resolution_clock::now();
        for (auto& nsc: normal_sched) {
            auto inv_start = std::chrono::high_resolution_clock::now();
            if (nsc.taskId != 0x0) {
                int cj = std::chrono::duration_cast<std::chrono::microseconds>(
                    inv_start - test_start
                ).count() - nsc.startTime;
                std::cout << cj << " ";
                tacc += std::abs(cj);
                cacc++;
            }
            while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now() - inv_start
                   ).count() < (nsc.endTime - nsc.startTime) * 1000);
        }
        std::cout << "AVG: " << double(tacc) / cacc << std::endl;
        std::cout << "GREEDY LOWER BOUND: ";
        tacc = 0, cacc = 0;
        test_start = std::chrono::high_resolution_clock::now();
        for (auto& nsc: greedy_sched) {
            decltype(test_start) inv_start = std::chrono::
                                             high_resolution_clock::now();
            // measure jitter if RT task
            if (nsc.taskId != 0x0) {
                int cj = std::chrono::duration_cast<std::chrono::microseconds>(
                    inv_start - test_start
                ).count() - nsc.startTime;
                std::cout << cj << " ";
                tacc += std::abs(cj);
                cacc++;
            }
            // sleep until finishing this interval
            while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now() - inv_start
                   ).count() < (nsc.endTime - nsc.startTime) * 1000);
        }
        std::cout << "AVG: " << double(tacc) / cacc << std::endl;
        std::cout << "TOTAL-I: " << _ic << ", " <<
                     "FINISHED-I: " << interactive_count << ", " <<
                     "GR-I: " << double(interactive_count) / _ic << std::endl;
        std::cout << "TOTAL-B: " << _bc << ", " <<
                     "FINISHED-B: " << batch_count << ", " <<
                     "GR-B: " << double(batch_count) / _bc << std::endl;
    }
    return 0;
}