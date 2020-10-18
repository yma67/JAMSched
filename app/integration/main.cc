#include <future>
#include "concurrency/future.hpp"
#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>
#include <cstring>
#include <nlohmann/json.hpp>

int addNumbers(int a, int b) 
{
    printf("a + b = %d\n", a + b); 
    //std::cout << "Add NSync Add of " << a << " + " << b << std::endl;
    return a + b;
}

int scaleNumber(int x) 
{
    return x * 150;
}

double getTime()
{
    return 100.56;
}

int main()
{
    jamc::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCall("DuplicateCString", strdup);
    ribScheduler.RegisterRPCall("RPCFunctionJSync", [] (int a, int b) -> int {
        std::cout << "Sync Add of " << a << " + " << b << std::endl;
        return a + b;
    });
    ribScheduler.RegisterRPCall("RPCFunctionJAsync", [] (int a, int b) -> int {
        std::cout << "Async Subtract of " << a << " - " << b << std::endl;
        return a - b;
    });
    int execCount = 0;
    ribScheduler.RegisterRPCall("ConcatCString", [&execCount, &ribScheduler] (char *dest, const char *src) -> char* {
        printf("please return by a pointer to memory allocated on heap\n");
#ifdef JAMSCRIPT_ENABLE_VALGRIND
        if (execCount++ > 10) ribScheduler.ShutDown();
#else
        if (execCount++ > 100) ribScheduler.ShutDown();
#endif
        return strdup(strcat(dest, src));
    });

    ribScheduler.RegisterRPCall("addNumbers", addNumbers);
    ribScheduler.RegisterRPCall("scaleNumber", scaleNumber);
    ribScheduler.RegisterRPCall("getTime", getTime);
    
    auto slotSize = 1;
    auto tuSleepTime = std::chrono::milliseconds(100);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
                                     
            auto sleepStart = jamc::Clock::now();
            jamc::ctask::SleepFor(tuSleepTime);
            std::cout << "JSleep Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(jamc::Clock::now() - sleepStart - tuSleepTime).count() << " us" << std::endl;
            jamc::ctask::Yield();
            printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
        }
    }).Detach();
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
            jamc::ctask::SleepFor(tuSleepTime);
            printf(">>...........\n");
        }
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}