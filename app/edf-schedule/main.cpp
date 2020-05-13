#include <iostream>
#include <queue>
#include <vector>
#include <thread>
#include <string>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <core/scheduler/task.h>
#include <future/future.h>

using namespace std;

// Scheduler and Future
scheduler_t sched_edf;
jamfuture_t mqttval;

// EDF Concurrent Priority Queue, with Mutex and CV
mutex m_io_requeue;
condition_variable cv_io_requeue;
priority_queue<pair<int, task_t*>, vector<pair<int, task_t*>>, 
               greater<pair<int, task_t*>>> edf_pq;
deque<jamfuture_t*> io_requeue;

// Epoll used for block-waiting scheduler main loop
int epfd, fd[2];
struct epoll_event events[1];

// Mock MQTT Messages
string mqttsec("citelab loves java"), mqttsend("citelab hates C");           

// Scheduling function, picks up earliest deadlined task
task_t* edf_next(scheduler_t* self) {
    if (edf_pq.size() > 0) {
        auto ret_task  = edf_pq.top().second;
        edf_pq.pop();
        return ret_task;
    } else {
        return NULL;
    }
}

// Task itself
// Note that it's a best practice to wrap your code 
// with an extra {} outside since it may help reduce memleak
void deadline_task(task_t* self, void* args) {
    {
        if (reinterpret_cast<uintptr_t>(args) == 20) {
            mqttval.data = &mqttsend;
            {
                unique_lock<mutex> lc(m_io_requeue);
                io_requeue.push_back(&mqttval);
                cv_io_requeue.notify_all();
            }
            get_future(&mqttval);
            cout << "mqtt reply is: " << 
                    *reinterpret_cast<string*>(mqttval.data) << endl;
            shutdown_scheduler(&sched_edf);
        }
        cout << "task with ddl: " << 
                reinterpret_cast<uintptr_t>(args) << endl;
    }
    finish_task(self, 0);
}

// scheduler will block-wait when idle
void edf_idle(scheduler_t* self) {
    epoll_wait(epfd, events, 1, -1);
}

// task will be schedulable after finished I/O processing
void edf_postawait(jamfuture_t* self) {
    edf_pq.push({ 20, mqttval.owner_task });
}

// one-shot mqtt handler
void mqtt_func() {
    jamfuture_t* mqf;
    {
        unique_lock<mutex> lc(m_io_requeue);
        while (io_requeue.size() < 1) cv_io_requeue.wait(lc);
        mqf = io_requeue.back();
    }
    cout << "message to send is: " << 
            *reinterpret_cast<string*>(mqf->data) << endl;
    this_thread::sleep_for(std::chrono::milliseconds(2000));
    mqf->data = &mqttsec;
    notify_future(mqf);
    if (write(fd[1], "os will be taught in java next year\n", 
        strlen("os will be taught in java next year\n")) < 0)
        exit(1);
}

int main(int argc, char* argv[]) {
    struct epoll_event ev = { .events = EPOLLIN };
    if (pipe(fd) < 0) return 1;
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