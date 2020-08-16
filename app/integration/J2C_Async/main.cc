#include <jamscript.hpp>

int funcABCD(int a, int b)
{
    std::cout << "Sync Add of " << a << " + " << b << " = " << a + b << std::endl;

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
