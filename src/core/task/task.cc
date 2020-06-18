#include "core/task/task.h"
#include "concurrency/notifier.h"
#include "scheduler/scheduler.h"
#include "scheduler/tasklocal.h"

JAMScript::TaskInterface::TaskInterface(SchedulerBase* scheduler)
: status(TASK_READY),
    isStealable(true),
    scheduler(scheduler),
    baseScheduler(scheduler),
    references(0),
    notifier(ThisTask::Active()),
    id(0),
    taskLocalStoragePool(*GetGlobalJTLSMap()),
    deadline(std::chrono::microseconds(0)),
    burst(std::chrono::microseconds(0)) {}

JAMScript::TaskInterface::~TaskInterface() {}

void JAMScript::TaskInterface::ExecuteC(uint32_t tsLower, uint32_t tsHigher) {
    TaskInterface* task = reinterpret_cast<TaskInterface*>(tsLower | ((static_cast<uint64_t>(tsHigher) << 16) << 16));
    task->Execute();
    task->status = TASK_FINISHED;
    task->notifier.Notify();
    task->SwapOut();
}

void JAMScript::TaskInterface::Join() {
    notifier.Join();
}

void JAMScript::TaskInterface::Detach() {
    notifier.ownerTask = nullptr;
}

std::unordered_map<JAMScript::JTLSLocation, std::any>* 
JAMScript::TaskInterface::GetTaskLocalStoragePool() { 
    return &taskLocalStoragePool; 
}

JAMScript::SchedulerBase::SchedulerBase(uint32_t sharedStackSize)
    : sharedStackSizeActual(sharedStackSize), toContinue(true), sharedStackBegin(new uint8_t[sharedStackSize]) {
    uintptr_t u_p = (uintptr_t)(sharedStackSizeActual - (sizeof(void*) << 1) +
                                (uintptr_t) reinterpret_cast<uintptr_t>(sharedStackBegin));
    u_p = (u_p >> 4) << 4;
    sharedStackAlignedEnd = reinterpret_cast<uint8_t*>(u_p);
#ifdef __x86_64__
    sharedStackAlignedEndAct = reinterpret_cast<uint8_t*>((u_p - sizeof(void*)));
    *(reinterpret_cast<void**>(sharedStackAlignedEndAct)) = nullptr;
#elif defined(__aarch64__) || defined(__arm__)
    sharedStackAlignedEndAct = (void*)(u_p);
#else
#error "not supported"
#endif
    sharedStackSizeAligned = sharedStackSizeActual - 16 - (sizeof(void*) << 1);
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    v_stack_id =
        VALGRIND_STACK_REGISTER(sharedStackBegin, (void*)((uintptr_t)sharedStackBegin + sharedStackSizeActual));
#endif
}

JAMScript::SchedulerBase::~SchedulerBase() {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(v_stack_id);
#endif
    delete[] sharedStackBegin;
}

thread_local JAMScript::TaskInterface* JAMScript::ThisTask::thisTask = nullptr;

JAMScript::TaskInterface* JAMScript::ThisTask::Active() { 
    return thisTask; 
}

void JAMScript::ThisTask::SleepFor(Duration dt) {
    static_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutFor(thisTask, dt);
}

void JAMScript::ThisTask::SleepUntil(TimePoint tp) {
    static_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutUntil(thisTask, tp);
}

void JAMScript::ThisTask::SleepFor(Duration dt, std::unique_lock<SpinLock>& lk, Notifier* f) {
    static_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutFor(thisTask, dt, lk, f);
}

void JAMScript::ThisTask::SleepUntil(TimePoint tp, std::unique_lock<SpinLock>& lk, Notifier* f) {
    static_cast<RIBScheduler*>(thisTask->baseScheduler)->timer.SetTimeoutUntil(thisTask, tp, lk, f);
}

void JAMScript::ThisTask::Yield() {
    thisTask->scheduler->Enable(thisTask);
    thisTask->SwapOut();
}

bool JAMScript::operator<(const TaskInterface& a, const TaskInterface& b) noexcept {
    if (a.deadline == b.deadline)
        return a.burst < b.burst;
    return a.deadline < b.deadline;
}

bool JAMScript::operator>(const TaskInterface& a, const TaskInterface& b) noexcept {
    if (a.deadline == b.deadline)
        return a.burst > b.burst;
    return a.deadline > b.deadline;
}

bool JAMScript::operator==(const TaskInterface& a, const TaskInterface& b) noexcept { 
    return a.id == b.id; 
}

std::size_t JAMScript::hash_value(const TaskInterface& value) noexcept { 
    return std::size_t(value.id); 
}

bool JAMScript::priority_order(const TaskInterface& a, const TaskInterface& b) noexcept {
    if (a.deadline == b.deadline)
        return a.burst < b.burst;
    return a.deadline < b.deadline;
}

bool JAMScript::priority_inverse_order(const TaskInterface& a, const TaskInterface& b) noexcept {
    if (a.deadline == b.deadline)
        return a.burst > b.burst;
    return a.deadline > b.deadline;
}