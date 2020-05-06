add_library(libjamfuture future.c)
target_include_directories(libjamfuture PUBLIC "${PROJECT_SOURCE_DIR}/include/future/")
target_include_directories(libjamfuture PRIVATE "${PROJECT_SOURCE_DIR}/include/core/")