#include <core/scheduler/task.h>
#include <xtask/shared-stack-task.h>
#include <future/future.h>
#include <queue>
#include <string>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define REQUIRE(x) if (!(x)) { perror(#x" not valid, return\n"); assert(0); }
using namespace std;
deque<task_t*> sched_queue;
scheduler_t scheduler_future;
string secret("Java is the best programming language in the world" 
              "which has not yet been contaminated by the business\n");
string push_back_builder("");
int pushb_index = 0;

void renablr(jamfuture_t* self) {
    sched_queue.push_back(self->owner_task);
}

void secret_completer(task_t* self, void* arg) {
    {
        char ch = *reinterpret_cast<char*>(arg);
        auto secret_getter = *reinterpret_cast<pair<jamfuture_t*, 
                                jamfuture_t*>*>(self->get_user_data(self));
        push_back_builder = push_back_builder + ch;
        double x(0.25);
        x *= double(ch);
        string push_back_builder_snapshot = push_back_builder;
        get_future(secret_getter.second);
        REQUIRE(abs(x - (0.25 * double(ch))) < 0.001);
        REQUIRE((push_back_builder_snapshot + 
                *reinterpret_cast<string*>(secret_getter.second->data)) == 
                secret);
        *reinterpret_cast<string*>(secret_getter.second->data) = ch + 
                                                *reinterpret_cast<string*>
                                                (secret_getter.second->data);
        
        if (secret_getter.first != NULL) {
            secret_getter.first->data = secret_getter.second->data;
            notify_future(secret_getter.first);
        }
    }
    finish_task(self, 0);
}

task_t* secret_scheduler_next(scheduler_t* self) {
    if (sched_queue.size() > 0) {
        task_t* to_run = sched_queue.front();
        sched_queue.pop_front();
        return to_run;
    } else {
        shutdown_scheduler(&scheduler_future);
        return NULL;
    }
}

int main() {
    printf("yaaaaa\n");
    string completed_secret("");
    char chx[secret.size()];
    pair<jamfuture_t*, jamfuture_t*> px[secret.size()];
    jamfuture_t* prev_future = NULL;
    shared_stack_t* sstack = make_shared_stack(1024 * 128, 
                                               malloc, free, memcpy);
    memset(&scheduler_future, 0, sizeof(scheduler_t));
    make_scheduler(&scheduler_future, secret_scheduler_next, 
                   empty_func_next_idle, empty_func_before_after, 
                   empty_func_before_after);
    for (int i = 0; i < secret.size(); i++) {
        chx[i] = secret[i];
        jamfuture_t* ptrf = reinterpret_cast<jamfuture_t*>
                            (calloc(sizeof(jamfuture_t), 1));
        px[i] = make_pair(prev_future, ptrf);
        task_t* ntask;
        if (i % 2 == 0) {
            ntask = reinterpret_cast<task_t*>(calloc(sizeof(task_t), 1));
            unsigned char* nstack = reinterpret_cast<unsigned char*>
                                    (calloc(1024 * 64, 1));
#ifdef JAMSCRIPT_ENABLE_VALGRIND
            ntask->v_stack_id = VALGRIND_STACK_REGISTER(
                nstack, 
                (void*)((uintptr_t)nstack + 1024 * 64)
            );
#endif 
            make_task(ntask, &scheduler_future, secret_completer, &chx[i], 
                      1024 * 64, nstack);
        } else {
            ntask = make_shared_stack_task(&scheduler_future, secret_completer,
                                           &chx[i], sstack);
        }
        ntask->set_user_data(ntask, &px[i]);
        make_future(ptrf, ntask, NULL, renablr);
        prev_future = ptrf;
        sched_queue.push_back(ntask);
    }
    prev_future->data = &completed_secret;
    notify_future(prev_future);
    scheduler_mainloop(&scheduler_future);
    cout << completed_secret << endl;
    REQUIRE(secret == completed_secret);
    for (int i = 0; i < secret.size(); i++) {
        task_t* to_free = px[i].second->owner_task;
        free(px[i].second);
        if (i % 2 == 0) {
#ifdef JAMSCRIPT_ENABLE_VALGRIND
    VALGRIND_STACK_DEREGISTER(to_free->v_stack_id);
#endif
            free(to_free->stack);
            free(to_free);
        } else {
            destroy_shared_stack_task(to_free);
        }
    }
    
    destroy_shared_stack(sstack);
    return 0;
}
