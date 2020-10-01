add_executable(testLS main.cc)
add_executable(testLSB main2.cc)
target_link_libraries(testLS PRIVATE jamsched)
target_link_libraries(testLSB PRIVATE jamsched)