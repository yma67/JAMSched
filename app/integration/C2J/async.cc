#include <jamscript.hpp>

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        for (int i = 0; i < 1000; i++)
        {
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(5000));
            printf("==============================================\n");
            JAMScript::ThisTask::CreateRemoteExecAsyncAvecRappeler(
                std::string("helloj"), std::string(""), 0, [] {
                printf("Timed Out\n");
            });
        }
    });
                    
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {                                        
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(700));
            printf(">>...........\n");
            JAMScript::ThisTask::CreateRemoteExecAsyncAvecRappeler(
                std::string("xyzfunc"), std::string(""), 0, [] {
                    printf("Timed Out\n");
                }, 
                std::string("mahesh"), 234.56, 78
            );
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}