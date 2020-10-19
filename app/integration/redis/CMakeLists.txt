add_executable(redisi main.cc)
target_link_libraries(redisi PRIVATE jamsched tcmalloc)