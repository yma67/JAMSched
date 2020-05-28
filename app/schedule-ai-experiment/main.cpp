#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include <jamscript-impl/jamscript-scheduler.h>

struct task_data_transfer {
    uint32_t task_id, exec_count;
    uint64_t task_sleep;
    jamscript::c_side_scheduler* scheduler;
    jamscript::ctask_types task_type;
    task_data_transfer() : 
    task_id(0), task_sleep(0), scheduler(nullptr), 
    task_type(jamscript::real_time_task_t), exec_count(0) {}
    task_data_transfer(uint32_t task_id, uint64_t task_sleep, 
                       jamscript::c_side_scheduler* scheduler, 
                       jamscript::ctask_types task_type) : 
    task_id(task_id), task_sleep(task_sleep), scheduler(scheduler), 
    task_type(task_type), exec_count(0) {}
};

struct bi_dto {
    std::vector<std::pair<uint64_t, jamscript::interactive_extender>>* 
    pinteractive_tasks;
    std::vector<std::pair<uint64_t, uint64_t>>* pbatch_tasks;
    bi_dto(std::vector<std::pair<uint64_t, jamscript::interactive_extender>>* 
           pi, std::vector<std::pair<uint64_t, uint64_t>>* pb) : 
           pinteractive_tasks(pi), pbatch_tasks(pb) {}
};

int nrounds, batch_count = 0, interactive_count = 0, preempt_tslice = 0, 
    _bc, _ic;

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
    std::vector<jamscript::task_schedule_entry> normal_sched, greedy_sched;
    std::vector<std::pair<uint64_t, jamscript::interactive_extender>> 
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
            jamscript::interactive_extender ie;
            ie.burst = burst;
            ie.deadline = ddl;
            ie.task_type = jamscript::interactive_task_t;
            ie.handle = nullptr;
            interactive_tasks.push_back({ arr, ie });
        }
        trace_file >> nbtask;
        _bc = nbtask;
        while (nbtask--) {
            int arr, burst;
            trace_file >> arr >> burst;
            batch_tasks.push_back({ arr, burst });
        }

        jamscript::c_side_scheduler jamc_sched(normal_sched, greedy_sched, 
                                               1024 * 256, &pbi, 
                                               [](task_t* self, void* args) {

            {
                auto* scheduler_ptr = static_cast<jamscript::c_side_scheduler*>
                    (self->scheduler->get_scheduler_data(self->scheduler));
                auto* batch_interactives = static_cast<bi_dto*>(args);
                auto* interactive_tasks = batch_interactives->
                                          pinteractive_tasks;
                auto* batch_tasks = batch_interactives->pbatch_tasks;
                while (scheduler_ptr->multiplier < nrounds) {
                    auto local_app_current_time = 
                    std::chrono::high_resolution_clock::now();
                    auto curr_timediff = std::chrono::
                    duration_cast<std::chrono::nanoseconds>(
                        local_app_current_time - 
                        scheduler_ptr->scheduler_start_time
                    ).count() / 1000;
                    for (auto& itask: *interactive_tasks) {
                        if (curr_timediff >= itask.first && 
                            itask.second.handle == nullptr) {
                            itask.second.handle = scheduler_ptr->
                            add_interactive_task(self, itask.second.deadline, 
                                                 itask.second.burst, nullptr, 
                                                 [](task_t* self, void* args) {
                                auto _itask_start = 
                                std::chrono::high_resolution_clock::now();
                                auto* extender = 
                                static_cast<jamscript::interactive_extender*>(
                                    self->task_fv->get_user_data(self)
                                );
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
                                    yield_task(self);
                                }
                                interactive_count++;
                                extender->handle->status = ack_finished;
                                notify_future(extender->handle.get());
                                finish_task(self, 0);
                            });
                        }
                    }
                    for (auto& btask: *batch_tasks) {
                        if (curr_timediff >= btask.first) {
                            scheduler_ptr->add_batch_task(btask.second, 
                                                          nullptr, 
                                                          [](task_t* self, 
                                                             void* args) {
                                auto _btask_start = 
                                std::chrono::high_resolution_clock::now();
                                auto* extender = 
                                static_cast<jamscript::batch_extender*>(
                                    self->task_fv->get_user_data(self)
                                );
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
                                    yield_task(self);
                                }
                                batch_count++;
                                finish_task(self, 0);
                            });
                            btask.first = std::numeric_limits<uint64_t>::max();
                        }
                    }
                    std::this_thread::sleep_for(
                        std::chrono::nanoseconds(preempt_tslice)
                    );
                    yield_task(self);
                }
                scheduler_ptr->exit();
            }
            finish_task(self, EXIT_SUCCESS);
        });
        for (uint32_t i = 1; i < tasks.size(); i++) {
            task_dtos[i] = { i, tasks[i], &jamc_sched, 
                             jamscript::real_time_task_t };
            jamc_sched.add_real_time_task(i, &(*(task_dtos.begin() + i)), 
                                          [](task_t* self, void* args) {
                {
                    auto _start_time = std::chrono::
                                       high_resolution_clock::now();
                    auto* pack = static_cast<task_data_transfer*>(args);
                    if (pack->scheduler->multiplier >= nrounds) {
                        pack->scheduler->exit();
                        finish_task(self, EXIT_SUCCESS);
                    }
                    std::cout << "TASK #" << pack->task_id << " " << 
                                 "EXEC"   << std::endl;
                    pack->exec_count++;
                    pack->scheduler->add_real_time_task(pack->task_id, pack, 
                                                        self->task_function);
                    while (std::chrono::duration_cast<std::chrono::nanoseconds>
                           (std::chrono::high_resolution_clock::now() - 
                            _start_time).count() < pack->task_sleep * 1000);
                }
                finish_task(self, EXIT_SUCCESS);
            });
        }
        jamc_sched.run();
        for (uint32_t i = 1; i < task_dtos.size(); i++) {
            std::cout << "TASK #" << i << " EXP: " << 
                         jamc_sched.multiplier * tasks_exec_count[i] << " " <<
                         "ACT: "  << task_dtos[i].exec_count << std::endl;
        }
        std::cout << "NORMAL LOWER BOUND: ";
        int tacc = 0, cacc = 0;
        decltype(std::chrono::high_resolution_clock::now()) test_start = 
        std::chrono::high_resolution_clock::now();
        for (auto& nsc: normal_sched) {
            auto inv_start = std::chrono::high_resolution_clock::now();
            if (nsc.task_id != 0x0) {
                int cj = std::chrono::duration_cast<std::chrono::microseconds>(
                    inv_start - test_start
                ).count() - nsc.start_time;
                std::cout << cj << " ";
                tacc += std::abs(cj);
                cacc++;
            }
            while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now() - inv_start
                   ).count() < (nsc.end_time - nsc.start_time) * 1000);
        }
        std::cout << "AVG: " << double(tacc) / cacc << std::endl;
        std::cout << "GREEDY LOWER BOUND: ";
        tacc = 0, cacc = 0;
        test_start = std::chrono::high_resolution_clock::now();
        for (auto& nsc: greedy_sched) {
            decltype(test_start) inv_start = std::chrono::
                                             high_resolution_clock::now();
            // measure jitter if RT task
            if (nsc.task_id != 0x0) {
                int cj = std::chrono::duration_cast<std::chrono::microseconds>(
                    inv_start - test_start
                ).count() - nsc.start_time;
                std::cout << cj << " ";
                tacc += std::abs(cj);
                cacc++;
            }
            // sleep until finishing this interval
            while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now() - inv_start
                   ).count() < (nsc.end_time - nsc.start_time) * 1000);
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