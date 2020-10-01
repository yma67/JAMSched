#include <jamscript.hpp>
#include <queue>

constexpr std::size_t kNumberOfCoroutine = 1000000;
constexpr std::size_t kNumberOfChild = 10;

bool useImmediateExecutePolicy = true;
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
            if (factor == 1)
            {
                JAMScript::ThisTask::CreateBatchTask(
                stCommon, JAMScript::Duration::max(), 
                skynet, std::ref(*sc), std::ref(*swg), long(subNum), long(factor), long(div)).Detach();
            }
            else
            {
                JAMScript::ThisTask::CreateBatchTask(
                stCommonNode, JAMScript::Duration::max(), 
                skynet, std::ref(*sc), std::ref(*swg), long(subNum), long(factor), long(div)).Detach();
            }
        }
        swg->Wait();
        sc->close();
        std::accumulate(sc->begin(), sc->end(), 0L, std::plus<long>()) >> cNum;
        wg.Done();
    }
}

int main(int argc, char *argv[])
{
    long totalNS = 0;
    for (int i = 0; i < 10; i++)
    {
        JAMScript::RIBScheduler ribScheduler(1024 * 256);
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}},
                                {{std::chrono::milliseconds(0), std::chrono::milliseconds(10000), 0}});
        std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst { };
        for (int i = 0; i < atoi(argv[1]); i++) vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 256)));
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
    return 0;
}