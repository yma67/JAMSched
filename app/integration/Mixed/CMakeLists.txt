add_executable(testMixed  main.cc)
target_link_libraries(testMixed PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
