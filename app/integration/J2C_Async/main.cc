#include <future>
#include "concurrency/future.hpp"
#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>
#include <cstring>
#include <nlohmann/json.hpp>
#include <boost/compute/detail/lru_cache.hpp>

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

int main()
{
    auto lc = new boost::compute::detail::lru_cache<std::string, std::string>(100);

    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCall("addNumbers", addNumbers);
    ribScheduler.RegisterRPCall("RPCFunctionJAsync", [] (int a, int b) -> int {
        std::cout << "Async Subtract of " << a << " - " << b << std::endl;
        return a - b;
    });

    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}