#include "core/task/task.hpp"
#include "scheduler/scheduler.hpp"
#include "scheduler/tasklocal.hpp"
#include "concurrency/notifier.hpp"

JAMScript::TaskInterface::TaskInterface(SchedulerBase *scheduler)
    : status(TASK_READY),
      isStealable(true),
      scheduler(scheduler),
      baseScheduler(scheduler),
      references(0),
      notifier(std::make_shared<Notifier>(TaskInterface::Active())),
      id(0),
      taskLocalStoragePool(*GetGlobalJTLSMap()),
      deadline(std::chrono::microseconds(0)),
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

void JAMScript::TaskHandle::Join()
{
    n->Join();
}

void JAMScript::TaskHandle::Detach()
{
    n->ownerTask = nullptr;
}

std::unordered_map<JAMScript::JTLSLocation, std::any> *
JAMScript::TaskInterface::GetTaskLocalStoragePool()
{
    return &taskLocalStoragePool;
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