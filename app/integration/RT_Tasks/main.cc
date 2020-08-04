#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>
#include <time.h>

void hello1() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    printf("Time %ld, %ld\n", t.tv_sec, t.tv_nsec);
}

void hello2() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    printf("Time %ld, %ld\n", t.tv_sec, t.tv_nsec);
}

void hello3() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    printf("Time %ld, %ld\n", t.tv_sec, t.tv_nsec);
}


int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::microseconds(0), std::chrono::microseconds(50), 1}, {std::chrono::microseconds(50), std::chrono::microseconds(100), 2}, {std::chrono::microseconds(100), std::chrono::microseconds(150), 3}, {std::chrono::microseconds(200), std::chrono::microseconds(50000), 0}},
                             {{std::chrono::microseconds(0), std::chrono::microseconds(50), 1}, {std::chrono::microseconds(50), std::chrono::microseconds(100), 2}, {std::chrono::microseconds(100), std::chrono::microseconds(150), 3}, {std::chrono::microseconds(200), std::chrono::microseconds(50000), 0}});
                        
    ribScheduler.CreateRealTimeTask({true, 0}, 1, hello1);
    ribScheduler.CreateRealTimeTask({true, 0}, 2, hello2);
    ribScheduler.CreateRealTimeTask({true, 0}, 3, hello3);

    //std::function(<void>)(hello1));


    ribScheduler.RunSchedulerMainLoop();
    return 0;
}