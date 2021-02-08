add_subdirectory(schedule-ai-experiment)
add_subdirectory(one-task)
add_subdirectory(assignment2)
add_subdirectory(integration)
add_subdirectory(skynet)

if(DEFINED CUDA_TOOLKIT_ROOT_DIR)
    add_subdirectory(cudaExample)
endif(DEFINED CUDA_TOOLKIT_ROOT_DIR)

add_subdirectory(io)
