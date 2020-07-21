add_executable(testJ2CS  main.cc)
target_link_libraries(testJ2CS PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
