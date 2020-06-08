//
// Created by mayuxiang on 2020-05-20.
//
#include <core/scheduler/task.h>
#include <sys/resource.h>
#include <xtask/shared-stack-task.h>

#include <catch2/catch.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <jamscript-impl/jamscript-future.hh>
#include <jamscript-impl/jamscript-remote.hh>
#include <jamscript-impl/jamscript-scheduler.hh>
#include <thread>
#include <utility>
#include <vector>

int r1c, r2c, r3c, r4c;
bool b1c = false, i1c = false;
uint32_t sleep_time;

TEST_CASE("Scheduling-Paper-Sanity", "[jsched]") {
    r1c = r2c = r3c = r4c = 0;
    sleep_time = 1000;
    JAMScript::Scheduler jamc_sched(
        {{0 * 1000, 1 * 1000, 1},   {1 * 1000, 3 * 1000, 4},   {3 * 1000, 4 * 1000, 3},
         {4 * 1000, 5 * 1000, 2},   {5 * 1000, 6 * 1000, 1},   {6 * 1000, 8 * 1000, 4},
         {8 * 1000, 9 * 1000, 3},   {9 * 1000, 10 * 1000, 0},  {10 * 1000, 11 * 1000, 1},
         {11 * 1000, 12 * 1000, 2}, {12 * 1000, 14 * 1000, 4}, {14 * 1000, 15 * 1000, 0},
         {15 * 1000, 16 * 1000, 1}, {16 * 1000, 17 * 1000, 3}, {17 * 1000, 18 * 1000, 0},
         {18 * 1000, 20 * 1000, 4}, {20 * 1000, 21 * 1000, 1}, {21 * 1000, 22 * 1000, 2},
         {22 * 1000, 22500, 0},     {22500, 23500, 3},         {23500, 24 * 1000, 0},
         {24 * 1000, 26 * 1000, 4}, {26 * 1000, 27 * 1000, 1}, {27 * 1000, 30 * 1000, 0}},
        {{0 * 1000, 2 * 1000, 4},   {2 * 1000, 3 * 1000, 1},   {3 * 1000, 4 * 1000, 3},
         {4 * 1000, 5 * 1000, 2},   {5 * 1000, 9 * 1000, 0},   {9 * 1000, 10 * 1000, 1},
         {10 * 1000, 12 * 1000, 4}, {12 * 1000, 13 * 1000, 1}, {13 * 1000, 14 * 1000, 3},
         {14 * 1000, 16 * 1000, 4}, {16 * 1000, 17 * 1000, 2}, {17 * 1000, 18 * 1000, 1},
         {18 * 1000, 19 * 1000, 3}, {19 * 1000, 21 * 1000, 4}, {21 * 1000, 22 * 1000, 2},
         {22 * 1000, 23 * 1000, 1}, {23 * 1000, 24 * 1000, 3}, {24 * 1000, 26 * 1000, 4},
         {26 * 1000, 27 * 1000, 1}, {27 * 1000, 30 * 1000, 0}},
        0, 1024 * 256, nullptr, [](CTask* self, void* args) {
            std::cout << "LOCAL START" << std::endl;
            YieldTask(self);
            auto* scheduler_ptr = static_cast<JAMScript::Scheduler*>(
                self->scheduler->GetSchedulerData(self->scheduler));
            for (int v = 0; v < 2; v++) {
                std::cout << "FINISHED PSEUDO PREEMPT A" << std::endl;
                std::shared_ptr<CFuture> handle_interactive1 = scheduler_ptr->CreateInteractiveTask(
                    30 * 1000, 500, &i1c, [](CTask* self, void* args) {
                        {
                            REQUIRE(self == ThisTask());
                            auto* i1cp = static_cast<bool*>(args);
                            auto* self_cpp = static_cast<JAMScript::InteractiveTaskExtender*>(
                                self->taskFunctionVector->GetUserData(self));
                            *i1cp = true;
                            std::cout << "INTERAC" << std::endl;
                            for (int i = 0; i < 100; i++) {
                                std::this_thread::sleep_for(std::chrono::microseconds(5));
                                YieldTask(self);
                            }
                            NotifyFinishOfFuture(self_cpp->handle.get());
                        }
                        FinishTask(self, EXIT_SUCCESS);
                    });
                std::cout << "FINISHED PSEUDO PREEMPT B" << std::endl;
                scheduler_ptr->CreateBatchTask(500, &sleep_time, [](CTask* self, void* args) {
                    REQUIRE(self == ThisTask());
                    for (int i = 0; i < 100; i++) {
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(*static_cast<uint32_t*>(args) / 200));
                        YieldTask(self);
                    }
                    b1c = true;
                    std::cout << "BATCH" << std::endl;
                    YieldTask(self);
                });
                WaitForValueFromFuture(handle_interactive1.get());
                std::cout << "GOT HANDLE" << std::endl;
                if (handle_interactive1->status == ACK_CANCELLED)
                    i1c = true;
            }
            while (scheduler_ptr->GetCurrentTimepointInScheduler() / 1000 < 3 * 30 * 1000) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                YieldTask(self);
            }
            scheduler_ptr->Exit();
            FinishTask(self, EXIT_SUCCESS);
        });
    auto rt1 = [](CTask* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, JAMScript::Scheduler*>*>(args);
            REQUIRE(self == ThisTask());
            std::this_thread::sleep_for(std::chrono::microseconds(900));
            std::cout << "TASK1 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->CreateRealTimeTask(1, args, self->TaskFunction);
        }
        FinishTask(self, EXIT_SUCCESS);
    };
    auto rt2 = [](CTask* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, JAMScript::Scheduler*>*>(args);
            std::this_thread::sleep_for(std::chrono::microseconds(900));
            REQUIRE(self == ThisTask());
            std::cout << "TASK2 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->CreateRealTimeTask(2, args, self->TaskFunction);
        }
        FinishTask(self, EXIT_SUCCESS);
    };
    auto rt3 = [](CTask* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, JAMScript::Scheduler*>*>(args);
            std::this_thread::sleep_for(std::chrono::microseconds(900));
            REQUIRE(self == ThisTask());
            std::cout << "TASK3 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->CreateRealTimeTask(3, args, self->TaskFunction);
        }
        FinishTask(self, EXIT_SUCCESS);
    };
    auto rt4 = [](CTask* self, void* args) {
        {
            auto pack = *static_cast<std::pair<int*, JAMScript::Scheduler*>*>(args);
            std::this_thread::sleep_for(std::chrono::microseconds(1900));
            REQUIRE(self == ThisTask());
            std::cout << "TASK4 EXEC" << std::endl;
            (*(pack.first))++;
            pack.second->CreateRealTimeTask(4, args, self->TaskFunction);
        }
        FinishTask(self, EXIT_SUCCESS);
    };
    std::pair<void*, JAMScript::Scheduler*> pack1({&r1c, &jamc_sched}), pack2({&r2c, &jamc_sched}),
        pack3({&r3c, &jamc_sched}), pack4({&r4c, &jamc_sched});
    jamc_sched.CreateRealTimeTask(1, &pack1, rt1);
    jamc_sched.CreateRealTimeTask(2, &pack2, rt2);
    jamc_sched.CreateRealTimeTask(3, &pack3, rt3);
    jamc_sched.CreateRealTimeTask(4, &pack4, rt4);
    jamc_sched.Run();
    REQUIRE(i1c);
#ifndef JAMSCRIPT_ENABLE_VALGRIND
#ifdef __x86_64__
    REQUIRE(b1c);
#endif
    REQUIRE(r1c >= 6);
    REQUIRE(r2c >= 3);
    REQUIRE(r3c >= 4);
    REQUIRE(r4c >= 5);
#endif
}

int CiteLabAdditionFunctionInteractive(int a, char b, float c, short d, double e, long f,
                                       std::string validator) {
    REQUIRE(1 == a);
    REQUIRE(2 == b);
    REQUIRE(Approx(0.5) == c);
    REQUIRE(3 == d);
    REQUIRE(Approx(1.25) == e);
    REQUIRE(4 == f);
    REQUIRE(validator == "citelab loves java interactive");
    return a + b + d + f;
}

int CiteLabAdditionFunctionRealTime(int a, char b, float c, short d, double e, long f,
                                    std::string validator) {
    REQUIRE(1 == a);
    REQUIRE(2 == b);
    REQUIRE(Approx(0.5) == c);
    REQUIRE(3 == d);
    REQUIRE(Approx(1.25) == e);
    REQUIRE(4 == f);
    REQUIRE(validator == "citelab loves java real time");
    return a + b + d + f;
}

int CiteLabAdditionFunctionBatch(int a, char b, float c, short d, double e, long f,
                                 std::string validator) {
    REQUIRE(1 == a);
    REQUIRE(2 == b);
    REQUIRE(Approx(0.5) == c);
    REQUIRE(3 == d);
    REQUIRE(Approx(1.25) == e);
    REQUIRE(4 == f);
    REQUIRE(validator == "citelab loves java batch");
    return a + b + d + f;
}

int CiteLabAdditionFunctionNotifier(std::shared_ptr<JAMScript::Future<std::string>> secret_arch,
                                    std::string validator) {
    long long prevns = this_scheduler()->GetCurrentTimepointInScheduler();
    JAMScript::SleepFor(200);
    long long currns = this_scheduler()->GetCurrentTimepointInScheduler();
    REQUIRE(currns >= prevns + 200 * 1000);
    WARN("JSleep jitter in ns: " + std::to_string(currns - prevns - 200 * 1000));
    secret_arch->SetValue(validator);
    return 888888;
}

int CiteLabAdditionFunctionWaiter(std::shared_ptr<JAMScript::Future<std::string>> secret_arch,
                                  std::string validator) {
    REQUIRE(validator == secret_arch->Get());
    return 233333;
}

JAMScript::RemoteExecutionAgent rh;

TEST_CASE("CreateLocalNamedTaskAsync", "[jsched]") {
    JAMScript::Scheduler jamc_sched(
        {{0, 10 * 1000, 0}, {0, 20 * 1000, 1}, {0, 30 * 1000, 0}},
        {{0, 10 * 1000, 0}, {0, 20 * 1000, 1}, {0, 30 * 1000, 0}}, 888, 1024 * 256, nullptr,
        [](CTask* self, void* args) {
            auto* scheduler_ptr = static_cast<JAMScript::Scheduler*>(
                self->scheduler->GetSchedulerData(self->scheduler));
            {
                auto res = scheduler_ptr->CreateLocalNamedTaskAsync<int>(
                    uint64_t(30 * 1000), uint64_t(500), "citelab i", 1, 2, float(0.5), 3,
                    double(1.25), 4, std::string("citelab loves java interactive"));

                auto resrt = scheduler_ptr->CreateLocalNamedTaskAsync<int>(
                    uint32_t(1), "citelab r", 1, 2, float(0.5), 3, double(1.25), 4,
                    std::string("citelab loves java real time"));
                auto resb = scheduler_ptr->CreateLocalNamedTaskAsync<int>(
                    uint64_t(5), "citelab b", 1, 2, float(0.5), 3, double(1.25), 4,
                    std::string("citelab loves java batch"));
                // assume fogonly
                auto resrm = rh.RegisterRemoteExecution("what is the source of memory leak",
                                                        "this == fog", 1, 3, 2);
                auto resrm2 = rh.RegisterRemoteExecution("what is the source of memory leak",
                                                         "this == fog", 0, 3, 2);
                REQUIRE(JAMScript::ExtractValueFromLocalNamedExecution<int>(res) == 10);
                REQUIRE(JAMScript::ExtractValueFromLocalNamedExecution<int>(resrt) == 10);
                REQUIRE(JAMScript::ExtractValueFromLocalNamedExecution<int>(resb) == 10);
                REQUIRE(JAMScript::ExtractValueFromRemoteNamedExecution<std::string>(resrm) ==
                        "command, condvec, and fmask!");
                auto p = std::make_shared<JAMScript::Future<std::string>>();
                REQUIRE(JAMScript::ExtractValueFromLocalNamedExecution<int>(
                            scheduler_ptr->CreateLocalNamedTaskAsync<int>(
                                uint64_t(30 * 1000), uint64_t(500), "citelab n", p,
                                std::string("aarch64"))) == 888888);
                REQUIRE(JAMScript::ExtractValueFromLocalNamedExecution<int>(
                            scheduler_ptr->CreateLocalNamedTaskAsync<int>(
                                uint64_t(5), "citelab w", p, std::string("aarch64"))) == 233333);
                REQUIRE_THROWS_WITH(JAMScript::ExtractValueFromRemoteNamedExecution<int>(resrm2),
                                    Catch::Contains("wrong device, should be fog only"));
            }
            scheduler_ptr->Exit();
            FinishTask(self, 0);
        });
    jamc_sched.RegisterNamedExecution("citelab i",
                                      reinterpret_cast<void*>(CiteLabAdditionFunctionInteractive));
    jamc_sched.RegisterNamedExecution("citelab r",
                                      reinterpret_cast<void*>(CiteLabAdditionFunctionRealTime));
    jamc_sched.RegisterNamedExecution("citelab b",
                                      reinterpret_cast<void*>(CiteLabAdditionFunctionBatch));
    jamc_sched.RegisterNamedExecution("citelab n",
                                      reinterpret_cast<void*>(CiteLabAdditionFunctionNotifier));
    jamc_sched.RegisterNamedExecution("citelab w",
                                      reinterpret_cast<void*>(CiteLabAdditionFunctionWaiter));
    std::thread t(std::ref(rh), [&]() { return jamc_sched.IsSchedulerRunning(); });
    jamc_sched.Run();
    t.join();
}