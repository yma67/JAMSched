# JAMSched [![Build Status](https://travis-ci.com/yma67/JAMSched.svg?token=WDPhpGnigsQCoWp5WMJt&branch=master)](https://travis-ci.com/yma67/JAMSched)
#### Real Time Scheduler + Parallel Coroutine Engine for [JAMScript](https://github.com/citelab/JAMScript) C (Device) Side, with support of Remote Procedure Call and separate data-transfer path on Redis
## Scheduling
### Resources
#### Threads
- 1 Kernel Level Thread for Real-Time/Interactive Jobs (jobs with deadline/time constraints)
- n Kernel Level Thread for Batch Jobs (jobs without time constraints)
#### Coroutines
- Capable of supporting shared-stack coroutines and stand-alone stack coroutines, scheduled with no difference
- Automatic type deduction of C++ functions
- Each task is allocated to as a coroutine, mapped to 1 kernel level thread
- More than 1M tasks per GB
- Switching time: ~30ns on Intel Core i7 10710U
### Real Time Tasks
#### Description
- Task with hard start/deadline
- Scheduled on Real-Time Kernel Level Thread
- All Real-Time tasks runs in a Single Kernel Thread
#### Scheduling Strategy
- A static schedule of Real-Time tasks as a finite list of time intervals are used to schedule Hard Real-Time tasks
- Each time interval has a RT-Task ID, beginning of an interval defines the start of the task with the ID
- An ID equals to 0 defines a slot for Non-Real-Time tasks
- Schedule of Real Time Tasks are downloaded for each cycle
- Length of the cycle is defined by the end time of last interval in the finite list
- Time Point defined in the interval are time points relative to the start of the cycle
- Scheduler picks up Real Time task according to the ID associated with interval
- 2 schedules with different distributions of Non-RealTime slots are provided, picked up according to some algorithm
#### Performance
##### Jitter
- Jitter = (StartTimeOfRTTask - BeginOfTimeInterval), relative to the start of cycle
- Jitter is within 20us, usually within 10us
### Interactive Tasks
#### Description
- Task with soft deadline
- Scheduled on Real-Time Kernel Level Thread, possibly Non-Real-Time Kernel Level Thread if expired
#### Scheduling Strategy
- If task has not passed its deadline, schedule it in Earliest Deadline First
- Otherwise, if the programmer allows this task to degrade to a Batch Task, it will degrade. 
- Otherwise, it would be pushed into a Bounded LIFO Stack, and will be executed if there is no task in EDF queue
- If the LIFO stack exceeded its capacity, it would pop out (cancel) the eariest-pushed task
- Just like how we process our emails, assignments, and messages...
### Batch Task
#### Description
- Task without deadline
- Scheduled on Non-Real-Time Kernel Level Thread
- Capable of Parallel Execution
#### Scheduling Strategy
- Only Batch Tasks could be executed in worker threads
- FIFO with Work Steal (between Non-Real-Time Kernel Level Threads)
- Work Steal is triggered if one Non-Real-Time Kernel Level Thread run out of tasks
- Load Balancing: Tasks are distributed by the main scheduler to the Non-Real-Time Kernel Level Threads with least amout of tasks 
## Synchronization Primitives
### Purpose
- Synchronize at user-space, without blocking Kernel-Level thread
- Blocking Kernel level threads could break the real-time constraints
- Easier for Asynchronous Programming
### Spin Mutex
### Condition Variable
### Mutex
### Semaphore
### Future
## Timer as Timing Wheel
- Powered by ```timeout.c: Tickless Hierarchical Timing Wheel```
### Precision
- Ideally 30us, normally 50us
## Remote Procedure Call (RPC)
### This is a Toy-Level simple ADD-ON of RPC capability to the scheduler
- Each RPC Invocation is matched to a Batch Task
### Fault-torelance of this RPC depends on JAMScript JavaScript-Side
### Protocol
- JSON/Cbor on MQTT
### Fault-torelance
- Heartbeat monitoring
- Multiple connections
- Acknowledgement, retry and timeout of connection failure
### Note
Please return by a pointer to memory allocated on heap by malloc/calloc to avoid memory leak. 
### Example
```cpp
JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
ribScheduler.RegisterRPCall("DuplicateCString", strdup);
ribScheduler.RegisterRPCall("RPCFunctionJSync", [] (int a, int b) -> int {
    std::cout << "Sync Add of " << a << " + " << b << std::endl;
    return a + b;
});
ribScheduler.RegisterRPCall("RPCFunctionJAsync", [] (int a, int b) -> int {
    std::cout << "Async Subtract of " << a << " - " << b << std::endl;
    return a - b;
});
ribScheduler.RegisterRPCall("ConcatCString", [] (char *dest, const char *src) -> char* {
    printf("please return by a pointer to memory allocated on heap");
    return strdup(strcat(dest, src));
});
```
## Named Local Invocation
### Example of Registration
```cpp
JAMScript::RIBScheduler ribScheduler(1024 * 256);
ribScheduler.RegisterLocalExecution("TestExec", [] (int a, int b) -> int {
    return a + b;
});
```
### Example of Invocation
```cpp
auto fu = ribScheduler.CreateLocalNamedInteractiveExecution<int>(
    // Interactive Task Attributes
    {false, 1024}, std::chrono::milliseconds(1000), std::chrono::microseconds(50),
    // Execution Name
    std::string("TestExec"),
    // Execution Parameters
    3, 4
);
// after some lines of code...
int val = fu.Get();
```
### Note 
Also available for Batch Tasks
## General Workflow
### Create an application
- goto app folder, and make a folder for your application
- add a CMakeLists.txt for configuration and link your favoriate machine learning libraries here
- add a main.c/main.cpp, and other files you need
- make sure your include your folder in app/CMakeLists.txt
- if you are VSCode Users, goto [VSCode Users - Reasons to Use](#debug-your-app)
### Create a module
- goto src folder, and make a folder for your module
- add a CMakeLists.txt for your library
- add files you need
- make sure your include your folder in src/CMakeLists.txt
### Testing
- please read https://github.com/catchorg/Catch2, as we are using Catch2 for testing
- VSCode users, please review [Debug Your App](#debug-your-app) for an option of debugging your test cases
- **DO NOT** compile your tests into LIBRARIES
## CiteLab Coding Style (draft)
- vscode formatter style: ```Visual Studio```
- avoid unnecessary memcpy/copy construct of large object
- avoid unnecessary notify of threads on Condition Variable
- avoid random comment outs without explanation
- avoid unhandled memory allocations, like strdup
- more to come...
## Note to VSCode Users
### Reasons to use
- free yourselves from gdb, cmake... commands
- looks cool
### Build
- install CMake Tools, available from https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools
- configure, and build all project using the top right buttons
- supported compiler: Clang-9
### Debug
- Goto ```Run``` tab, and choose which app you would like to add/debug
- To debug testcases, run VSCode task ```"clang-9 - test-debug"```
### <a name="debug-your-app"></a> Debug Your App
- Add the following "chunk" in ```launch.json```
- ```<name>```: name of your app folder
- ```<exec>```: name of your app executable
```json
{
    "name": "clang-9 - <name>",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build/app/<name>/<exec>",
    "args": [],
    "stopAtEntry": false,
    "cwd": "${workspaceFolder}/build/app/<name>",
    "environment": [],
    "externalConsole": false,
    "MIMode": "lldb",
    "setupCommands": [
        {
            "description": "format lldbprint",
            "text": "-enable-pretty-printing",
            "ignoreFailures": true
        }
    ],
    "preLaunchTask": "build project",
    "miDebuggerPath": "/usr/bin/lldb"
}
```
