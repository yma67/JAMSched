#include <jamscript.hpp>
JAMScript::RIBScheduler *rbs;
int count = 0;
std::chrono::high_resolution_clock::time_point prevUS;
[[clang::optnone]] int execincr(int x, float y){
  count++;
if(count % 1000000 == 0) 
{
  printf("-----------------Value of count %d, elapsed %ld us\n", count, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - prevUS).count());
  prevUS = std::chrono::high_resolution_clock::now();
}
  return 0;
}
int incr(int x, float y) {
  //  printf("Incr %d\n", x);
  //rbs->CreateLocalNamedBatchExecution<int>({false, 1024*64}, std::chrono::milliseconds(10), std::string("incr"), x, y);
  int i;
      auto s = std::chrono::high_resolution_clock::now();
  rbs->CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(10), std::function(execincr), x, y);
      // printf("Task Creation elapsed %ld ns\n", std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - s).count());
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
  //rbs->CreateLocalNamedBatchExecution<int>({false, 1024*64}, std::chrono::milliseconds(10), std::string("loop"));
  rbs->CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(10), std::function(execloop));  
  //  printf("Loop Hello");
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
int main(int argc, char **argv) {
        JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    rbs = &ribScheduler;
        ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                                {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
        user_setup();
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::milliseconds(1000), std::function(user_main), argc, argv);
    prevUS = std::chrono::high_resolution_clock::now();
        ribScheduler.RunSchedulerMainLoop();
        return 0;
  }
