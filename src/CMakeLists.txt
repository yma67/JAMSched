include(FetchContent)

# JSON Library
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent
  GIT_TAG v3.7.3)

FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  FetchContent_Populate(json)
  add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Boost
find_package(Boost 1.65.0)
include_directories(${Boost_INCLUDE_DIRS})

file(GLOB_RECURSE JAMCOMPONENTS_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/ "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
set(JAMCOMPONENTS_SOURCES_LIST "")
foreach(jamcomponent_source ${JAMCOMPONENTS_SOURCES})
    list(APPEND JAMCOMPONENTS_SOURCES_LIST ${jamcomponent_source})
endforeach(jamcomponent_source ${JAMTEST_SOURCES})
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/time/time.cc")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/concurrency/condition_variable.cc")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/concurrency/spinlock.cc")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/scheduler/decider.cc")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/scheduler/tasklocal.cc")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/remote/remote.cc")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/remote/mqtt/mqtt_adapter.c")
list(APPEND JAMCOMPONENTS_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/remote/mqtt/nvoid.c")

set(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")
# set(CMAKE_CXX_FLAGS "${CXXFLAGS} -ggdb")

find_package(Threads)
add_library(jamsched ${JAMCOMPONENTS_SOURCES_LIST})
target_compile_features(jamsched PUBLIC cxx_std_17)
target_link_libraries(jamsched PUBLIC nlohmann_json::nlohmann_json paho-mqtt3a ${CMAKE_THREAD_LIBS_INIT})
target_include_directories(jamsched PUBLIC ${PROJECT_SOURCE_DIR}/include)