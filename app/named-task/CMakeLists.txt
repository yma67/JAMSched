set(CMAKE_CXX_FLAGS "${CXXFLAGS} -ggdb")
add_executable(named-task main.cc)
target_link_libraries(named-task PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")