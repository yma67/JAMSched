add_executable(io-server main.cpp)
add_executable(io-client client.c)
target_link_libraries(io-server PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")