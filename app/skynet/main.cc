#include <jamscript.hpp>
#include <queue>

template <typename T>
class SingleConsumerQueue
{
    JAMScript::ConditionVariable cv;
    JAMScript::Mutex m;
    std::vector<T> vStore;
    size_t demand = 0;
public:
    void Push(T t) 
    {
        std::scoped_lock sl(m);
        vStore.push_back(std::move(t));
        if (demand <= vStore.size()) cv.notify_one();
    }
    std::vector<T> PopN(size_t n)
    {
        std::unique_lock sl(m);
        demand = n;
        vStore.reserve(n);
        while (vStore.size() < n) cv.wait(sl);
        std::vector<T> res = std::move(vStore);
        vStore = std::vector<T>();
        return res;
    }
};

JAMScript::StackTraits stCommon(true, 0, true);
void skynet(std::shared_ptr<SingleConsumerQueue<long>> cNum, long num, long size, long div)
{
    if (size == 1)
    {
        cNum->Push(num);
        return;
    }
    else
    {
        auto sc = std::make_shared<SingleConsumerQueue<long>>();
        for (long i = 0; i < div; i++)
        {
            long factor = size / div;
            long subNum = num + i * (factor);
            JAMScript::ThisTask::CreateBatchTask(
                stCommon, JAMScript::Duration::max(), 
                skynet, sc, long(subNum), long(factor), long(div));
            JAMScript::ThisTask::Yield();
        }
        auto v = sc->PopN(div);
        long sum = 0;
        for (long i = 0; i < div; i++)
        {
            sum += v[i];
        }
        cNum->Push(sum);
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
            stCommon, JAMScript::Duration::max(), [&ribScheduler, &totalNS] {
            auto tpStart = std::chrono::high_resolution_clock::now();
            auto sc = std::make_shared<SingleConsumerQueue<long>>();
            skynet(sc, 0, 1000000, 10);
            auto res = sc->PopN(1)[0];
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