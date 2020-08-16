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
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
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
                                     
            auto sleepStart = JAMScript::Clock::now();
            JAMScript::ThisTask::SleepFor(tuSleepTime);
            std::cout << "JSleep Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(JAMScript::Clock::now() - sleepStart - tuSleepTime).count() << " us" << std::endl;
            // JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
            // auto q = ribScheduler.ExtractRemote<int>(jf);
            // jf.Get();
            // std::cout << jf << std::endl;
            JAMScript::ThisTask::Yield();
            printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
        }
    }).Detach();
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
            JAMScript::ThisTask::SleepFor(tuSleepTime);
            printf(">>...........\n");
            JAMScript::ThisTask::Exit();
/*            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecSync(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
            try {
                auto q = ribScheduler.ExtractRemote<int>(jf);
            } catch (std::exception e) {
                e.what();
            } */
            // std::cout << jf << std::endl;
            JAMScript::ThisTask::Yield();
        }
    }).Detach();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}