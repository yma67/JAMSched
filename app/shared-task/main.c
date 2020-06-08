#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>

CSharedStack* xstack_app;
CScheduler xsched;
int coro_count = 0;


int naive_fact(int x) {
    return (x > 1) ? (naive_fact(x - 1) * x) : (1);
}

void share_fact_wrapper(CTask* self, void* args) {
    int axe[1024];
    memset(axe, '\0' + 1, sizeof(int) * 1024);
    YieldTask(self);
}

void after_xsched(CTask* self) {
    // printf("%d\n", ((xuser_data_t*)(self->userData))->__privateStackSize);
}

CTask* xstask_app_sched(CScheduler* self) {
    coro_count += 1;
    printf("%d\n", coro_count);
    return CreateSharedStackTask(&xsched, share_fact_wrapper, NULL, xstack_app);
}
//unsigned char fxs[256 * 1024];
CTask* nortask_app_sched(CScheduler* self) {
    CTask* t = malloc(sizeof(CTask));
    if (t == NULL) return t;
    unsigned char* fxs = malloc(1024 * 32 * sizeof(char));
    if (fxs == NULL) {
        free(t);
        return NULL;
    }
    coro_count += 1;
    CreateTask(t, &xsched, share_fact_wrapper, NULL, 1024 * 32, fxs);
    return t;
}

void common_xtask_idle(CScheduler* self) {
    ShutdownScheduler(&xsched);
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
    xstack_app = CreateSharedStack(1024 * 32, malloc, free, memcpy);
    printf("succsss to set limit heap, exit\n");
    CreateScheduler(&xsched, xstask_app_sched, common_xtask_idle, 
                   EmptyFuncBeforeAfter, after_xsched);
    SchedulerMainloop(&xsched);
    DestroySharedStack(xstack_app);
    return 0;
}