include(FetchContent)
FetchContent_Declare(catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v2.12.1)
FetchContent_MakeAvailable(catch2)

if (JAMSCRIPT_TEST_BENCHMARKING_OPT)
    add_compile_definitions(CATCH_CONFIG_ENABLE_BENCHMARKING)
endif(JAMSCRIPT_TEST_BENCHMARKING_OPT)

file(GLOB_RECURSE JAMTEST_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(JAMTEST_SOURCES_LIST "")
foreach(jamtest_source ${JAMTEST_SOURCES})
    list(APPEND JAMTEST_SOURCES_LIST ${jamtest_source})
endforeach(jamtest_source ${JAMTEST_SOURCES})
list(APPEND JAMTEST_SOURCES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/future/test-future.cpp")

find_package(Threads)
add_executable(test-main ${JAMTEST_SOURCES_LIST})
target_link_libraries(test-main PRIVATE jamsched Catch2::Catch2 ${CMAKE_THREAD_LIBS_INIT})