cmake_minimum_required(VERSION 3.17)

project(JAMScript DESCRIPTION "JAMScript Nano-kernel Scheduler" LANGUAGES C CXX ASM)

set(CMAKE_BUILD_TYPE Debug)

include(FetchContent)
if(${CMAKE_VERSION} VERSION_LESS 3.14)
    include(cmake/add_FetchContent_MakeAvailable.cmake)
endif()

add_subdirectory(src)
add_subdirectory(app)
add_subdirectory(test)
enable_testing()
add_test(NAME jamtest COMMAND test-main)