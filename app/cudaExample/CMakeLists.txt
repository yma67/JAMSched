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

add_executable(seq_randsum seq_randsum.cu)
target_compile_options(seq_randsum PRIVATE -arch=sm_50)
target_link_libraries(seq_randsum PRIVATE cudart ${CUDA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} pthread)
target_compile_features(seq_randsum PRIVATE cxx_std_17)
target_include_directories(seq_randsum PRIVATE "/usr/local/cuda/include")

add_executable(stm_randsum stm_randsum.cc)
target_link_libraries(stm_randsum PRIVATE jamsched liba2cu cudart)
target_compile_features(stm_randsum PRIVATE cxx_std_17)
target_include_directories(stm_randsum PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")

add_executable(mem_randsum mem_randsum.cc)
target_link_libraries(mem_randsum PRIVATE jamsched liba2cu cudart cnmem)
target_compile_features(mem_randsum PRIVATE cxx_std_17)
target_include_directories(mem_randsum PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")
