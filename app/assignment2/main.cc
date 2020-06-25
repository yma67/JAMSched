#include <concurrency/future.hpp>
#include <scheduler/scheduler.hpp>
#include <scheduler/tasklocal.hpp>
#include <remote/remote.hpp>
#include <concurrency/semaphore.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <string>
#include <cstring>
#include <any>
#include <tuple>

std::string PrintResultWithGrade(int rex, char *grade, std::string comment, nvoid_t *token)
{
    std::vector<char> tokenVerifier = {'L', 'e', 'K', 'a', 'N', 0, 'R', 'i', 'C', 'h', 'A', 'r', 'D'};
    assert(tokenVerifier == std::vector<char>(reinterpret_cast<char *>(token->data), reinterpret_cast<char *>(token->data) + token->len));
    std::cout << "Token verified, your assignment submission is successful." << std::endl;
    std::cout << "Final value of var: " << rex << std::endl;
    std::cout << "Your grade for capstone project: " << grade << std::endl;
    std::cout << "Comment: " << comment << std::endl;
    tokenVerifier[5] = ' ';
    return std::string(tokenVerifier.data());
}

auto PrintResultWithGradeFunctor = std::function(PrintResultWithGrade);
auto PrintResultWithGradeInvoker = JAMScript::RExecDetails::Invoker<decltype(PrintResultWithGradeFunctor)>(PrintResultWithGradeFunctor);
auto CompareCStringFunctor = std::function(strcmp);
auto CompareCStringInvoker = JAMScript::RExecDetails::Invoker<decltype(CompareCStringFunctor)>(CompareCStringFunctor);
auto CStringLengthFunctor = std::function(strlen);
auto CStringLengthInvoker = JAMScript::RExecDetails::Invoker<decltype(CStringLengthFunctor)>(CStringLengthFunctor);

std::unordered_map<std::string, JAMScript::RExecDetails::InvokerInterface *> invokerMap = {
    {std::string("PrintResultWithGrade"), &PrintResultWithGradeInvoker},
    {std::string("CompareCString"), &CompareCStringInvoker},
    {std::string("CStringLength"), &CStringLengthInvoker}};

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
    ribScheduler.RegisterRPCalls(invokerMap);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(10), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(10), 0}});
    std::atomic_int32_t syncVar = 0, rTotal = 0;
    int var = 0;
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::high_resolution_clock::duration::max(), [&]() {
#ifndef JAMSCRIPT_ENABLE_VALGRIND
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
#endif
        // supposed we received this Cbor
        nlohmann::json jx = {
            {"actname",
             "PrintResultWithGrade"},
            {"args",
             {var, "F", "Not using the C programming language in your project!", {'L', 'e', 'K', 'a', 'N', 0, 'R', 'i', 'C', 'h', 'A', 'r', 'D'}}}};
        std::cout << ribScheduler.CreateJSONBatchCall(jx) << std::endl;
        nlohmann::json jx1 = {
            {"actname",
             "CompareCString"},
            {"args",
             {"Muthucumaru", "Maheswaran"}}};
        std::cout << "Reference: " << (strcmp("Muthucumaru", "Maheswaran") > 0) << std::endl;
        std::cout << "Note: result is fine if they have same sign" << std::endl;
        std::cout << ribScheduler.CreateJSONBatchCall(jx1) << std::endl;
        nlohmann::json jx2 = {
            {"actname",
             "CStringLength"},
            {"args",
             {"Muthucumaru Maheswaran"}}};
        std::cout << "Reference: " << strlen("Muthucumaru Maheswaran") << std::endl;
        std::cout << ribScheduler.CreateJSONBatchCall(jx2) << std::endl;
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}