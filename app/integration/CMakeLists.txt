add_executable(jinterf main.cc)
target_link_libraries(jinterf PRIVATE jamsched)
target_include_directories(jamsched PRIVATE "${PROJECT_SOURCE_DIR}/include/")