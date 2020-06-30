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
auto RPCFunctionJSyncInvoker = JAMScript::RExecDetails::Invoker<decltype(RPCFunctionJSyncFunctor)>(RPCFunctionJSyncFunctor);
auto RPCFunctionJAsyncFunctor = std::function(RPCFunctionJAsync);
auto RPCFunctionJAsyncInvoker = JAMScript::RExecDetails::Invoker<decltype(RPCFunctionJAsyncFunctor)>(RPCFunctionJAsyncFunctor);
auto DuplicateCStringFunctor = std::function(strdup);
auto DuplicateCStringInvoker = JAMScript::RExecDetails::Invoker<decltype(DuplicateCStringFunctor)>(DuplicateCStringFunctor);

std::unordered_map<std::string, JAMScript::RExecDetails::InvokerInterface *> invokerMap = {
    {std::string("RPCFunctionJSync"), &RPCFunctionJSyncInvoker},
    {std::string("RPCFunctionJAsync"), &RPCFunctionJAsyncInvoker},
    {std::string("DuplicateCString"), &DuplicateCStringInvoker}
};

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCalls(invokerMap);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::high_resolution_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(2));
        }
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}