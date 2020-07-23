#include "concurrency/future.hpp"
#include <remote/remote.hpp>
#include <scheduler/scheduler.hpp>
#include <core/task/task.hpp>

int main()
{
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    for (int i = 0; i < 1; i++)
#else
    for (int i = 0; i < 10000; i++)
#endif
    {
        printf("Trail No. %d\n", i);
        int countArray[2] = {0, 0};
        JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
        JAMScript::Promise<void> prShutDown;
        ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
            //while (true)
            {
                ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                        {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
                countArray[0] = 1;
                prShutDown.SetValue();
                printf("==============================================\n");
                //JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
                //auto p = ribScheduler.ExtractRemote<int>(jf);
    //            jf.Get();
                //std::cout << jf << std::endl;
            }
        }).Detach();
                    
        ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
            //while (true)
            {
                ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                        {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                        
                JAMScript::ThisTask::SleepFor(std::chrono::milliseconds(70));
                countArray[1] = 1;
                printf(">>...........\n");
                prShutDown.GetFuture().Get();
                ribScheduler.ShutDown();
                //JAMScript::Future<nlohmann::json> jf = ribScheduler.CreateRemoteExecution(std::string("hellofunc"), std::string(""), 0, 9, std::string("hello"), 0.4566, 1);
    //            int q = ribScheduler.ExtractRemote(&jf);
                //std::cout << jf << std::endl;
            }
        }).Detach();
        ribScheduler.RunSchedulerMainLoop();
        assert(countArray[0] > 0);
        assert(countArray[1] > 0);
    }
    
    return 0;
}