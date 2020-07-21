add_executable(testTT  main.cc)
target_link_libraries(testTT PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
