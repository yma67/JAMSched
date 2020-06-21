#include <scheduler/scheduler.hpp>
#include <scheduler/tasklocal.hpp>
#include <concurrency/future.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

struct ReaderWriterReadPriortized
{
private:
    int read_count, write_count;
    JAMScript::SpinLock lock;
    JAMScript::ConditionVariable read_queue, write_queue;

public:
    ReaderWriterReadPriortized() : read_count(0), write_count(0) {}

    void ReadLock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        read_count += 1;
        read_queue.wait(lc, [this]() -> bool {
            return !(write_count > 0);
        });
        read_queue.notify_one();
    }

    void ReadUnlock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        read_count -= 1;
        if (read_count == 0)
            write_queue.notify_one();
    }

    void WriteLock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        write_queue.wait(lc, [this]() -> bool {
            return !(read_count > 0 || write_count > 0);
        });
        write_count += 1;
    }

    void WriteUnlock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        write_count -= 1;
        if (read_count > 0)
            read_queue.notify_one();
        else
            write_queue.notify_one();
    }
};

struct ReaderWriterWritePriortized
{
private:
    int read_count, write_count, read_wait, write_wait;
    JAMScript::SpinLock lock;
    JAMScript::ConditionVariable read_queue, write_queue;

public:
    ReaderWriterWritePriortized() : read_count(0), write_count(0), read_wait(0), write_wait(0) {}

    void ReadLock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        read_queue.wait(lc, [this]() -> bool {
            return !(write_count > 0 || write_wait > 0);
        });
        read_count += 1;
        read_queue.notify_one();
    }

    void ReadUnlock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        read_count -= 1;
        if (read_count == 0)
            write_queue.notify_one();
    }

    void WriteLock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        write_wait += 1;
        write_queue.wait(lc, [this]() -> bool {
            return !(read_count > 0 || write_count > 0);
        });
        write_wait -= 1;
        write_count += 1;
    }

    void WriteUnlock()
    {
        std::unique_lock<JAMScript::SpinLock> lc(lock);
        write_count -= 1;
        if (write_count > 0 || write_wait > 0)
            write_queue.notify_one();
        else
            read_queue.notify_one();
    }
};

using ReadWriteLock = ReaderWriterReadPriortized;

int main()
{
    JAMScript::RIBScheduler ribScheduler(1024 * 256);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    std::atomic_int32_t syncVar = 0, rTotal;
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::high_resolution_clock::duration::max(), [&]() {
        int var = 0;
        ReadWriteLock rw;
        std::vector<JAMScript::TaskInterface *> rpool, wpool;
        for (int i = 0; i < 500; i++)
        {
            rpool.push_back(ribScheduler.CreateBatchTask(
                {false, 1024 * 32, true}, std::chrono::high_resolution_clock::duration::max(), [&](int ntry) {
                    for (int i = 0; i < ntry; i++)
                    {
                        rw.ReadLock();
                        syncVar++;
                        std::cout << "Read" << std::endl;
                        JAMScript::ThisTask::SleepFor(std::chrono::microseconds(rand() % 1000));
                        std::cout << var << std::endl;
                        syncVar--;
                        rw.ReadUnlock();
                    }
                },
                60));
        }
        for (int i = 0; i < 10; i++)
        {
            wpool.push_back(ribScheduler.CreateBatchTask(
                {false, 1024 * 32, true}, std::chrono::high_resolution_clock::duration::max(), [&](int ntry) {
                    for (int i = 0; i < ntry; i++)
                    {
                        rw.WriteLock();
                        syncVar++;
                        assert(syncVar <= 1);
                        std::cout << "Write" << std::endl;
                        JAMScript::ThisTask::SleepFor(std::chrono::microseconds(rand() % 1000));
                        var += 10;
                        syncVar--;
                        rw.WriteUnlock();
                    }
                },
                30));
        }
        for (JAMScript::TaskInterface *interf : rpool)
        {
            interf->Join();
        }
        for (JAMScript::TaskInterface *interf : wpool)
        {
            interf->Join();
        }
        std::cout << "final value of var is: " << var << std::endl;
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}