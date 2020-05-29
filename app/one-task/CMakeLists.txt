add_executable(one-task main.c)
target_link_libraries(one-task PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")