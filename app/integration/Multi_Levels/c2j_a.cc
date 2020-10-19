#include <jamscript>

int main(int argc, char *argv[])
{
    jamc::Node node(argc, argv);
    jamc::RIBScheduler ribScheduler(1024 * 256, node.getHostAddr(), node.getAppId(), node.getDevId());
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            jamc::ctask::SleepFor(std::chrono::milliseconds(3));
            printf("==============================================\n");
            jamc::ctask::CreateRemoteExecAsyncMultiLevelAvecRappeler(
                std::string("helloj"), std::string(""), 0, [] {
                std::cout << "Lauched the remot exec.." << std::endl;
            }, 
            [] (std::error_condition ec) {
                switch (ec.value())
                {
                case int(RemoteExecutionErrorCode::HeartbeatFailure):
                    printf("Heartbeat Failed\n");
                    break;
                case int(RemoteExecutionErrorCode::AckTimedOut):
                    printf("Acknoledgement Failed\n");
                    break;
                default:
                    break;
                }
            });
        }
    });
                    
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {                                        
            jamc::ctask::SleepFor(std::chrono::milliseconds(3));
            printf(">>...........\n");
            jamc::ctask::CreateRemoteExecAsyncMultiLevelAvecRappeler(
                std::string("xyzfunc"), std::string(""), 0, [] {
                    std::cout << "Lauched the remot exec.." << std::endl;
                }, 
                [] (std::error_condition ec) {
                    switch (ec.value())
                    {
                    case int(RemoteExecutionErrorCode::HeartbeatFailure):
                        printf("Heartbeat Failed\n");
                        break;
                    case int(RemoteExecutionErrorCode::AckTimedOut):
                        printf("Acknoledgement Failed\n");
                        break;
                    default:
                        break;
                    }
                },
                std::string("mahesh"), 234.56, 78
            );
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}