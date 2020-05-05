add_executable(one-task main.c)
target_link_libraries(one-task PRIVATE libjamcore)
target_include_directories(libjamcore PRIVATE "${PROJECT_SOURCE_DIR}/include/")