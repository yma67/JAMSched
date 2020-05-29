add_executable(fifo-sched main.cpp)
target_link_libraries(fifo-sched PRIVATE jamsched)
target_compile_features(fifo-sched PRIVATE cxx_std_17)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")