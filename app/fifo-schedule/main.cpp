#include <core/scheduler/task.h>
#include <iostream>
#include <queue>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <csignal>

using namespace std;

CScheduler scheduler;
CTask* flame;
queue<CTask*> fifo;

int capsleep = 30;
int procsleep = 40;
int playbacksleep = 50;

int rounds = 0;

void capture(CTask* self, void* args);

void playback(CTask* self, void* args) {
    int* sleeptr = reinterpret_cast<int*>(args);
    cout << "audio playback";
    this_thread::sleep_for(chrono::milliseconds(*sleeptr));
    CTask* capt = reinterpret_cast<CTask*>(calloc(1, 
                    sizeof(CTask) + 256 * 1024));
    CreateTask(capt, &scheduler, capture, &capsleep, 256 * 1024, 
              reinterpret_cast<unsigned char*>(capt + 1));
    fifo.push(capt);
    FinishTask(self, 0);
}

void process(CTask* self, void* args) {
    int* sleeptr = reinterpret_cast<int*>(args);
    cout << "audio processing";
    this_thread::sleep_for(chrono::milliseconds(*sleeptr));
    CTask* play = reinterpret_cast<CTask*>(calloc(1, 
                    sizeof(CTask) + 256 * 1024));
    CreateTask(play, &scheduler, playback, &playbacksleep, 256 * 1024, 
              reinterpret_cast<unsigned char*>(play + 1));
    fifo.push(play);
    FinishTask(self, 0);
}

void capture(CTask* self, void* args) {
    int* sleeptr = reinterpret_cast<int*>(args);
    cout << "audio capture";
    this_thread::sleep_for(chrono::milliseconds(*sleeptr));
    CTask* processing = reinterpret_cast<CTask*>(calloc(1, 
                            sizeof(CTask) + 256 * 1024));
    CreateTask(processing, &scheduler, process, &procsleep, 256 * 1024,
              reinterpret_cast<unsigned char*>(processing + 1));
    fifo.push(processing);
    FinishTask(self, 0);
}

CTask* schedule_next(CScheduler* self) {
    CTask* nxt = fifo.front();
    fifo.pop();
    return nxt;
}

void IdleTask(CScheduler* self) {
    
}

void BeforeEach(CTask* self) {
    cout << "event: ";
}

void AfterEach(CTask* self) {
    cout << ", round#" << rounds / 3 << endl;
    if (rounds / 3 > 10) {
        ShutdownScheduler(&scheduler);
    }
    rounds++;
}

int main() {
    cout << SIG_BLOCK << endl;
    CreateScheduler(&scheduler, schedule_next, IdleTask, BeforeEach, 
                   AfterEach);
    flame = reinterpret_cast<CTask*>(calloc(1, sizeof(CTask) + 256 * 1024));
    CreateTask(flame, &scheduler, capture, &capsleep, 256 * 1024, 
              reinterpret_cast<unsigned char*>(flame + 1));
    fifo.push(flame);
    SchedulerMainloop(&scheduler);
    return 0;
}