#include <jamscript.hpp>


struct VeryLargeObject {
    char veryLargeDummy[1024 * 1024 * 1024];
};
VeryLargeObject* globalVLO = nullptr;
int main()
{
    jamc::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                        
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            VeryLargeObject *vlo  = new VeryLargeObject;
            globalVLO = vlo;
            // Sleep for 70ms          
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            // this statement should prevent if the coroutines are not scheduled in the same pthread
            printf("==============================================\n");
            // this statement generates execption and program will crash very soon
            auto jf = ribScheduler.CreateRemoteExecSync(std::string("hellofunc"), std::string(""), 0, std::chrono::minutes(5), 9, std::string("hello"), 0.4566, 1);
        }
    });
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            printf(">>...........\n");
            auto jf = ribScheduler.CreateRemoteExecSync(std::string("hellofunc"), std::string(""), 0, std::chrono::minutes(5), 9, std::string("hello"), 0.4566, 1);
        }
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}