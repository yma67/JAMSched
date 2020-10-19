#include <jamscript>

int main()
{
    for (int i = 0; i < 10000; i++)
    {
        printf("Trail No. %d\n", i);
        int countArray[2] = {0, 0};
        jamc::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
        jamc::promise<void> prShutDown;
        ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                    {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            countArray[0] = 1;
            prShutDown.set_value();
            printf("==============================================\n");
        });
                    
        ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::steady_clock::duration::max(), [&]() {
            ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                                    {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
                                    
            jamc::ctask::SleepFor(std::chrono::milliseconds(70));
            countArray[1] = 1;
            printf(">>...........\n");
            prShutDown.get_future().get();
            ribScheduler.ShutDown();
        });
        ribScheduler.RunSchedulerMainLoop();
        assert(countArray[0] > 0);
        assert(countArray[1] > 0);
    }
    
    return 0;
}