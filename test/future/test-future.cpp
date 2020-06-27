#include <scheduler/scheduler.hpp>
#include <scheduler/tasklocal.hpp>
#include <concurrency/future.hpp>
#include <catch2/catch.hpp>
#include <thread>
#include <chrono>
#include <pthread.h>

TEST_CASE("Performance Future", "[future]")
{

    std::chrono::duration dt = std::chrono::nanoseconds(0);
#if defined(JAMSCRIPT_ENABLE_VALGRIND)
    const int nIter = 1;
#elif JAMSCRIPT_ON_TRAVIS == 1
    const int nIter = 30;
    WARN(nIter);
#else
    const int nIter = 3000;
#endif
    pthread_barrier_t *barrier = new pthread_barrier_t;
    ;
    pthread_barrier_init(barrier, NULL, 2);
    for (int i = 0; i < nIter; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        auto p = std::make_shared<JAMScript::Promise<std::chrono::high_resolution_clock::time_point>>();
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});

        ribScheduler.CreateBatchTask({false, 1024 * 32, false}, std::chrono::milliseconds(90), [barrier, p, &dt, &ribScheduler]() {
                        auto fut = p->GetFuture();
                        pthread_barrier_wait(barrier);
                        // std::this_thread::sleep_for(std::chrono::microseconds(100));
                        //std::cout << "Before Get" << std::endl;
                        auto ts = fut.Get();
                        //std::cout << "After Get" << std::endl;
                        dt += std::chrono::high_resolution_clock::now() - ts;
                        ribScheduler.ShutDown();
                    })
            .Detach();
        std::thread t([barrier, p]() {
            pthread_barrier_wait(barrier);
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
            p->SetValue(std::chrono::high_resolution_clock::now());
        });
        t.detach();
        ribScheduler.RunSchedulerMainLoop();
    }
    delete barrier;
    WARN("AVG Latency: " << std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count() / nIter << "ns");
}

TEST_CASE("InterLock", "[future]")
{
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    std::string sec("");
#else
    std::string sec("muthucumaru maheswaran loves java");
#endif
    JAMScript::RIBScheduler ribScheduler(1024 * 256);
    auto p = std::make_shared<JAMScript::Promise<std::chrono::high_resolution_clock::time_point>>();
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [sec, p, &ribScheduler]() {
        auto pt = std::make_shared<JAMScript::Promise<std::string>>();
        auto prev = pt;
        for (auto ch : sec)
        {
            auto p = std::make_shared<JAMScript::Promise<std::string>>();
            ribScheduler.CreateBatchTask({false, 1024 * 256, true}, std::chrono::milliseconds(90), [p, ch, prev]() {
                            auto sx = p->GetFuture().Get();
                            prev->SetValue(ch + sx);
                        })
                .Detach();
            prev = p;
        }
        prev->SetValue("");
        auto ans = pt->GetFuture().Get();
        REQUIRE(ans == sec);
        WARN(ans);
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
}

TEST_CASE("LExec", "[future]")
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    ribScheduler.RegisterNamedExecution("testExec", [](int a, int b) -> int {
        return a + b;
    });
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [&ribScheduler]() {
        auto fu = ribScheduler.CreateLocalNamedInteractiveExecution<int>({false, 1024}, std::chrono::milliseconds(1000), std::chrono::microseconds(50), std::string("testExec"), 3, 4);
        JAMScript::ThisTask::SleepFor(std::chrono::microseconds(100));
        REQUIRE(fu.Get() == 7);
        ribScheduler.ShutDown();
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
}