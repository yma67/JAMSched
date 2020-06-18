#include <scheduler/scheduler.h>
#include <scheduler/tasklocal.h>
#include <concurrency/future.h>
#include <catch2/catch.hpp>
#include <thread>
#include <chrono>
#include <pthread.h>

pthread_barrier_t barrier;

TEST_CASE("Performance Future", "[future]") {
    
    std::chrono::duration dt = std::chrono::nanoseconds(0);
    const int nIter = 100;
    for (int i = 0; i < nIter; i++) {
        pthread_barrier_init(&barrier, NULL, 2);
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        auto p = std::make_shared<JAMScript::Promise<std::chrono::high_resolution_clock::time_point>>();
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
        
        ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::milliseconds(90), [p, &dt, &ribScheduler]() {
            auto ff = p->GetFuture();
            pthread_barrier_wait(&barrier);
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
            auto ts = ff.Get();
            dt += std::chrono::high_resolution_clock::now() - ts;
            ribScheduler.ShutDown();
        });
        std::thread tn([p]() {
            pthread_barrier_wait(&barrier);
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
            p->SetValue(std::chrono::high_resolution_clock::now());
        });
        ribScheduler.Run();
        tn.join();
    }
    WARN("AVG Latency: " << std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count() / nIter << "ns");
}


TEST_CASE("InterLock", "[future]") {
    std::string sec("muthucumaru maheswaran loves java");
    JAMScript::RIBScheduler ribScheduler(1024 * 256);
    auto p = std::make_shared<JAMScript::Promise<std::chrono::high_resolution_clock::time_point>>();
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [sec, p, &ribScheduler]() {
        auto pt = std::make_shared<JAMScript::Promise<std::string>>();
        auto prev = pt;
        for (auto ch: sec) {
            auto p = std::make_shared<JAMScript::Promise<std::string>>();
            ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [p, ch, prev](){
                auto sx = p->GetFuture().Get();
                prev->SetValue(ch + sx);
            });
            prev = p;
        }
        prev->SetValue("");
        auto ans = pt->GetFuture().Get();
        REQUIRE(ans == sec);
        WARN(ans);
        ribScheduler.ShutDown();
    });
    ribScheduler.Run();
}