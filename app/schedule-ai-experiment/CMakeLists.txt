find_package(Threads)
add_executable(schedule-ai-experiment main.cpp)
target_link_libraries(schedule-ai-experiment PRIVATE jamsched  ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(schedule-ai-experiment PRIVATE cxx_std_17)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/trace30.txt" "${CMAKE_CURRENT_BINARY_DIR}/trace30.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/trace45.txt" "${CMAKE_CURRENT_BINARY_DIR}/trace45.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/trace75.txt" "${CMAKE_CURRENT_BINARY_DIR}/trace75.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/trace75decider.txt" "${CMAKE_CURRENT_BINARY_DIR}/trace75decider.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/t20_low.txt" "${CMAKE_CURRENT_BINARY_DIR}/t20_low.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/t40_medium.txt" "${CMAKE_CURRENT_BINARY_DIR}/t40_medium.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/t60_high.txt" "${CMAKE_CURRENT_BINARY_DIR}/t60_high.txt" COPYONLY)
