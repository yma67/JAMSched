#include <jamscript.hpp>
#include <queue>

constexpr std::size_t kNumberOfCoroutine = 1000000;
constexpr std::size_t kNumberOfChild = 10;
constexpr bool kWaitInGroup = true;
constexpr bool useImmediateExecutePolicy = true;

JAMScript::StackTraits stCommon(true, 0, true, useImmediateExecutePolicy), 
                       stCommonNode(false, 4096 * 2, true, useImmediateExecutePolicy);

auto GetDurationNS(std::chrono::high_resolution_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - tp).count();
}

void CSPSkynet(JAMScript::Channel<long> &cNum, JAMScript::WaitGroup &wg, long num, long size, long div)
{
    if (size > 1)
    {
        auto sc = std::make_unique<JAMScript::Channel<long>>();
        auto swg = std::make_unique<JAMScript::WaitGroup>();
        for (long i = 0; i < div; i++)
        {
            swg->Add(1);
            long factor = size / div;
            long subNum = num + i * (factor);
            JAMScript::ThisTask::CreateBatchTask(
                (factor == 1) ? (stCommon) : (stCommonNode), JAMScript::Duration::max(),
                CSPSkynet, std::ref(*sc), std::ref(*swg), long(subNum), long(factor), long(div))
                .Detach();
        }
        if constexpr(kWaitInGroup)
        {
            swg->Wait();
            sc->close();
            std::accumulate(sc->begin(), sc->end(), 0L, std::plus<long>()) >> cNum;
            wg.Done();
        }
        else
        {
            long curr, sum = 0;
            for (int i = 0; i < div; i++)
            {
                curr << *sc;
                sum += curr;
            }
            sum >> cNum;
        }
        return;
    }
    num >> cNum;
    if constexpr(kWaitInGroup) wg.Done();
}

long FutureSkynet(long num, long size, long div)
{
    if (size > 1)
    {
        std::vector<long> l(div);
        std::vector<JAMScript::future<long>> futures;
        std::iota(l.begin(), l.end(), 0);
        std::transform(l.begin(), l.end(), std::back_inserter(futures), [num, size, div] (long i) {
            long factor = size / div;
            long subNum = num + i * (factor);
            return JAMScript::async((factor == 1) ? (stCommon) : (stCommonNode),
                                    [factor, subNum, div] {
                                        return FutureSkynet(subNum, factor, div);
                                    });
        });
        return std::accumulate(futures.begin(), futures.end(), 0L, [](auto x, auto &y) {
            return x + y.get();
        });
    }
    return num;
}

int main(int argc, char *argv[])
{
    long totalNS = 0, totalFutureNS = 0;
    std::printf("Channel + WaitGroup Version\n");
    for (int i = 0; i < 10; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst{};
        for (int i = 0; i < atoi(argv[1]); i++)
            vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 256)));
        ribScheduler.SetStealers(std::move(vst));
        ribScheduler.CreateBatchTask(stCommonNode, JAMScript::Duration::max(), [&ribScheduler, &totalNS] {
            auto tpStart = std::chrono::high_resolution_clock::now();
            auto sc = std::make_unique<JAMScript::Channel<long>>();
            auto swg = std::make_unique<JAMScript::WaitGroup>();
            CSPSkynet(std::ref(*sc), std::ref(*swg), 0, kNumberOfCoroutine, kNumberOfChild);
            long res;
            res << (*sc);
            auto elapsed = GetDurationNS(tpStart);
            totalNS += elapsed;
            std::printf("result=%ld, elapsed=%ld ms, per_fiber=%ld ns\n", 
                        res, elapsed / 1000000, elapsed / 1111111);
            ribScheduler.ShutDown();
        });
        ribScheduler.RunSchedulerMainLoop();
    }
    std::printf("avg over 10 = %ld ms\n", totalNS / 10000000);
    std::printf("Future Version\n");
    for (int i = 0; i < 10; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst{};
        for (int i = 0; i < atoi(argv[1]); i++)
            vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 256)));
        ribScheduler.SetStealers(std::move(vst));
        auto tpStart = std::chrono::high_resolution_clock::now();
        ribScheduler.CreateBatchTask(stCommonNode, JAMScript::Duration::max(), 
        [&i, tpStart, &totalFutureNS, &ribScheduler] {
            JAMScript::async([&i, tpStart, &totalFutureNS, &ribScheduler] {
                return FutureSkynet(0L, long(kNumberOfCoroutine), long(kNumberOfChild));
            }).then([tpStart](JAMScript::future<long> res) {
                long r = res.get();
                if (r == 499999500000)
                {
                    auto elapsed = GetDurationNS(tpStart);
                    std::printf("result=%ld, elapsed=%ld ms, per_fiber=%ld ns\n", 
                                r, elapsed / 1000000, elapsed / 1111111);
                    return elapsed;
                }
                return std::numeric_limits<long>::max();
            }).then([&totalFutureNS](JAMScript::future<long> dt) {
                totalFutureNS += dt.get();
                return JAMScript::unit();
            }).then([&i, &ribScheduler, &totalFutureNS](JAMScript::future<JAMScript::unit> dt) {
                dt.get();
                if (i == 9)
                {
                    std::printf("avg over 10 = %ld ms\n", totalFutureNS / 10000000);
                }
                ribScheduler.ShutDown();
                return JAMScript::unit();
            });
        });
        ribScheduler.RunSchedulerMainLoop();
    }
    return 0;
}