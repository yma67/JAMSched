# JAMSched [![Build Status](https://travis-ci.com/yma67/JAMSched.svg?token=WDPhpGnigsQCoWp5WMJt&branch=master)](https://travis-ci.com/yma67/JAMSched)
#### The light weight micro-kernel-like scheduling framework with Remote Procedure Call
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
### Remote Procedure Call (RPC)
#### Example (using ```strdup```)
```cpp
auto DuplicateCStringFunctor = std::function(strdup);
using StrdupFuncType = decltype(DuplicateCStringFunctor);
auto DuplicateCStringInvoker = JAMScript::RExecDetails::RoutineRemote<StrdupFuncType>(DuplicateCStringFunctor);
std::unordered_map<std::string, JAMScript::RExecDetails::RoutineInterface *> invokerMap = {
    {std::string("DuplicateCString"), &DuplicateCStringInvoker}
};
JAMScript::RIBScheduler ribScheduler(1024 * 256, "tcp://localhost:1883", "app-1", "dev-1");
ribScheduler.RegisterRPCalls(invokerMap);
```
### Named Local Invocation
#### Example of Registration
```cpp
JAMScript::RIBScheduler ribScheduler(1024 * 256);
ribScheduler.RegisterNamedExecution("TestExec", [](int a, int b) -> int {
    return a + b;
});
```
#### Example of Invocation
```cpp
JAMScript::Future<int> fu = 
ribScheduler.CreateLocalNamedInteractiveExecution<int>(
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
#### Note 
Also available for Batch Tasks
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
