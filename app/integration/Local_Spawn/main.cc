#include <jamscript.hpp>

int RPCFunctionJSync(int a, int b)
{
    std::cout << "Sync Add of " << a << " + " << b << std::endl;
    return a + b;
}

int addNumbers(int a, int b) 
{
    printf("a + b = %d\n", a + b); 
    return a + b;
}

void scaleNumber(int x) 
{
    printf("x * 150 = %d\n", x * 150); 
    // return x * 150;
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
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.RegisterLocalExecution("addNumbers", addNumbers);
    ribScheduler.RegisterLocalExecution("scaleNumber", scaleNumber);
    ribScheduler.RegisterLocalExecution("getTime", getTime);
    ribScheduler.RegisterLocalExecution("RPCFunctionJSync", RPCFunctionJSync);
    ribScheduler.RegisterLocalExecution("RPCFunctionJAsync", RPCFunctionJAsync);
    ribScheduler.RegisterLocalExecution("DuplicateCString", strdup);
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                     
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf("==============================================\n");
            auto pr = ribScheduler.CreateLocalNamedInteractiveExecution<void>({false, 1024 * 4, true}, std::chrono::milliseconds(10), std::chrono::milliseconds(50), std::string("scaleNumber"), 3);
        }
    });
                   
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                     {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});   
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf(">>...........\n");
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}