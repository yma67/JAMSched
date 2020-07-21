add_executable(testJ2CA  main.cc)
target_link_libraries(testJ2CA PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
