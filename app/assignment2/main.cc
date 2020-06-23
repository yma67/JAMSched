#include <concurrency/future.hpp>
#include <scheduler/scheduler.hpp>
#include <scheduler/tasklocal.hpp>
#include <concurrency/semaphore.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

struct ReaderWriterReadPriortized
{
private:
    int read_count, write_count;
    JAMScript::SpinMutex lock;
    JAMScript::ConditionVariable read_queue, write_queue;

public:
    ReaderWriterReadPriortized() : read_count(0), write_count(0) {}

    void ReadLock()
    {
        std::unique_lock<JAMScript::SpinMutex> lc(lock);
        read_count += 1;
        read_queue.wait(lc, [this]() -> bool {
            return !(write_count > 0);
        });
        read_queue.notify_one();
    }

    void ReadUnlock()
    {
        std::unique_lock<JAMScript::SpinMutex> lc(lock);
        read_count -= 1;
        if (read_count == 0)
            write_queue.notify_one();
    }

    void WriteLock()
    {
        std::unique_lock<JAMScript::SpinMutex> lc(lock);
        write_queue.wait(lc, [this]() -> bool {
            return !(read_count > 0 || write_count > 0);
        });
        write_count += 1;
    }

    void WriteUnlock()
    {
        std::unique_lock<JAMScript::SpinMutex> lc(lock);
        write_count -= 1;
        if (read_count > 0)
            read_queue.notify_one();
        else
            write_queue.notify_one();
    }
};

struct ReaderWriterFair
{
private:
    JAMScript::Semaphore qMutex, rwMutex, cMutex;
    long rCount;

public:
    ReaderWriterFair() : rCount(0) {}

    void ReadLock()
    {
        qMutex.Wait();
        cMutex.Wait();
        rCount++;
        if (rCount == 1)
        {
            rwMutex.Wait();
        }
        cMutex.Signal();
        qMutex.Signal();
    }

    void ReadUnlock()
    {
        cMutex.Wait();
        rCount--;
        if (rCount == 0)
        {
            rwMutex.Signal();
        }
        cMutex.Signal();
    }

    void WriteLock()
    {
        qMutex.Wait();
        rwMutex.Wait();
        qMutex.Signal();
    }

    void WriteUnlock()
    {
        rwMutex.Signal();
    }
};

using ReadWriteLock = ReaderWriterReadPriortized;

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256, 5);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    std::atomic_int32_t syncVar = 0, rTotal = 0;
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::high_resolution_clock::duration::max(), [&]() {
        int var = 0;
        ReadWriteLock rw;
        std::vector<JAMScript::TaskHandle> rpool, wpool;
        for (int i = 0; i < 500; i++)
        {
            rpool.push_back(ribScheduler.CreateBatchTask(
                {true, 0, true}, std::chrono::high_resolution_clock::duration::max(), [&](int ntry) {
                    for (int i = 0; i < ntry; i++)
                    {
                        rw.ReadLock();
                        std::cout << "Read" << std::endl;
                        // JAMScript::ThisTask::SleepFor(std::chrono::microseconds(rand() % 1000));
                        std::cout << rTotal++ << std::endl;
                        std::cout << var << std::endl;
                        rw.ReadUnlock();
                    }
                },
                60));
        }
        for (int i = 0; i < 10; i++)
        {
            wpool.push_back(ribScheduler.CreateInteractiveTask(
                {true, 0, true}, std::chrono::high_resolution_clock::duration::max(), std::chrono::high_resolution_clock::duration::min(), []() {}, [&](int ntry) {
                    for (int i = 0; i < ntry; i++)
                    {
                        rw.WriteLock();
                        syncVar++;
                        assert(syncVar <= 1);
                        std::cout << "Write" << std::endl;
                        //JAMScript::ThisTask::SleepFor(std::chrono::microseconds(rand() % 1000));
                        var += 10;
                        syncVar--;
                        rw.WriteUnlock();
                    } },
                30));
        }
        for (auto &x : rpool)
            x.Join();
        for (auto &x : wpool)
            x.Join();
        std::cout << "final value of var is: " << var << std::endl;
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}