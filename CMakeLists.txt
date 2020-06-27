cmake_minimum_required(VERSION 3.15)

project(JAMScript DESCRIPTION "JAMScript Scheduler & Runtime" LANGUAGES C CXX ASM)

set(CMAKE_BUILD_TYPE Debug)
# add_compile_definitions(JAMSCRIPT_ENABLE_VALGRIND)
# add_compile_definitions(JAMSCRIPT_SCHED_AI_EXP)
option(JAMSCRIPT_ON_TRAVIS "Run on Travis CI" OFF)
if(JAMSCRIPT_ON_TRAVIS)
    add_compile_definitions(JAMSCRIPT_ON_TRAVIS)
endif(JAMSCRIPT_ON_TRAVIS)
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
