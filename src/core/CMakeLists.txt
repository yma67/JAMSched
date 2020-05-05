set(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")
add_library(libjamcore scheduler/task.c scheduler/context.c ucontext/ucontext-impl.s)
target_include_directories(libjamcore PUBLIC "${PROJECT_SOURCE_DIR}/include/core/")