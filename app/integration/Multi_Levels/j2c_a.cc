#include <jamscript.hpp>

const int kReqCount = 1000;
std::atomic<int> reqCount = 0;
std::once_flag onceInitTime;
std::chrono::high_resolution_clock::time_point reqStart;
int funcABCD(int a, int b)
{
    std::call_once(onceInitTime, [] { reqStart = std::chrono::high_resolution_clock::now(); reqCount = 0; }); 
    if (reqCount++ == kReqCount)
    {
        auto nw = std::chrono::high_resolution_clock::now();
        printf("time elapsed per %d reqs - %lu\n", kReqCount, std::chrono::duration_cast<std::chrono::microseconds>(nw - reqStart).count());
        reqStart = nw;
        reqCount = 0;
    }
    // printf("%d\n", reqCount.load());
    // std::cout << "Sync Add of " << a << " + " << b << " = " << a + b << std::endl;
    // usleep(1000000);
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    return a + b;
}

int addNumbers(int a, int b) 
{
    printf("a + b = %d\n", a + b); 
    //std::cout << "Add NSync Add of " << a << " + " << b << std::endl;
    return a + b;
}

void testloop() 
{
    printf("Testing loop\n");
    //std::cout << "Add NSync Add of " << a << " + " << b << std::endl;
}

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterRPCall("addNumbers", addNumbers);
    ribScheduler.RegisterRPCall("testloop", testloop);
    ribScheduler.RegisterRPCall("funcABCD", funcABCD);    

    ribScheduler.RegisterRPCall("funcABCDMin", [] (int a, int b) -> int {
        std::cout << "Async Subtract of " << a << " - " << b << std::endl;
        return a - b;
    });

    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
