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

bool b1c = false, i1c = false;
uint32_t sleep_time = 1000;

int main(int argc, char *argv[]) {
    if (argc != 2) return EXIT_FAILURE;
    int nn, ng, ntask;
    uint64_t s, e;
    uint32_t id;
    std::ifstream trace_file(argv[1]);
    std::vector<uint64_t> tasks, tasks_exec_count;
    std::vector<task_data_transfer> task_dtos;
    std::vector<jamscript::task_schedule_entry> normal_sched, greedy_sched;
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
        jamscript::c_side_scheduler jamc_sched(normal_sched, greedy_sched, 
                                               1024 * 256, nullptr, 
                                               [](task_t* self, void* args) {
            {
                std::cout << "LOCAL START" << std::endl;
                yield_task(self);
                auto* scheduler_ptr = static_cast<jamscript::c_side_scheduler*>
                    (self->scheduler->get_scheduler_data(self->scheduler));
                for (int v = 0; v < 2; v++) {
                    std::cout << "FINISHED PSEUDO PREEMPT A" << std::endl;
                    std::shared_ptr<jamfuture_t> handle_interactive1 = 
                    scheduler_ptr->
                    add_interactive_task(self, 18000, 9000, &i1c, 
                                        [] (task_t* self, void* args) {
                        {
                            auto* i1cp = static_cast<bool*>(args);
                            auto* self_cpp = static_cast<
                                    jamscript::interactive_extender*
                                >(
                                    self->task_fv->get_user_data(self)
                                );
                            *i1cp = true;
                            for (int i = 0; i < 160; i++) {
                                std::this_thread::sleep_for(
                                    std::chrono::microseconds(50)
                                );
                                yield_task(self);
                            }
                            std::cout << "INTERAC" << std::endl;
                            notify_future(self_cpp->handle.get());
                        }
                        finish_task(self, EXIT_SUCCESS);
                    });
                    std::cout << "FINISHED PSEUDO PREEMPT B" << std::endl;
                    scheduler_ptr->add_batch_task(500, &sleep_time, 
                                                [] (task_t* self, void* args) {
                        for (int i = 0; i < 100; i++) {
                            std::this_thread::sleep_for(
                                    std::chrono::microseconds(
                                        *static_cast<uint32_t*>(args) / 200
                                    )
                                );
                            yield_task(self);
                        }
                        b1c = true;
                        std::cout << "BATCH" << std::endl;
                        yield_task(self);
                    });
                    get_future(handle_interactive1.get());
                    std::cout << "GOT HANDLE" << std::endl;
                    if (handle_interactive1->status == ack_cancelled) {
                        i1c = true;
                    }
                    for (int i = 0; i < 240; i++) {
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                        yield_task(self);
                    }
                }
                while (scheduler_ptr->multiplier < 3) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
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
                    auto* pack = static_cast<task_data_transfer*>(args);
                    if (pack->scheduler->multiplier >= 3) {
                        pack->scheduler->exit();
                        finish_task(self, EXIT_SUCCESS);
                    }
                    std::this_thread::sleep_for(
                            std::chrono::microseconds(pack->task_sleep)
                        );
                    std::cout << "TASK #" << pack->task_id << " " << 
                                 "EXEC"   << std::endl;
                    pack->exec_count++;
                    pack->scheduler->add_real_time_task(pack->task_id, pack, 
                                                        self->task_function);
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
    }
    return 0;
}