add_executable(skynet main.cc)
target_link_libraries(skynet PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/skynet.erl" "${CMAKE_CURRENT_BINARY_DIR}/skynet.erl" COPYONLY)