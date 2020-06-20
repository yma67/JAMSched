#include <unistd.h>
#include <chrono>
#include <core/task/task.hpp>
#include <cstdlib>
#include <iostream>
#include <scheduler/scheduler.hpp>

struct timespec time1, time2;

struct timespec diff(struct timespec start, struct timespec end) {
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0) {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

class BenchSched : public JAMScript::SchedulerBase {
public:
    JAMScript::TaskInterface* NextTask() override { return onlyTask; }
    void Enable() {}
    void RunSchedulerMainLoop() {
        this->onlyTask->SwapIn();
        this->onlyTask->SwapIn();
    }
    BenchSched(uint32_t stackSize) : JAMScript::SchedulerBase(stackSize) {}
    ~BenchSched() { delete onlyTask; }
    JAMScript::TaskInterface* onlyTask = nullptr;
};

int main() {
    BenchSched bSched(256 * 1024);
    bSched.onlyTask = new JAMScript::StandAloneStackTask(&bSched, 1024 * 256, []() {
        clock_gettime(CLOCK_MONOTONIC, &time1);
        JAMScript::ThisTask::Yield();
        clock_gettime(CLOCK_MONOTONIC, &time2);
        std::cout << "2xCtx switch time: " << diff(time1, time2).tv_nsec << " ns" << std::endl;
    });
    bSched.RunSchedulerMainLoop();
    return 0;
}