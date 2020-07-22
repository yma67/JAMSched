#include "concurrency/future.hpp"
#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                    {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
                printf("==============================================\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
            //auto p = ribScheduler.ExtractRemote<int>(jf);
            // jf.Get();
        }
    });
                    
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                    {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                        
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
            printf(">>...........\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
            //            int q = ribScheduler.ExtractRemote(&jf);
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}