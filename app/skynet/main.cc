#include <jamscript.hpp>
#include <queue>

#define NumberOfCoroutine 1000000
#define NumberOfChild 10

template <typename T, std::size_t Demand>
class SingleConsumerOneShotQueue
{
    JAMScript::ConditionVariable cv;
    JAMScript::SpinOnlyMutex m;
    std::array<T, Demand> vStore;
    std::size_t count = 0;
public:
    void Push(T t) 
    {
        std::scoped_lock sl(m);
        vStore[count++] = t;
        if (Demand <= count) cv.notify_one();
    }
    std::array<T, Demand> &PopAll()
    {
        std::unique_lock sl(m);
        while (count < Demand) cv.wait(sl);
        return vStore;
    }
};

bool useImmediateExecutePolicy = true;
JAMScript::StackTraits stCommon(true, 0, true, useImmediateExecutePolicy), stCommonNode(false, 4096 * 2, true, useImmediateExecutePolicy);
template <std::size_t N>
void skynet(SingleConsumerOneShotQueue<long, N>& cNum, long num, long size, long div)
{
    // printf("num=%ld, size=%ld\n", num, size);
    if (size == 1)
    {
        cNum.Push(num);
        return;
    }
    else
    {
        SingleConsumerOneShotQueue<long, NumberOfChild> sc;
        for (long i = 0; i < div; i++)
        {
            long factor = size / div;
            long subNum = num + i * (factor);
            if (factor == 1)
            {
                JAMScript::ThisTask::CreateBatchTask(
                stCommon, JAMScript::Duration::max(), 
                skynet<NumberOfChild>, std::ref(sc), long(subNum), long(factor), long(div)).Detach();
            }
            else
            {
                JAMScript::ThisTask::CreateBatchTask(
                stCommonNode, JAMScript::Duration::max(), 
                skynet<NumberOfChild>, std::ref(sc), long(subNum), long(factor), long(div)).Detach();
            }
        }
        auto& v = sc.PopAll();
        long sum = 0;
        for (long i = 0; i < div; i++)
        {
            sum += v[i];
        }
        cNum.Push(sum);
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
            SingleConsumerOneShotQueue<long, 1> sc;
            skynet<1>(std::ref(sc), 0, NumberOfCoroutine, NumberOfChild);
            auto res = sc.PopAll()[0];
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