#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>

shared_stack_t* xstack_app;
scheduler_t xsched;
int coro_count = 0;


int naive_fact(int x) {
    return (x > 1) ? (naive_fact(x - 1) * x) : (1);
}

void share_fact_wrapper(task_t* self, void* args) {
    int axe[1024];
    memset(axe, '\0' + 1, sizeof(int) * 1024);
    self->yield_task(self);
}
void after_xsched(task_t* self) {
    // printf("%d\n", ((xuser_data_t*)(self->user_data))->__private_stack_size);
}
task_t* xstask_app_sched(scheduler_t* self) {
    coro_count += 1;
    printf("%d\n", coro_count);
    return make_shared_stack_task(&xsched, share_fact_wrapper, NULL, xstack_app);
}
//unsigned char fxs[256 * 1024];
task_t* nortask_app_sched(scheduler_t* self) {
    task_t* t = malloc(sizeof(task_t*));
    if (t == NULL) return t;
    unsigned char* fxs = malloc(1024 * 32 * sizeof(char));
    if (fxs == NULL) {
        free(t);
        return NULL;
    }
    coro_count += 1;
    make_task(t, &xsched, share_fact_wrapper, NULL, 1024 * 32, fxs);
    return t;
}

void common_xtask_idle(scheduler_t* self) {
    shutdown_scheduler(&xsched);
    printf("%d\n", coro_count);
}

int main() {
    struct rlimit hlmt;
    if (getrlimit(RLIMIT_AS, &hlmt)) {
        printf("fail to get limit heap, exit\n");
        return 0;
    }
    printf("succsss to get limit heap, exit\n");
    hlmt.rlim_cur = 1024 * 1024 * 1024;
    hlmt.rlim_max = 1024 * 1024 * 1024;
    if (setrlimit(RLIMIT_AS, &hlmt)) {
        printf("fail to set limit heap, exit\n");
        return 0;
    }
    printf("succsss to set limit heap, exit\n");
    xstack_app = make_shared_stack(1024 * 32, malloc, free, memcpy);
    printf("succsss to set limit heap, exit\n");
    make_scheduler(&xsched, xstask_app_sched, common_xtask_idle, 
                   empty_func_before_after, after_xsched);
    scheduler_mainloop(&xsched);
    destroy_shared_stack(xstack_app);
    return 0;
}