#include <jamscript.hpp>

std::atomic_int count = 0;
std::chrono::high_resolution_clock::time_point prevUS;
[[clang::optnone]] int execincr(int x, float y){
  int currtc = count++;
if(currtc % 1000000 == 0) 
{
  printf("-----------------Value of count %d, elapsed %ld us\n", currtc, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - prevUS).count());
  prevUS = std::chrono::high_resolution_clock::now();
}
  return 0;
}

int incr(int x, float y) {
    auto s = std::chrono::high_resolution_clock::now();
  JAMScript::ThisTask::CreateLocalNamedBatchExecution<int>(JAMScript::StackTraits(true, 0, true), std::chrono::milliseconds(10), std::string("incr"), x, y);
    // printf("Task Creation elapsed %ld ns\n", std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - s).count());
  //rbs->CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(10), std::function(execincr), x, y);
  //  JAMScript::ThisTask::Yield();
  //  printf("End... of incr...\n");
 return 0;
}

int execloop(){
  int i;
  //  printf("IN exec loop \n");
  while (1) {
    incr(10, 4.5);
    JAMScript::ThisTask::Yield();
    //    printf("Exec Hello %d \n", count);
  }
  return 0;
}
int loop() {
  int i;
  JAMScript::ThisTask::CreateLocalNamedBatchExecution<int>(JAMScript::StackTraits(true, 0, true), std::chrono::milliseconds(10), std::string("loop"));
  return 0;
}
int user_main(int argc, char **argv) {
  loop();
  printf("Hello\n");
  return 0;
}
#define user_setup() { \
   ribScheduler.RegisterRPCall("incr", execincr); \
   ribScheduler.RegisterLocalExecution("incr", execincr); \
   ribScheduler.RegisterRPCall("loop", execloop); \
   ribScheduler.RegisterLocalExecution("loop", execloop); \
}

[[clang::optnone]] void baseline() {
  // ... code
  for (int i = 0; i < 13000000; i++)
  {
    execincr(i, 4.5);
  }
}

int main(int argc, char **argv) {
    JAMScript::RIBScheduler ribScheduler(1024 * 4, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    user_setup();
    prevUS = std::chrono::high_resolution_clock::now();
    ribScheduler.CreateBatchTask(JAMScript::StackTraits(true, 0, false), std::chrono::milliseconds(1000), std::function(user_main), argc, argv);
    baseline();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
