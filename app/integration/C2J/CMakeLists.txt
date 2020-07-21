add_executable(testC2J1  main.cc)
target_link_libraries(testC2J1 PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
