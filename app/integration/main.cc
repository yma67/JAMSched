#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>
#include <cstring>

int RPCFunctionJSync(int a, int b)
{
    std::cout << "Sync Add of " << a << " + " << b << std::endl;
    return a + b;
}

int RPCFunctionJAsync(int a, int b)
{
    std::cout << "Async Subtract of " << a << " - " << b << std::endl;
    return a - b;
}

auto RPCFunctionJSyncFunctor = std::function(RPCFunctionJSync);
auto RPCFunctionJSyncInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(RPCFunctionJSyncFunctor)>(RPCFunctionJSyncFunctor);
auto RPCFunctionJAsyncFunctor = std::function(RPCFunctionJAsync);
auto RPCFunctionJAsyncInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(RPCFunctionJAsyncFunctor)>(RPCFunctionJAsyncFunctor);
auto DuplicateCStringFunctor = std::function(strdup);
auto DuplicateCStringInvoker = JAMScript::RExecDetails::RoutineRemote<decltype(DuplicateCStringFunctor)>(DuplicateCStringFunctor);

std::unordered_map<std::string, JAMScript::RExecDetails::InvokerInterface *> invokerMap = {
    {std::string("RPCFunctionJSync"), &RPCFunctionJSyncInvoker},
    {std::string("RPCFunctionJAsync"), &RPCFunctionJAsyncInvoker},
    {std::string("DuplicateCString"), &DuplicateCStringInvoker}
};

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCalls(invokerMap);
    auto tuPeriod = std::chrono::milliseconds(500);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), tuPeriod, 0}},
                             {{std::chrono::milliseconds(0), tuPeriod, 0}});
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}