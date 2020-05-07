add_library(libjamxtask shared-stack-task.c)
target_include_directories(libjamxtask PUBLIC "${PROJECT_SOURCE_DIR}/include/xtask/")
target_include_directories(libjamxtask PRIVATE "${PROJECT_SOURCE_DIR}/include/core/")