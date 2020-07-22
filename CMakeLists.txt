cmake_minimum_required(VERSION 3.15)

project(JAMScript DESCRIPTION "JAMScript Scheduler & Runtime" LANGUAGES C CXX ASM)

set(CMAKE_BUILD_TYPE Debug) # Debug with RelWithDebInfo

option(JAMSCRIPT_ON_TRAVIS_OPT "Run on Travis CI" OFF)
option(JAMSCRIPT_ENABLE_VALGRIND_OPT "Enable valgrind" OFF)
option(JAMSCRIPT_SCHED_AI_EXP_OPT "Print scheduling info" OFF)
option(JAMSCRIPT_BLOCK_WAIT_OPT "Blocking wait to reduce CPU usage when there is no task" ON)
option(JAMSCRIPT_DEBUG_LINEINFO_OPT "Add line info in debug message" OFF)
option(JAMSCRIPT_TEST_BENCHMARKING_OPT "Benchmarking while testing" OFF)

if(JAMSCRIPT_ON_TRAVIS_OPT)
    add_compile_definitions(JAMSCRIPT_ON_TRAVIS)
endif(JAMSCRIPT_ON_TRAVIS_OPT)

if(JAMSCRIPT_ENABLE_VALGRIND_OPT)
    add_compile_definitions(JAMSCRIPT_ENABLE_VALGRIND)
endif(JAMSCRIPT_ENABLE_VALGRIND_OPT)

if(JAMSCRIPT_SCHED_AI_EXP_OPT)
    add_compile_definitions(JAMSCRIPT_SCHED_AI_EXP)
endif(JAMSCRIPT_SCHED_AI_EXP_OPT)

if(JAMSCRIPT_BLOCK_WAIT_OPT)
    add_compile_definitions(JAMSCRIPT_BLOCK_WAIT)
endif(JAMSCRIPT_BLOCK_WAIT_OPT)

add_subdirectory(src)
add_subdirectory(app)
add_subdirectory(test)

enable_testing()
add_test(NAME jamtest COMMAND test-main)

find_package(Doxygen)
if(Doxygen_FOUND)
    add_subdirectory(docs)
else()
    message(STATUS "Doxygen not found, not building docs")
endif()
