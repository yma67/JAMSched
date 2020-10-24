#include "core/task/task.hpp"
#include "time/timeout.h"
#include "scheduler/scheduler.hpp"
#include "scheduler/tasklocal.hpp"
#include "concurrency/notifier.hpp"

jamc::TaskInterface::TaskInterface(SchedulerBase *scheduler)
    : status(TASK_READY), isStealable(true), scheduler(scheduler),
      notifier(std::make_shared<Notifier>()), cvStatus(0), timeOut(std::make_unique<struct timeout>()),
      id(0), taskLocalStoragePool(*GetGlobalJTLSMap()), deadline(std::chrono::microseconds(0)),
      burst(std::chrono::microseconds(0)) {}

jamc::TaskInterface::~TaskInterface() {}

void jamc::TaskInterface::ExecuteC(uint32_t tsLower, uint32_t tsHigher)
{
    CleanupPreviousTask();
    TaskInterface *task = reinterpret_cast<TaskInterface *>(tsLower | ((static_cast<uint64_t>(tsHigher) << 16) << 16));
    thisTask = task;
    task->Execute();
    task->status = TASK_FINISHED;
    task->notifier->Notify();
    task->SwapOut();
}

void jamc::TaskInterface::CleanupPreviousTask()
{
    if (thisTask != nullptr)
    {
       thisTask->GetSchedulerValue()->EndTask(thisTask);
    }
}

void jamc::TaskInterface::ResetTaskInfos()
{
    thisTask = nullptr;
}

void jamc::TaskInterface::SwapOut()
{
    auto nextTask = GetSchedulerValue()->GetNextTask();
    if (nextTask != this)
    {
        SwapTo(nextTask);
        CleanupPreviousTask();
        thisTask = this;
    }
}

jamc::TaskHandle::TaskHandle(std::shared_ptr<Notifier> h)
 : n(std::move(h))
{

}

void jamc::TaskHandle::Join()
{
    if (n != nullptr)
    {
        n->Join();
    }
}

void jamc::TaskHandle::Detach()
{
    n = nullptr;
}

std::unordered_map<jamc::JTLSLocation, std::any> *
jamc::TaskInterface::GetTaskLocalStoragePool()
{
    return &taskLocalStoragePool;
}

jamc::TaskHandle::TaskHandle(jamc::TaskHandle &&other) : n(nullptr)
{
    n = other.n;
    other.n = nullptr;
}

jamc::TaskHandle &jamc::TaskHandle::operator=(jamc::TaskHandle &&other) 
{
    if (this != &other) 
    {
        Detach();
        n = other.n;
        other.n = nullptr;
    }
    return *this;
}

jamc::SchedulerBase::SchedulerBase(uint32_t sharedStackSize)
    : sharedStackSizeActual(sharedStackSize), toContinue(true), sharedStackBegin(new uint8_t[sharedStackSize])
{
    uintptr_t u_p = (uintptr_t)(sharedStackSizeActual - (sizeof(void *) << 1) +
                                (uintptr_t) reinterpret_cast<uintptr_t>(sharedStackBegin));
    u_p = (u_p >> 4) << 4;
    sharedStackAlignedEnd = reinterpret_cast<uint8_t *>(u_p);
#ifdef __x86_64__
    sharedStackAlignedEndAct = reinterpret_cast<uint8_t *>((u_p - sizeof(void *)));
    *(reinterpret_cast<void **>(sharedStackAlignedEndAct)) = nullptr;
#elif defined(__aarch64__) || defined(__arm__)
    sharedStackAlignedEndAct = (void *)(u_p);
#else
#error "not supported"
#endif
    sharedStackSizeAligned = sharedStackSizeActual - 16 - (sizeof(void *) << 1);
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    v_stack_id =
        VALGRIND_STACK_REGISTER(sharedStackBegin, (void *)((uintptr_t)sharedStackBegin + sharedStackSizeActual));
#endif
}

jamc::SchedulerBase::~SchedulerBase()
{
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(v_stack_id);
#endif
    delete[] sharedStackBegin;
}

jamc::TimePoint jamc::SchedulerBase::GetSchedulerStartTime() const 
{ 
    return Clock::now(); 
}

jamc::TimePoint jamc::SchedulerBase::GetCycleStartTime() const 
{
    return Clock::now(); 
}

auto jamc::ctask::ConsumeOneFromBroadcastStream(const std::string &nameSpace, const std::string &variableName)
{
    BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
    return TaskInterface::Active()->GetRIBScheduler()->ConsumeOneFromBroadcastStream(nameSpace, variableName);
}

auto jamc::ctask::ProduceOneToLoggingStream(const std::string &nameSpace, const std::string &variableName, const nlohmann::json &value)
{
    BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
    return TaskInterface::Active()->GetRIBScheduler()->ProduceOneToLoggingStream(nameSpace, variableName, value);
}

thread_local jamc::TaskInterface *jamc::TaskInterface::thisTask = nullptr;

jamc::TaskInterface *jamc::TaskInterface::Active()
{
    return thisTask;
}

void jamc::ctask::Yield()
{
    auto thisTaskK = TaskInterface::Active();
    if (thisTaskK != nullptr && thisTaskK->status != TASK_FINISHED) 
    {
        thisTaskK->Enable();
        thisTaskK->SwapOut();
    }
}

void jamc::ctask::Exit()
{
    auto* thisTaskK = TaskInterface::Active();
    if (thisTaskK != nullptr)
    {
        thisTaskK->status = TASK_FINISHED;
        thisTaskK->notifier->Notify();
        thisTaskK->SwapOut();
    }
}

jamc::Duration jamc::ctask::GetTimeElapsedCycle()
{
    return Clock::now() - TaskInterface::Active()->GetSchedulerValue()->GetCycleStartTime();
}
        
jamc::Duration jamc::ctask::GetTimeElapsedScheduler()
{
    return Clock::now() - TaskInterface::Active()->GetSchedulerValue()->GetSchedulerStartTime();
}

bool jamc::operator<(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst < b.burst;
    return a.deadline < b.deadline;
}

bool jamc::operator>(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst > b.burst;
    return a.deadline > b.deadline;
}

bool jamc::operator==(const TaskInterface &a, const TaskInterface &b) noexcept
{
    return a.id == b.id;
}

std::size_t jamc::hash_value(const TaskInterface &value) noexcept
{
    return std::size_t(value.id);
}

bool jamc::priority_order(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst < b.burst;
    return a.deadline < b.deadline;
}

bool jamc::priority_inverse_order(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst > b.burst;
    return a.deadline > b.deadline;
}