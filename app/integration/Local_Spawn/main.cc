#include <jamscript.hpp>

int count = 0;
int execincr(int x, float y){
  count++;
if(count % 1000000 == 0) printf("-----------------Value of count %d\n", count);
 return 0;
}
int incr(int x, float y) {
  execincr(x, y);
  JAMScript::ThisTask::CreateLocalNamedBatchExecution<int>(JAMScript::StackTraits(false, 1024*64, false), std::chrono::milliseconds(10), std::string("incr"), int(x), float(y));
  int i;
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
  JAMScript::ThisTask::CreateLocalNamedBatchExecution<int>(JAMScript::StackTraits(false, 1024*64, false), std::chrono::milliseconds(10), std::string("loop"));
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
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});
    user_setup();
    ribScheduler.CreateBatchTask({false, 1024 * 256, false}, std::chrono::milliseconds(1000), std::function(user_main), argc, argv);
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
