#include <future>
#include "concurrency/future.hpp"
#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>
#include <cstring>
#include <nlohmann/json.hpp>

int RPCFunctionJSync(int a, int b)
{
    std::cout << "Sync Add of " << a << " + " << b << std::endl;
    return a + b;
}

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



int RPCFunctionJAsync(int a, int b)
{
    std::cout << "Async Subtract of " << a << " - " << b << std::endl;
    return a - b;
}

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCall("addNumbers", addNumbers);
    ribScheduler.RegisterRPCall("scaleNumber", scaleNumber);
    ribScheduler.RegisterRPCall("getTime", getTime);
    ribScheduler.RegisterRPCall("RPCFunctionJSync", RPCFunctionJSync);
    ribScheduler.RegisterRPCall("RPCFunctionJAsync", RPCFunctionJAsync);
    ribScheduler.RegisterRPCall("DuplicateCString", strdup);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                        
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf("==============================================\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
 //           ribScheduler.ExtractRemote(&jf);
//            jf.Get();
            //std::cout << jf << std::endl;
        }
    });
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf(">>...........\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
//            int q = ribScheduler.ExtractRemote(&jf);
            //std::cout << jf << std::endl;
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}