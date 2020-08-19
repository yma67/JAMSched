#include "core/task/task.hpp"
#include "time/timeout.h"
#include "scheduler/scheduler.hpp"
#include "scheduler/tasklocal.hpp"
#include "concurrency/notifier.hpp"

JAMScript::TaskInterface::TaskInterface(SchedulerBase *scheduler)
    : status(TASK_READY), isStealable(true), scheduler(scheduler), 
      notifier(std::make_shared<Notifier>()), cvStatus(0), timeOut(std::make_unique<struct timeout>()),
      id(0), taskLocalStoragePool(*GetGlobalJTLSMap()), deadline(std::chrono::microseconds(0)),
      burst(std::chrono::microseconds(0)) {}

JAMScript::TaskInterface::~TaskInterface() {}

void JAMScript::TaskInterface::ExecuteC(uint32_t tsLower, uint32_t tsHigher)
{
    TaskInterface *task = reinterpret_cast<TaskInterface *>(tsLower | ((static_cast<uint64_t>(tsHigher) << 16) << 16));
    task->Execute();
    task->status = TASK_FINISHED;
    task->notifier->Notify();
    task->SwapOut();
}

JAMScript::TaskHandle::TaskHandle(std::shared_ptr<Notifier> h)
 : n(std::move(h))
{

}

void JAMScript::TaskHandle::Join()
{
    if (n != nullptr)
    {
        n->Join();
    }
}

void JAMScript::TaskHandle::Detach()
{
    n = nullptr;
}

std::unordered_map<JAMScript::JTLSLocation, std::any> *
JAMScript::TaskInterface::GetTaskLocalStoragePool()
{
    return &taskLocalStoragePool;
}

JAMScript::TaskHandle::TaskHandle(JAMScript::TaskHandle &&other) : n(nullptr)
{
    n = other.n;
    other.n = nullptr;
}

JAMScript::TaskHandle &JAMScript::TaskHandle::operator=(JAMScript::TaskHandle &&other) 
{
    if (this != &other) 
    {
        Detach();
        n = other.n;
        other.n = nullptr;
    }
    return *this;
}

JAMScript::SchedulerBase::SchedulerBase(uint32_t sharedStackSize)
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

JAMScript::SchedulerBase::~SchedulerBase()
{
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(v_stack_id);
#endif
    delete[] sharedStackBegin;
}

JAMScript::TimePoint JAMScript::SchedulerBase::GetSchedulerStartTime() const 
{ 
    return Clock::now(); 
}

JAMScript::TimePoint JAMScript::SchedulerBase::GetCycleStartTime() const 
{
    return Clock::now(); 
}

auto JAMScript::ThisTask::ConsumeOneFromBroadcastStream(const std::string &nameSpace, const std::string &variableName)
{
    BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
    return TaskInterface::Active()->GetRIBScheduler()->ConsumeOneFromBroadcastStream(nameSpace, variableName);
}

auto JAMScript::ThisTask::ProduceOneToLoggingStream(const std::string &nameSpace, const std::string &variableName, const nlohmann::json &value)
{
    BOOST_ASSERT_MSG(TaskInterface::Active()->GetRIBScheduler() != nullptr, "must have an RIB scheduler");
    return TaskInterface::Active()->GetRIBScheduler()->ProduceOneToLoggingStream(nameSpace, variableName, value);
}

thread_local JAMScript::TaskInterface *JAMScript::TaskInterface::thisTask = nullptr;

JAMScript::TaskInterface *JAMScript::TaskInterface::Active()
{
    return thisTask;
}

void JAMScript::ThisTask::Yield()
{
    if (TaskInterface::Active()->status != TASK_FINISHED) 
    {
        TaskInterface::Active()->scheduler->Enable(TaskInterface::Active());
        TaskInterface::Active()->SwapOut();
    }
}

void JAMScript::ThisTask::Exit()
{
    auto* thisTask = TaskInterface::Active();
    thisTask->status = TASK_FINISHED;
    thisTask->notifier->Notify();
    thisTask->SwapOut();
}

JAMScript::Duration JAMScript::ThisTask::GetTimeElapsedCycle()
{
    return Clock::now() - TaskInterface::Active()->scheduler->GetCycleStartTime();
}
        
JAMScript::Duration JAMScript::ThisTask::GetTimeElapsedScheduler()
{
    return Clock::now() - TaskInterface::Active()->scheduler->GetSchedulerStartTime();
}

bool JAMScript::operator<(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst < b.burst;
    return a.deadline < b.deadline;
}

bool JAMScript::operator>(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst > b.burst;
    return a.deadline > b.deadline;
}

bool JAMScript::operator==(const TaskInterface &a, const TaskInterface &b) noexcept
{
    return a.id == b.id;
}

std::size_t JAMScript::hash_value(const TaskInterface &value) noexcept
{
    return std::size_t(value.id);
}

bool JAMScript::priority_order(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst < b.burst;
    return a.deadline < b.deadline;
}

bool JAMScript::priority_inverse_order(const TaskInterface &a, const TaskInterface &b) noexcept
{
    if (a.deadline == b.deadline)
        return a.burst > b.burst;
    return a.deadline > b.deadline;
}