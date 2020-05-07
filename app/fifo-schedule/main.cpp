#include <scheduler/task.h>
#include <iostream>
#include <queue>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <csignal>

using namespace std;

scheduler_t scheduler;
task_t* flame;
queue<task_t*> fifo;

int capsleep = 30;
int procsleep = 40;
int playbacksleep = 50;

int rounds = 0;

void capture(task_t* self, void* args);

void playback(task_t* self, void* args) {
    int* sleeptr = reinterpret_cast<int*>(args);
    cout << "audio playback";
    this_thread::sleep_for(chrono::milliseconds(*sleeptr));
    task_t* capt = reinterpret_cast<task_t*>(calloc(1, sizeof(task_t) + 256 * 1024));
    make_task(capt, &scheduler, capture, memset, &capsleep, NULL, 256 * 1024, reinterpret_cast<unsigned char*>(capt + 1));
    fifo.push(capt);
    finish_task(self, 0);
}

void process(task_t* self, void* args) {
    int* sleeptr = reinterpret_cast<int*>(args);
    cout << "audio processing";
    this_thread::sleep_for(chrono::milliseconds(*sleeptr));
    task_t* play = reinterpret_cast<task_t*>(calloc(1, sizeof(task_t) + 256 * 1024));
    make_task(play, &scheduler, playback, memset, &playbacksleep, NULL, 256 * 1024, reinterpret_cast<unsigned char*>(play + 1));
    fifo.push(play);
    finish_task(self, 0);
}

void capture(task_t* self, void* args) {
    int* sleeptr = reinterpret_cast<int*>(args);
    cout << "audio capture";
    this_thread::sleep_for(chrono::milliseconds(*sleeptr));
    task_t* processing = reinterpret_cast<task_t*>(calloc(1, sizeof(task_t) + 256 * 1024));
    make_task(processing, &scheduler, process, memset, &procsleep, NULL, 256 * 1024, reinterpret_cast<unsigned char*>(processing + 1));
    fifo.push(processing);
    finish_task(self, 0);
}

task_t* schedule_next() {
    task_t* nxt = fifo.front();
    fifo.pop();
    return nxt;
}

void idle_task() {
    
}

void before_each(task_t* self) {
    cout << "event: ";
}

void after_each(task_t* self) {
    cout << ", round#" << rounds / 3 << endl;
    if (rounds / 3 > 10) {
        shutdown_scheduler(&scheduler);
    }
    rounds++;
}

int main() {
    cout << SIG_BLOCK << endl;
    make_scheduler(&scheduler, schedule_next, idle_task, before_each, after_each, memset);
    flame = reinterpret_cast<task_t*>(calloc(1, sizeof(task_t) + 256 * 1024));
    make_task(flame, &scheduler, capture, memset, &capsleep, NULL, 256 * 1024, reinterpret_cast<unsigned char*>(flame + 1));
    fifo.push(flame);
    scheduler_mainloop(&scheduler);
    return 0;
}