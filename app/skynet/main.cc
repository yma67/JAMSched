#include <jamscript.hpp>
#include <queue>

constexpr std::size_t kNumberOfCoroutine = 1000000;
constexpr std::size_t kNumberOfChild = 10;
constexpr bool kWaitInGroup = true;
constexpr bool useImmediateExecutePolicy = true;

JAMScript::StackTraits stCommon(true, 0, true, useImmediateExecutePolicy), stCommonNode(false, 4096 * 2, true, useImmediateExecutePolicy);

void skynet(JAMScript::Channel<long> &cNum, JAMScript::WaitGroup &wg, long num, long size, long div)
{
    // printf("num=%ld, size=%ld\n", num, size);
    if (size == 1)
    {
        num >> cNum;
        wg.Done();
        return;
    }
    else
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
                skynet, std::ref(*sc), std::ref(*swg), long(subNum), long(factor), long(div))
                .Detach();
        }
        if constexpr (kWaitInGroup)
        {
            swg->Wait();
            sc->close();
            std::accumulate(sc->begin(), sc->end(), 0L, std::plus<long>()) >> cNum;
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
        wg.Done();
    }
}

JAMScript::RIBScheduler *glRIBScheduler;

long SkynetFuture(long num, long size, long div)
{
    if (size > 1)
    {
        std::vector<long> l(div);
        std::vector<JAMScript::future<long>> futures;
        std::iota(l.begin(), l.end(), 0);
        std::transform(l.begin(), l.end(), std::back_inserter(futures), [num, size, div] (long i) {
            long factor = size / div;
            long subNum = num + i * (factor);
            return JAMScript::async(*glRIBScheduler, (factor == 1) ? (stCommon) : (stCommonNode),
                                    [factor, subNum, div] {
                                        return SkynetFuture(subNum, factor, div);
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
    printf("Channel + WaitGroup Version\n");
    for (int i = 0; i < 10; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst{};
        for (int i = 0; i < atoi(argv[1]); i++)
            vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 256)));
        ribScheduler.SetStealers(std::move(vst));
        ribScheduler.CreateBatchTask(
            stCommonNode, JAMScript::Duration::max(), [&ribScheduler, &totalNS] {
                auto tpStart = std::chrono::high_resolution_clock::now();
                auto sc = std::make_unique<JAMScript::Channel<long>>();
                auto swg = std::make_unique<JAMScript::WaitGroup>();
                skynet(std::ref(*sc), std::ref(*swg), 0, kNumberOfCoroutine, kNumberOfChild);
                long res;
                res << (*sc);
                auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - tpStart).count();
                totalNS += elapsed;
                std::cout << "result = " << res << " elapsed = " << elapsed / 1000000 << " ms per_fiber = " << elapsed / 1111111 << " ns/fiber" << std::endl;
                ribScheduler.ShutDown();
            });
        ribScheduler.RunSchedulerMainLoop();
    }
    printf("avg over 10 = %ld ms\n", totalNS / 10000000);
    printf("Future Version\n");
    for (int i = 0; i < 10; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        glRIBScheduler = &ribScheduler;
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst{};
        for (int i = 0; i < atoi(argv[1]); i++)
            vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 256)));
        ribScheduler.SetStealers(std::move(vst));
        auto tpStart = std::chrono::high_resolution_clock::now();
        JAMScript::async(*glRIBScheduler, [] {
            return SkynetFuture(0L, long(kNumberOfCoroutine), long(kNumberOfChild));
        }).then([tpStart](JAMScript::future<long> res) {
            long r = res.get();
            if (r == 499999500000)
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - tpStart).count();
                std::cout << "result = " << r << " elapsed = " << elapsed / 1000000 << " ms per_fiber = " << elapsed / 1111111 << " ns/fiber" << std::endl;
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
                printf("avg over 10 = %ld ms\n", totalFutureNS / 10000000);
            }
            ribScheduler.ShutDown();
            return JAMScript::unit();
        });
        ribScheduler.RunSchedulerMainLoop();
    }
    return 0;
}