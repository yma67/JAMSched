cmake_minimum_required(VERSION 3.17)

project(JAMScript DESCRIPTION "JAMScript Nano-kernel Scheduler" LANGUAGES C CXX ASM)

set(CMAKE_BUILD_TYPE Debug)

add_subdirectory(src)
add_subdirectory(app)