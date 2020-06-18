# JAMSched [![Build Status](https://travis-ci.com/citelab/JAMSched.svg?token=WDPhpGnigsQCoWp5WMJt&branch=master)](https://travis-ci.com/citelab/JAMSched) 
#### The light weight micro-kernel-like scheduling framework
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
- 100 characters per line
- 4 space alignment
- avoid unnecessary blank lines
- parenthesis, especially those in function calls, should be aligned
- vscode formatter 
``` { BasedOnStyle: Google, UseTab: Never, IndentWidth: 4, TabWidth: 4,  AllowShortIfStatementsOnASingleLine: false, IndentCaseLabels: false, ColumnLimit: 100, AccessModifierOffset: -4, NamespaceIndentation: All }```
## Note to VSCode Users
### Reasons to use
- free yourselves from gdb, cmake... commands
- looks cool
### Build
- install CMake Tools, available from https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools
- configure, and build all project using the top right buttons
### Debug
- Goto ```Run``` tab, and choose which app you would like to add/debug
- To debug testcases, run VSCode task ```"gcc-7 - test-debug"```
### <a name="debug-your-app"></a> Debug Your App
- Add the following "chunk" in ```launch.json```
- ```<name>```: name of your app folder
- ```<exec>```: name of your app executable
```json
{
    "name": "gcc-7 - fifo-sched",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build/app/<name>/<exec>",
    "args": [],
    "stopAtEntry": false,
    "cwd": "${workspaceFolder}/build/app/<name>",
    "environment": [],
    "externalConsole": false,
    "MIMode": "gdb",
    "setupCommands": [
        {
            "description": "format gdb print",
            "text": "-enable-pretty-printing",
            "ignoreFailures": true
        }
    ],
    "preLaunchTask": "build project",
    "miDebuggerPath": "/usr/bin/gdb"
},
```
