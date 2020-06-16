#include <core/coroutine/task.h>
#include <xtask/shared-stack-task.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

struct timespec diff(struct timespec start, struct timespec end);

CScheduler schedule;
unsigned char the_only_task_stack[256 * 1024];
CTask the_only_task, *the_only_task_x;
int tick = 0;
int xn = 1024 * 64;
unsigned long long int time_count_ctx_switch = 0;
struct timespec time1, time2;

CTask* NextTask(CScheduler* self) {
    if (the_only_task.taskStatus == TASK_READY) {
        clock_gettime(CLOCK_MONOTONIC, &time1);
        return &the_only_task;
    }
    return NULL;
}

CTask* NextTaskX(CScheduler* self) {
    if (the_only_task_x->taskStatus == TASK_READY) {
        clock_gettime(CLOCK_MONOTONIC, &time1);
        return the_only_task_x;
    }
    return NULL;
}

void IdleTask(CScheduler* self) {
    // printf("executing idle task\n");
    // sleep(2);
}

void BeforeEach(CTask* self) {

}

void AfterEach(CTask* self) {

}

void only_task_f(CTask* self, void* args) {
    char padding[xn];
    padding[rand() % xn] = 's';
    int* tickk = args;
    while ((*tickk) < 10000) {
        clock_gettime(CLOCK_MONOTONIC, &time2);
        time_count_ctx_switch += diff(time1, time2).tv_nsec;
        *tickk = *tickk + 1;
        TaskYield(self);
    }
    self->scheduler->isSchedulerContinue = 0;
    FinishTask(self, 0);
}

void* hex_aligned_alloc(size_t x) { return aligned_alloc(16, x); }

int main() {
    printf("tick is initially %d\n", tick);
    CreateScheduler(&schedule, NextTask, IdleTask, BeforeEach, AfterEach);
    CreateTask(&the_only_task, &schedule, only_task_f, &tick, 1024 * 256, the_only_task_stack);
    SchedulerMainloop(&schedule);
    printf("tick is finally %d, avg ctx switch time is %lld ns\n", tick, time_count_ctx_switch / 10000);
    CSharedStack* xstack_app = CreateSharedStack(1024 * 256, malloc, free, memcpy);
    the_only_task_x = CreateSharedStackTask(&schedule, only_task_f, &tick, xstack_app);
    CreateScheduler(&schedule, NextTaskX, IdleTask, BeforeEach, AfterEach);
    tick = 0;
    time_count_ctx_switch = 0;
    SchedulerMainloop(&schedule);
    printf("tick is finally %d, avg ctx switch time is %lld ns\n", tick, time_count_ctx_switch / 10000);
    
    return 0;
}

struct timespec diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}