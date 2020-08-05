#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>
#include <time.h>

void hello3() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    printf("Time %ld, %ld\n", t.tv_sec, t.tv_nsec);
}

void hello2() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    printf("Time %ld, %ld\n", t.tv_sec, t.tv_nsec);
}

int hello1(JAMScript::RIBScheduler &js, int x) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    printf("%d Time %ld, %ld\n", x, t.tv_sec, t.tv_nsec);
    js.CreateRemoteExecAsync(std::string("helloj"), std::string(""), 0);
    return 0;
}


int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::microseconds(0), std::chrono::microseconds(500), 1}, {std::chrono::microseconds(500), std::chrono::microseconds(1000), 2}, {std::chrono::microseconds(1000), std::chrono::microseconds(1500), 3}, {std::chrono::microseconds(2000), std::chrono::microseconds(50000), 0}},
                             {{std::chrono::microseconds(0), std::chrono::microseconds(500), 1}, {std::chrono::microseconds(500), std::chrono::microseconds(1000), 2}, {std::chrono::microseconds(1000), std::chrono::microseconds(1500), 3}, {std::chrono::microseconds(2000), std::chrono::microseconds(50000), 0}});

    ribScheduler.CreateRealTimeTask({true, 0}, 1, std::function<int(JAMScript::RIBScheduler &, int)>(hello1), std::ref(ribScheduler), 5);                        
    ribScheduler.CreateRealTimeTask({true, 0}, 3, hello3);
    ribScheduler.CreateRealTimeTask({true, 0}, 2, hello2);

    //ribScheduler.CreateRealTimeTask({true, 0}, 1, std::function<int(int)>(hello1), 5);    

    //std::function(<void>)(hello1));


    ribScheduler.RunSchedulerMainLoop();
    return 0;
}