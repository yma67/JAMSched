#include <jamscript.hpp>

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
    jamc::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.RegisterLocalExecution("addNumbers", addNumbers);
    ribScheduler.RegisterLocalExecution("scaleNumber", scaleNumber);
    ribScheduler.RegisterLocalExecution("getTime", getTime);
    ribScheduler.RegisterLocalExecution("RPCFunctionJSync", RPCFunctionJSync);
    ribScheduler.RegisterLocalExecution("RPCFunctionJAsync", RPCFunctionJAsync);
    ribScheduler.RegisterLocalExecution("DuplicateCString", strdup);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                        
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            printf("==============================================\n");
            jamc::ctask::CreateRemoteExecSync(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
        }
    });
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            printf(">>...........\n");
            jamc::ctask::CreateRemoteExecSync(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}