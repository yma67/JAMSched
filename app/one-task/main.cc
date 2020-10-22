#include <unistd.h>
#include <chrono>
#include <core/task/task.hpp>
#include <cstdlib>
#include <iostream>
#include <scheduler/scheduler.hpp>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <typeinfo>

int JCalc(std::string a, std::string b) {
    return std::stoi(a) + std::stoi(b);
}

bool is_number(const std::string& s)
{
    return !s.empty() && std::find_if(s.begin(), 
        s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

struct timespec time1, time2;

struct timespec diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0)
    {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    }
    else
    {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

class BenchSched : public jamc::SchedulerBase
{
public:

    void Enable(jamc::TaskInterface *toEnable) override {}
    void EnableImmediately(jamc::TaskInterface *toEnable) override {}
    void RunSchedulerMainLoop() override 
    {
        auto x = GetNextTask();
        clock_gettime(CLOCK_MONOTONIC, &time1);
        if (x != nullptr) x->SwapFrom(nullptr);
    }
    jamc::TaskInterface *GetNextTask() override
    {
        if (idx < tasks.size()) 
        {
            return tasks[idx++];
        }
        return nullptr;
    }
    void EndTask(jamc::TaskInterface *) override
    {
        
    }
    BenchSched(uint32_t stackSize) : jamc::SchedulerBase(stackSize) {}
    ~BenchSched() override { 
        for (int i = 0; i < tasks.size(); i = i + 2)
        {
            delete tasks[i];
        }
    }
    std::vector<jamc::TaskInterface *> tasks;
    int idx = 0;
};

uint64_t glbCount = 0;

BenchSched bSched(256 * 1024), bMSched(1024 * 256);

int main(int argc, char *argv[])
{
    if (argc != 2 || !is_number(std::string(argv[1]))) {
        return EXIT_FAILURE;
    }
#ifndef __APPLE__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    sched_param sch;
    int policy; 
    pthread_getschedparam(pthread_self(), &policy, &sch);
    sch.sched_priority = 50;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch);

    /*bSched.tasks.push_back(new jamc::StandAloneStackTask(&bSched, 1024 * 256, []() {
        clock_gettime(CLOCK_MONOTONIC, &time1);
        jamc::ctask::Yield();
        clock_gettime(CLOCK_MONOTONIC, &time2);
        // std::cout << "1 task ctx switch time: " << diff(time1, time2).tv_nsec << " ns" << std::endl;
    }));

    bSched.RunSchedulerMainLoop();*/

    // allocate 1M coroutines, and swap in/out FIFO order
    for (int i = 0; i < 1000000; i++) {
        auto fx = new jamc::SharedCopyStackTask(&bMSched, [argv]() {
            unsigned int stackSize = atoi(argv[1]);
            char arr[stackSize];
            int idx = rand() % stackSize;
            arr[idx] = rand() % 256;
            srand(arr[idx]);
            for (int i = 0; i < rand() % stackSize; i++) {
                arr[i] = arr[rand() % stackSize] + rand() % 256;
            }
            clock_gettime(CLOCK_MONOTONIC, &time1);
            jamc::ctask::Yield();
            clock_gettime(CLOCK_MONOTONIC, &time2);
            glbCount += diff(time1, time2).tv_nsec;
        });
        bMSched.tasks.push_back(fx);
        bMSched.tasks.push_back(fx);
    }
    printf("%d\n", bMSched.tasks.size());
    bMSched.RunSchedulerMainLoop();
    std::cout << "Avg (1M): " << glbCount / 1000000 << "ns" << std::endl;
    nlohmann::json jx;
    jx.push_back(1);
    jx.push_back(2);
    jx.push_back(3);
    char charr[40];
    std::vector<char> chvec(charr, charr + 40);
    nlohmann::json jxe = { { "args", jx }, { "name", "jxe" }, { "bytes",  chvec } };
    std::cout << jxe["bytes"].is_array() << std::endl;
    assert(jxe["bytes"].get<std::vector<char>>() == chvec);
    return 0;
}