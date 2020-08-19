#include <jamscript.hpp>

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        int rounds = 0;
        while (true)
        {
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(3));
            printf("==============================================round no %d\n", rounds++);
            auto res = ribScheduler.CreateRemoteExecSync(std::string("gethello"), std::string(""), 0, std::chrono::minutes(5), std::string("david"));
            std::cout << "Results .... " << res << std::endl;
        }
    });
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        int rounds = 0;
        while (true)
        {                                        
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(3));
            printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~round no %d\n", rounds++);
            auto res = ribScheduler.CreateRemoteExecSync(std::string("addNumbers"), std::string(""), 0, std::chrono::minutes(5), 45, 67);
            std::cout << "Results .... " << res << std::endl;
        }
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}