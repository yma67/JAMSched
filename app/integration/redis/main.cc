#include <jamscript>

constexpr int kIter = 1000000;

int main() {
    jamc::RedisState rst;
    rst.redisServer = "127.0.0.1";
    rst.redisPort = 6379;
    std::vector<std::pair<std::string, std::string>> vec{{"app-1", "x"}, {"app-1", "y"}, 
                                                         {"app-1", "z"}, {"app-1", "u"}, 
                                                         {"app-1", "v"}};
    jamc::RIBScheduler rsc(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1", rst, vec);
    rsc.SetSchedule({{std::chrono::milliseconds(0), std::chrono::seconds(10000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::seconds(10000), 0}});
    std::atomic_int closeint = 0;
    for (int i = 0; i < vec.size(); i++)
    {
        rsc.CreateBatchTask({false, 4096 * 2, true}, std::chrono::steady_clock::duration::max(), [&vec, &rsc, &closeint, i]() {
            int j = 0;
            std::chrono::high_resolution_clock::duration d;
            auto stp = std::chrono::high_resolution_clock::now();
            while (j < (kIter + 2))
            {
                auto st = std::chrono::high_resolution_clock::now();
                rsc.ProduceOneToLoggingStream(vec[i].first, vec[i].second, 
                    { "memory leak id = " + std::to_string(i) + 
                    "-" + std::to_string(j++) + 
                    " at " + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - stp).count())});
                d += (std::chrono::high_resolution_clock::now() - st);
                // printf("%d-%d\n", i, j);
                if (j % (kIter + 1) == kIter) {
                    std::printf("elapsed us per %d = %ld\n", kIter, std::chrono::duration_cast<std::chrono::microseconds>(d).count());
                    d = std::chrono::microseconds(0);
                }
                jamc::ctask::SleepFor(std::chrono::microseconds(1));
            }
            if (++closeint == vec.size())
            {
                rsc.ShutDown();
            }
        });
    }
    auto est = std::chrono::high_resolution_clock::now();
    rsc.RunSchedulerMainLoop();
    std::printf("elapsed total us = %ld\n", std::chrono::duration_cast<std::chrono::microseconds>((std::chrono::high_resolution_clock::now() - est)).count());
    return 0;
}