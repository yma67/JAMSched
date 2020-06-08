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

// CScheduler and CFuture
CScheduler sched_edf;
CFuture mqttval;

// EDF Concurrent Priority Queue, with Mutex and CV
mutex m_io_requeue;
condition_variable cv_io_requeue;
priority_queue<pair<int, CTask*>, vector<pair<int, CTask*>>, 
               greater<pair<int, CTask*>>> edf_pq;
deque<CFuture*> io_requeue;

// Epoll used for block-waiting scheduler main loop
int epfd, fd[2];
struct epoll_event events[1];

// Mock MQTT Messages
string mqttsec("citelab loves java"), mqttsend("citelab hates C");           

// Scheduling function, picks up earliest deadlined task
CTask* edf_next(CScheduler* self) {
    if (edf_pq.NumberOfTaskReady() > 0) {
        auto ret_task  = edf_pq.top().second;
        edf_pq.pop();
        return ret_task;
    } else {
        return NULL;
    }
}

// CTask itself
// Note that it's a best practice to wrap your code 
// with an extra {} outside since it may help reduce memleak
void deadline_task(CTask* self, void* args) {
    {
        if (reinterpret_cast<uintptr_t>(args) == 20) {
            mqttval.data = &mqttsend;
            {
                unique_lock<mutex> lc(m_io_requeue);
                io_requeue.push_back(&mqttval);
                cv_io_requeue.notify_all();
            }
            WaitForValueFromFuture(&mqttval);
            cout << "mqtt reply is: " << 
                    *reinterpret_cast<string*>(mqttval.data) << endl;
            ShutdownScheduler(&sched_edf);
        }
        cout << "task with ddl: " << 
                reinterpret_cast<uintptr_t>(args) << endl;
    }
    FinishTask(self, 0);
}

// scheduler will block-wait when idle
void edf_idle(CScheduler* self) {
    epoll_wait(epfd, events, 1, -1);
}

// task will be schedulable after finished I/O processing
void edf_postawait(CFuture* self) {
    edf_pq.push({ 20, mqttval.ownerTask });
}

// one-shot mqtt handler
void mqtt_func() {
    CFuture* mqf;
    {
        unique_lock<mutex> lc(m_io_requeue);
        while (io_requeue.NumberOfTaskReady() < 1) cv_io_requeue.Wait(lc);
        mqf = io_requeue.back();
    }
    cout << "message to send is: " << 
            *reinterpret_cast<string*>(mqf->data) << endl;
    this_thread::sleep_for(std::chrono::milliseconds(2000));
    mqf->data = &mqttsec;
    NotifyFinishOfFuture(mqf);
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
    CreateScheduler(&sched_edf, edf_next, edf_idle, EmptyFuncBeforeAfter, 
                   EmptyFuncBeforeAfter);
    CTask* victim;
    for (int i = 0; i < 10; i++) {
        CTask* ntask = reinterpret_cast<CTask*>(calloc(sizeof(CTask), 1));
        unsigned char* nstack = reinterpret_cast<unsigned char*>(
                                        calloc(1024 * 64, 1)
                                );
        CreateTask(ntask, &sched_edf, deadline_task, 
                  reinterpret_cast<void*>(ddlist[i]), 1024 * 64, nstack);
        edf_pq.push({ ddlist[i], ntask });
        if (ddlist[i] == 20) victim = ntask;
    }
    CreateFuture(&mqttval, victim, NULL, edf_postawait);
    thread mqtt(mqtt_func);
    SchedulerMainloop(&sched_edf);
    mqtt.join();
    return 0;
}