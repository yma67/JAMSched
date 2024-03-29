#include <jamscript>
#include <catch2/catch.hpp>
#include <thread>
#include <chrono>
#include <pthread.h>
#ifdef __APPLE__

#ifndef PTHREAD_BARRIER_H_
#define PTHREAD_BARRIER_H_

#include <errno.h>

typedef int pthread_barrierattr_t;
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int tripCount;
} pthread_barrier_t;


int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count)
{
    if(count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if(pthread_mutex_init(&barrier->mutex, 0) < 0)
    {
        return -1;
    }
    if(pthread_cond_init(&barrier->cond, 0) < 0)
    {
        pthread_mutex_destroy(&barrier->mutex);
        return -1;
    }
    barrier->tripCount = count;
    barrier->count = 0;

    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    pthread_cond_destroy(&barrier->cond);
    pthread_mutex_destroy(&barrier->mutex);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    pthread_mutex_lock(&barrier->mutex);
    ++(barrier->count);
    if(barrier->count >= barrier->tripCount)
    {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
        pthread_mutex_unlock(&barrier->mutex);
        return 1;
    }
    else
    {
        pthread_cond_wait(&barrier->cond, &(barrier->mutex));
        pthread_mutex_unlock(&barrier->mutex);
        return 0;
    }
}

#endif // PTHREAD_BARRIER_H_
#endif // __APPLE__
TEST_CASE("Performance future", "[future]")
{

    std::chrono::duration dt = std::chrono::nanoseconds(0);
#if defined(JAMSCRIPT_ENABLE_VALGRIND)
    const int nIter = 1;
#else
    const int nIter = 3000;
#endif
    pthread_barrier_t *barrier = reinterpret_cast<pthread_barrier_t *>(calloc(sizeof(pthread_barrier_t), 1));
    pthread_barrier_init(barrier, NULL, 2);
    for (int i = 0; i < nIter; i++)
    {
        jamc::RIBScheduler ribScheduler(1024 * 256);
        auto *p = new jamc::promise<std::chrono::steady_clock::time_point>();
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});

        ribScheduler.CreateBatchTask({false, 1024 * 32, false}, std::chrono::milliseconds(90), [barrier, p, &dt, &ribScheduler]() {
            auto fut = p->get_future();
            pthread_barrier_wait(barrier);
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
            //std::cout << "Before get" << std::endl;
            auto ts = fut.get();
            //std::cout << "After get" << std::endl;
            dt += std::chrono::steady_clock::now() - ts;
            ribScheduler.ShutDown();
            delete p;
        }).Detach();
        ribScheduler.CreateBatchTask({false, 1024 * 32, true}, std::chrono::milliseconds(90), [barrier, p, &ribScheduler]() {
            pthread_barrier_wait(barrier);
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
            p->set_value(std::chrono::steady_clock::now());
            jamc::ctask::Yield();
        }).Detach();
        ribScheduler.RunSchedulerMainLoop();
    }
    pthread_barrier_destroy(barrier);
    free(barrier);
    WARN("AVG Latency: " << std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count() / nIter << "ns");
}

TEST_CASE("InterLock", "[future]")
{
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    std::string sec("mm");
#else
    std::string sec("muthucumaru maheswaran loves java");
#endif
    jamc::RIBScheduler ribScheduler(1024 * 256);
    auto p = std::make_shared<jamc::promise<std::chrono::steady_clock::time_point>>();
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [sec, p, &ribScheduler]() {
        auto pt = std::make_shared<jamc::promise<std::string>>();
        auto prev = pt;
        for (auto ch : sec)
        {
            auto p = std::make_shared<jamc::promise<std::string>>();
            ribScheduler.CreateBatchTask({false, 1024 * 256, true}, std::chrono::milliseconds(90), [p, ch, prev]() {
                            auto sx = p->get_future().get();
                            prev->set_value(ch + sx);
                        })
                .Detach();
            prev = p;
        }
        prev->set_value("");
        auto ans = pt->get_future().get();
        REQUIRE(ans == sec);
        WARN(ans);
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
}

TEST_CASE("LExec", "[future]")
{
    jamc::RIBScheduler ribScheduler(1024 * 256);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    ribScheduler.RegisterLocalExecution("testExec", [](int a, int b) -> int {
        return a + b;
    });
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [&ribScheduler]() {
        auto fu = ribScheduler.CreateLocalNamedInteractiveExecution<int>({false, 1024 * 256}, std::chrono::milliseconds(1000), std::chrono::microseconds(50), std::string("testExec"), 3, 4);
#ifndef JAMSCRIPT_ON_TRAVIS
        jamc::ctask::SleepFor(std::chrono::microseconds(100));
#endif
        REQUIRE(fu.get() == 7);
        ribScheduler.ShutDown();
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
}