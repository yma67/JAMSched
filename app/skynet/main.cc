#include <jamscript>
#include <functional>
#include <queue>

constexpr std::size_t kNumberOfCoroutine = 1000000;
constexpr std::size_t kNumberOfChild = 10;
constexpr bool kWaitInGroup = true;
constexpr jamc::StackTraits stCommonNode(false, 4096 * 2, true), stCommon(true, 0, true);

inline long GetDurationNS(std::chrono::high_resolution_clock::time_point tp) 
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - tp).count();
}

void GetSkynetWithCSP(jamc::Channel<long> &cNum, jamc::WaitGroup &wg, long num, long size, long div)
{
    if (size > 1)
    {
        auto sc = std::make_unique<jamc::Channel<long>>();
        auto swg = std::make_unique<jamc::WaitGroup>();
        for (long i = 0; i < div; i++)
        {
            swg->Add(1);
            long factor = size / div;
            long subNum = num + i * (factor);
            jamc::ctask::CreateBatchTask(
                (factor == 1) ? stCommon : stCommonNode, jamc::Duration::max(),
                GetSkynetWithCSP, std::ref(*sc), std::ref(*swg),
                long(subNum), long(factor), long(div))
                .Detach();
        }
        if constexpr(kWaitInGroup)
        {
            swg->Wait();
            sc->close();
            std::accumulate(sc->begin(), sc->end(), 0L, std::plus<>()) >> cNum;
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
            sc->close();
            sum >> cNum;
        }
        return;
    }
    num >> cNum;
    if constexpr(kWaitInGroup) wg.Done();
}

template <typename T>
inline auto Generate(T base, std::size_t sz)
{
    std::vector<T> r(sz);
    std::iota(r.begin(), r.end(), base);
    return r;
}

template <typename R, typename Fn>
inline auto Map(Fn&& t, std::vector<R>&& r)
{
    std::vector<typename std::result_of<Fn(R)>::type> rt;
    std::transform(r.begin(), r.end(), std::back_inserter(rt), t);
    return rt;
} 

template <typename R, typename T, typename Fn>
inline auto FoldLeft(Fn&& t, T s, std::vector<R>&& r)
{
    return std::accumulate(r.begin(), r.end(), s, t);
}

auto GetSkynetWithAsync(long num, long size, long div) -> long
{
    return (size == 1) ? (num) : 
        FoldLeft([](auto x, auto &y) { return x + y.get(); }, 0L, 
        Map([num, size, div] (long i) {
            return jamc::async(
                (size / div == 1) ? stCommon : stCommonNode,
                [num, size, div, i] {
                return GetSkynetWithAsync(
                    num + i * (size / div), 
                    size / div, div
                );
            });
        }, 
        Generate(0L, div))
    );
}

int main(int argc, char *argv[])
{
    long totalNS = 0, totalFutureNS = 0;
    std::printf("Channel + WaitGroup Version\n");
    for (int i = 0; i < 10; i++)
    {
        jamc::RIBScheduler ribScheduler(1024 * 256);
        std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
        for (int i = 0; i < std::atoi(argv[1]); i++)
            vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
        ribScheduler.SetStealers(std::move(vst));
        ribScheduler.CreateBatchTask(stCommonNode, jamc::Duration::max(), [&ribScheduler, &totalNS] {
            auto tpStart = std::chrono::high_resolution_clock::now();
            auto sc = std::make_unique<jamc::Channel<long>>();
            auto swg = std::make_unique<jamc::WaitGroup>();
            GetSkynetWithCSP(std::ref(*sc), std::ref(*swg), 0, kNumberOfCoroutine, kNumberOfChild);
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
        jamc::RIBScheduler ribScheduler(1024 * 256);
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                 {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
        for (int i = 0; i < std::atoi(argv[1]); i++)
            vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
        ribScheduler.SetStealers(std::move(vst));
        auto tpStart = std::chrono::high_resolution_clock::now();
        ribScheduler.CreateBatchTask(stCommonNode, jamc::Duration::max(),
        [&i, tpStart, &totalFutureNS, &ribScheduler] {
            jamc::async(stCommonNode, [] {
                return GetSkynetWithAsync(0L, long(kNumberOfCoroutine), long(kNumberOfChild));
            }).then([tpStart](jamc::future<long> res) {
                long r = res.get();
                if (r == 499999500000)
                {
                    auto elapsed = GetDurationNS(tpStart);
                    std::printf("result=%ld, elapsed=%ld ms, per_fiber=%ld ns\n", 
                                r, elapsed / 1000000, elapsed / 1111111);
                    return elapsed;
                }
                return std::numeric_limits<long>::max();
            }).then([&totalFutureNS](jamc::future<long> dt) {
                totalFutureNS += dt.get();
                return jamc::unit();
            }).then([&i, &ribScheduler, &totalFutureNS](jamc::future<jamc::unit> dt) {
                dt.get();
                if (i == 9)
                {
                    std::printf("avg over 10 = %ld ms\n", totalFutureNS / 10000000);
                }
                ribScheduler.ShutDown();
                return jamc::unit();
            });
        });
        ribScheduler.RunSchedulerMainLoop();
    }
    return 0;
}
