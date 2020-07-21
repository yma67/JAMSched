add_executable(testRTT  main.cc)
target_link_libraries(testRTT PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
