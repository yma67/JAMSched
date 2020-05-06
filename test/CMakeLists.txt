FetchContent_Declare(catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v2.12.1)
FetchContent_MakeAvailable(catch2)

add_compile_definitions(CATCH_CONFIG_ENABLE_BENCHMARKING)
file(GLOB_RECURSE JAMTEST_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(JAMTEST_SOURCES_LIST "")
foreach(jamtest_source ${JAMTEST_SOURCES})
    list(APPEND JAMTEST_SOURCES_LIST ${jamtest_source})
endforeach(jamtest_source ${JAMTEST_SOURCES})

find_package (Threads)
add_executable(test-main ${JAMTEST_SOURCES_LIST})
target_link_libraries(test-main PRIVATE libjamsched Catch2::Catch2 ${CMAKE_THREAD_LIBS_INIT})