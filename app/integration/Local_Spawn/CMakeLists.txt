add_executable(testLS  main.cc)
target_link_libraries(testLS PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
