#include <scheduler/scheduler.hpp>

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf("==============================================\n");
            try 
            {
                auto res = ribScheduler.CreateRemoteExecSync<std::string>(std::string("gethello"), std::string(""), 0, std::string("david"));
                std::cout << "Results .... " << res << std::endl;
                continue;
            } 
            catch (const std::exception &e)
            {
                printf("Timeout error...\n");
            }
        }
    });
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {                                        
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf("~~~~~~~~~~~~~~~~~~~~~~~==========\n");
            try 
            {
                auto res = ribScheduler.CreateRemoteExecSync<int>(std::string("addNumbers"), std::string(""), 0, 45, 67);
                std::cout << "Results .... " << res << std::endl;
                continue;
            } 
            catch (const std::exception &e)
            {
                printf("Timeout error...\n");
            }
        }
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}