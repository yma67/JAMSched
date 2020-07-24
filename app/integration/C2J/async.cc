#include "concurrency/future.hpp"
#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                            {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
        while (true)
        {
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(700));
                printf("==============================================\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecAsync(std::string("hellofunc"), std::string(""), 0, std::string("abc?"));
            //auto p = ribScheduler.ExtractRemote<int>(jf);
            // jf.Get();

            try 
            {
                auto ack = jf.GetFor(std::chrono::milliseconds(100));
                std::cout << ack << std::endl;
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
            JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(700));
            printf(">>...........\n");
            JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecAsync(std::string("hellofunc"), std::string(""), 0, std::string("xyz"));
            //            int q = ribScheduler.ExtractRemote(&jf);
        }
    });

    ribScheduler.RunSchedulerMainLoop();
    return 0;
}