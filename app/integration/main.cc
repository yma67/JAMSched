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
auto addNumberFunctor = std::function(addNumbers);
auto scaleNumberFunctor = std::function(scaleNumber);
auto getTimeFunctor = std::function(getTime);
auto addNumberInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(addNumberFunctor)>(addNumberFunctor);
auto scaleNumberInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(scaleNumberFunctor)>(scaleNumberFunctor);
auto getTimeInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(getTimeFunctor)>(getTimeFunctor);
auto RPCFunctionJSyncFunctor = std::function(RPCFunctionJSync);
auto RPCFunctionJSyncInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(RPCFunctionJSyncFunctor)>(RPCFunctionJSyncFunctor);
auto RPCFunctionJAsyncFunctor = std::function(RPCFunctionJAsync);
auto RPCFunctionJAsyncInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(RPCFunctionJAsyncFunctor)>(RPCFunctionJAsyncFunctor);
auto DuplicateCStringFunctor = std::function(strdup);
auto DuplicateCStringInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(DuplicateCStringFunctor)>(DuplicateCStringFunctor);
std::unordered_map<std::string, const JAMScript::RExecDetails::RoutineInterface *> invokerMap = {
    {std::string("addNumbers"), &addNumberInvoker},
    {std::string("scaleNumber"), &scaleNumberInvoker},    
    {std::string("getTime"), &getTimeInvoker},
    
    {std::string("RPCFunctionJSync"), &RPCFunctionJSyncInvoker},
    {std::string("RPCFunctionJAsync"), &RPCFunctionJAsyncInvoker},
    {std::string("DuplicateCString"), &DuplicateCStringInvoker}
};
int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCalls(invokerMap);
    auto slotSize = 1;
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
                        
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::high_resolution_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
                                     
            auto sleepStart = JAMScript::Clock::now();
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            std::cout << "JSleep Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(JAMScript::Clock::now() - sleepStart - std::chrono::milliseconds(70)).count() << " us" << std::endl;
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
 //         auto q = ribScheduler.ExtractRemote<int>(jf);
//            jf.Get();
            //std::cout << jf << std::endl;
            JAMScript::ThisTask::Yield();
            printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
        }
    });
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::high_resolution_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(slotSize), 0}});
            
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf(">>...........\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
            auto q = ribScheduler.ExtractRemote<int>(jf);
            //std::cout << jf << std::endl;
            JAMScript::ThisTask::Yield();
        }
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}