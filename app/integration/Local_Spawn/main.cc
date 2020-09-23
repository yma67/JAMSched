#include <jamscript.hpp>
std::atomic_int count = 0;
std::chrono::high_resolution_clock::time_point prevUS;
constexpr bool isTaskStealable = true;
JAMScript::StackTraits commonStackMode = JAMScript::StackTraits(true, 0, true);
[[clang::optnone]] int execincr(int x, float y){
  //while (true)
  {
      auto countc=count++;
if(countc % 1000000 == 0)
{
  // std::cout << "thread#%s" << std::this_thread::get_id();
  printf("-----------------Value of count %d, elapsed %ld us\n", countc, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - prevUS).count());
  prevUS = std::chrono::high_resolution_clock::now();
}
  }

  // std::this_thread::yield();
  // std::this_thread::sleep_for(std::chrono::nanoseconds(10000));
  /*auto vn = ((rand() % 10) < 3) + 1;
  for (int i = 0; i < 1; i++) 
  {
    JAMScript::ThisTask::CreateBatchTask(commonStackMode, std::chrono::milliseconds(10), execincr, x, y);
  }*/
  return 0;
}

int incr(int x, float y) {
  //  printf("Incr %d\n", x);
  //rbs->CreateLocalNamedBatchExecution<int>({false, 1024*64}, std::chrono::milliseconds(10), std::string("incr"), x, y);
  int i;
      auto s = std::chrono::high_resolution_clock::now();
  static std::atomic_uint64_t cntDelay = 0, cntNum = 0, cntcDelay = 0, cntcNum = 0;
  auto ccommonStackMode = commonStackMode;
  // ccommonStackMode.pinCore = 4;
  JAMScript::ThisTask::CreateBatchTask(ccommonStackMode, std::chrono::milliseconds(10), [s] (int ax, float ay) {
    // printf("Task Creation elapsed %ld ns\n", std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - s).count());
    cntDelay.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - s).count());
    if (cntNum++ == 1000000)
    {
      printf("delay between create and execution avg of 100k is %lu us\n", cntDelay.load() / cntNum.load() / 1000);
      cntNum = cntDelay = 0;
    }
    execincr(ax, ay); }, x, y);
    cntcDelay.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - s).count());
    if (cntcNum++ == 1000000)
    {
      printf("create in avg of 100k is %lu us\n", cntcDelay.load() / cntcNum.load() / 1000);
      cntcNum = cntcDelay = 0;
    }
      // printf("Task Creation elapsed %ld ns\n", std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - s).count());
  // JAMScript::ThisTask::Yield();
  //  printf("End... of incr...\n");
 return 0;
}

int execloop(){
  int i;
  //  printf("IN exec loop \n");
  while (1) {
    incr(10, 4.5);
    // std::this_thread::sleep_for(std::chrono::microseconds(rand() % 1000));
    if (rand() % 100 < 5) JAMScript::ThisTask::Yield();
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
    /*JAMScript::RIBScheduler ribScheduler(1024 * 4, "tcp://localhost:1883", "app-1", "dev-1");
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(1000), 0}});*/
    std::vector<std::unique_ptr<JAMScript::StealScheduler>> vst { };
    JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
    int nthiefToC = 9;
    for (int i = 0; i < nthiefToC; i++) vst.push_back(std::move(std::make_unique<JAMScript::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    user_setup();
    prevUS = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < nthiefToC; i++) 
    //ribScheduler.CreateBatchTask(JAMScript::StackTraits(true, 0, false), std::chrono::milliseconds(1000), std::function(execincr), 1, 3.4f);
    ribScheduler.CreateBatchTask(JAMScript::StackTraits(true, 0, false), std::chrono::milliseconds(1000), std::function(user_main), argc, argv);
    // baseline();
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
