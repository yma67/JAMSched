#include <iostream>
#include <core/scheduler/task.h>
#include <future/future.h>
#include <queue>
#include <vector>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <string>
#include <cstring>
#include <mutex>
using namespace std;

scheduler_t sched_edf;
jamfuture_t mqttval;
priority_queue<pair<int, task_t*>, vector<pair<int, task_t*>>, 
               greater<pair<int, task_t*>>> edf_pq;
deque<jamfuture_t*> io_requeue;
mutex m_io_requeue;

int epfd, fd[2];
struct epoll_event events[1];
string mqttsec("citelab loves java"), mqttsend("citelab hates C");           

task_t* edf_next(scheduler_t* self) {
    if (edf_pq.size() > 0) {
        auto ret_task  = edf_pq.top().second;
        edf_pq.pop();
        return ret_task;
    } else {
        return NULL;
    }
}

void deadline_task(task_t* self, void* args) {
    {
        if (reinterpret_cast<uintptr_t>(args) == 20) {
            mqttval.data = &mqttsend;
            {
                unique_lock<mutex> lc(m_io_requeue);
                io_requeue.push_back(&mqttval);
            }
            get_future(&mqttval);
            cout << "mqtt reply is: " << *reinterpret_cast<string*>(mqttval.data) << endl;
            shutdown_scheduler(&sched_edf);
        }
        cout << "task with ddl: " << reinterpret_cast<uintptr_t>(args) << endl;
    }
    finish_task(self, 0);
}

void edf_idle(scheduler_t* self) {
    epoll_wait(epfd, events, 1, -1);
}

void edf_postawait(jamfuture_t* self) {
    edf_pq.push({ 20, mqttval.owner_task });
}

void mqtt_func() {
    this_thread::sleep_for(std::chrono::milliseconds(2000));
    {
        unique_lock<mutex> lc(m_io_requeue);
        jamfuture_t* mqf = io_requeue.back();
        cout << "message to send is: " << *reinterpret_cast<string*>(mqf->data) << endl;
        this_thread::sleep_for(std::chrono::milliseconds(1000));
        mqf->data = &mqttsec;
        notify_future(mqf);
        write(fd[1], "os will be taught in java next year\n", strlen("os will be taught in java next year\n"));
    }
}

int main() {
    struct epoll_event ev = { .events = EPOLLIN };
    pipe(fd);
    epfd = epoll_create1(0);
    fcntl(fd[0], F_SETFL, O_NONBLOCK);
    ev.data.fd = fd[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
    int ddlist[] = { 100, 90, 8, 10, 20, 15, 5, 30, 40, 1 };
    make_scheduler(&sched_edf, edf_next, edf_idle, empty_func_before_after, 
                   empty_func_before_after);
    task_t* victim;
    for (int i = 0; i < 10; i++) {
        task_t* ntask = reinterpret_cast<task_t*>(calloc(sizeof(task_t), 1));
        unsigned char* nstack = reinterpret_cast<unsigned char*>(
                                        calloc(1024 * 64, 1)
                                );
        make_task(ntask, &sched_edf, deadline_task, 
                  reinterpret_cast<void*>(ddlist[i]), 1024 * 64, nstack);
        edf_pq.push({ ddlist[i], ntask });
        if (ddlist[i] == 20) victim = ntask;
    }
    make_future(&mqttval, victim, NULL, edf_postawait);
    thread mqtt(mqtt_func);
    scheduler_mainloop(&sched_edf);
    mqtt.join();
    return 0;
}