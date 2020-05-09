add_executable(one-task main.c)
target_link_libraries(one-task PRIVATE libjamsched)
target_include_directories(libjamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")