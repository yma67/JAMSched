#include <future>
#include <jamscript>
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

auto addNumberInvoker = jamc::RExecDetails::RoutineRemote<decltype(addNumberFunctor)>(addNumberFunctor);
auto scaleNumberInvoker = jamc::RExecDetails::RoutineRemote<decltype(scaleNumberFunctor)>(scaleNumberFunctor);
auto getTimeInvoker = jamc::RExecDetails::RoutineRemote<decltype(getTimeFunctor)>(getTimeFunctor);

auto RPCFunctionJSyncFunctor = std::function(RPCFunctionJSync);
auto RPCFunctionJSyncInvoker = jamc::RExecDetails::RoutineRemote<decltype(RPCFunctionJSyncFunctor)>(RPCFunctionJSyncFunctor);
auto RPCFunctionJAsyncFunctor = std::function(RPCFunctionJAsync);
auto RPCFunctionJAsyncInvoker = jamc::RExecDetails::RoutineRemote<decltype(RPCFunctionJAsyncFunctor)>(RPCFunctionJAsyncFunctor);
auto DuplicateCStringFunctor = std::function(strdup);
auto DuplicateCStringInvoker = jamc::RExecDetails::RoutineRemote<decltype(DuplicateCStringFunctor)>(DuplicateCStringFunctor);

std::unordered_map<std::string, jamc::RExecDetails::RoutineInterface *> invokerMap = {

    {std::string("addNumbers"), &addNumberInvoker},
    {std::string("scaleNumber"), &scaleNumberInvoker},    
    {std::string("getTime"), &getTimeInvoker},
    
    {std::string("RPCFunctionJSync"), &RPCFunctionJSyncInvoker},
    {std::string("RPCFunctionJAsync"), &RPCFunctionJAsyncInvoker},
    {std::string("DuplicateCString"), &DuplicateCStringInvoker}
};

int main()
{
    jamc::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCalls(invokerMap);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                        
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            printf("==============================================\n");
            jamc::future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
 //           ribScheduler.ExtractRemote(&jf);
//            jf.get();
            //std::cout << jf << std::endl;
        }
    });
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            printf(">>...........\n");
            jamc::future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
//            int q = ribScheduler.ExtractRemote(&jf);
            //std::cout << jf << std::endl;
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}