#include <jamscript.hpp>

int main(int argc, char *argv[])
{
    JAMScript::Node node(argc, argv);
    JAMScript::RIBScheduler ribScheduler(1024 * 256, node.getHostAddr(), node.getAppId(), node.getDevId());
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(3));
                printf("==============================================\n");
            try {
                ribScheduler.CreateRemoteExecAsyncMultiLevel(std::string("helloj"), std::string(""), 0);
                std::cout << "Lauched the remot exec.." << std::endl;
                continue;
            } 
            catch (const std::exception &e)
            {
                printf("Timeout error...\n");
            }
        }
    });
                    
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {                                        
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(3));
            printf(">>...........\n");
            ribScheduler.CreateRemoteExecAsyncMultiLevel(std::string("xyzfunc"), std::string(""), 0, std::string("mahesh"), 234.56, 78);
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}