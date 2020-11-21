enable_language(CUDA)

add_library(cnmem src/cnmem.cpp)
target_link_libraries(cnmem LINK_PUBLIC ${CUDA_LIBRARIES} jamsched)
target_include_directories(cnmem PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")

add_library(liba2interf src/jamc-cuda.cpp)
target_link_libraries(liba2interf PRIVATE jamsched)
target_include_directories(liba2interf PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")

add_library(liba2cu src/Kernel.cu)
target_compile_options(liba2cu PRIVATE -dc -arch=sm_50)
target_link_libraries(liba2cu PRIVATE liba2interf cudart cnmem nvToolsExt)
target_compile_features(liba2cu PRIVATE cxx_std_17)
set_target_properties(liba2cu PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)
target_include_directories(liba2cu PRIVATE deps "${PROJECT_SOURCE_DIR}/include/")

add_executable(a2 main.cc)
target_link_libraries(a2 PRIVATE jamsched liba2cu cudart cnmem)
target_compile_features(a2 PRIVATE cxx_std_17)
target_include_directories(a2 PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")
