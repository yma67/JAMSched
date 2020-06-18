add_executable(one-task main.cc)
target_link_libraries(one-task PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")