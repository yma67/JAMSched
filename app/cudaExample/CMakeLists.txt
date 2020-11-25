enable_language(CUDA)

find_package(Boost 1.71.0 COMPONENTS fiber context filesystem system REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

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
target_link_libraries(seq_randsum PRIVATE cudart ${CUDA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} liba2cu)
target_compile_features(seq_randsum PRIVATE cxx_std_17)
target_include_directories(seq_randsum PRIVATE "/usr/local/cuda/include")

add_executable(seq_randsum_forkjoin seq_randsum_forkjoin.cu)
target_compile_options(seq_randsum_forkjoin PRIVATE -arch=sm_50)
target_link_libraries(seq_randsum_forkjoin PRIVATE cudart ${CUDA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} liba2cu)
target_compile_features(seq_randsum_forkjoin PRIVATE cxx_std_17)
target_include_directories(seq_randsum_forkjoin PRIVATE "/usr/local/cuda/include")

add_executable(single_stream single_stream.cu)
target_compile_options(single_stream PRIVATE -arch=sm_50)
target_link_libraries(single_stream PRIVATE cudart ${CUDA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} liba2cu ${Boost_LIBRARIES})
target_compile_features(single_stream PRIVATE cxx_std_17)
target_include_directories(single_stream PRIVATE "/usr/local/cuda/include")

add_executable(inf_exec inf_exec.cu)
target_compile_options(inf_exec PRIVATE -arch=sm_50)
target_link_libraries(inf_exec PRIVATE cudart ${CUDA_LIBRARIES} liba2cu)
target_compile_features(inf_exec PRIVATE cxx_std_17)
target_include_directories(inf_exec PRIVATE "/usr/local/cuda/include")

add_executable(stm_randsum stm_randsum.cc)
target_link_libraries(stm_randsum PRIVATE jamsched liba2cu cudart)
target_compile_features(stm_randsum PRIVATE cxx_std_17)
target_include_directories(stm_randsum PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")

add_executable(mem_randsum mem_randsum.cc)
target_link_libraries(mem_randsum PRIVATE jamsched liba2cu cudart cnmem)
target_compile_features(mem_randsum PRIVATE cxx_std_17)
target_include_directories(mem_randsum PRIVATE deps "${PROJECT_SOURCE_DIR}/include/" "/usr/local/cuda/include")

add_executable(mem_randsum_thread mem_randsum_thread.cu)
target_compile_options(mem_randsum_thread PRIVATE -arch=sm_50)
target_link_libraries(mem_randsum_thread PRIVATE cudart ${CUDA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} liba2cu)
target_compile_features(mem_randsum_thread PRIVATE cxx_std_17)
target_include_directories(mem_randsum_thread PRIVATE "/usr/local/cuda/include")
