#include <scheduler/task.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define CLOCK_MONOTONIC 1

struct timespec diff(struct timespec start, struct timespec end);

scheduler_t sched;
char the_only_task_[sizeof(task_t) + 256 * 1024];
task_t* the_only_task = (task_t*)the_only_task_;
int tick = 0;
unsigned long long int time_count_ctx_switch = 0;
struct timespec time1, time2;

task_t* next_task() {
    if (the_only_task->task_status == TASK_READY) {
        clock_gettime(CLOCK_MONOTONIC, &time1);
        return the_only_task;
    }
    return NULL;
}

void idle_task() {
    // printf("executing idle task\n");
    // sleep(2);
}

void before_each() {

}

void after_each() {

}

void only_task_f(task_t* self, void* args) {
    int* tickk = args;
    while ((*tickk) < 10000) {
        clock_gettime(CLOCK_MONOTONIC, &time2);
        time_count_ctx_switch += diff(time1, time2).tv_nsec;
        *tickk = *tickk + 1;
        yield_task(self, TASK_READY);
    }
    self->scheduler->cont = 0;
    finish_task(self, 0);
}


int main() {
    printf("tick is initially %d\n", tick);
    make_scheduler(&sched, next_task, idle_task, before_each, after_each, memset);
    make_task(the_only_task, &sched, only_task_f, memset, &tick, NULL, 1024 * 256);
    scheduler_mainloop(&sched);
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