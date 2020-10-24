#ifndef CONTEXT_H
#define CONTEXT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <stddef.h>
#include <stdint.h>

    typedef struct _JAMScriptStack
    {
        void *ss_sp;
        size_t ss_size;
    } JAMScriptStack;

    struct JAMScriptUContext
    {
#if defined(__x86_64__)
        uintptr_t registers[8];
#elif defined(__aarch64__)
        uintptr_t registers[23];
#else
#error "platform not supported"
#endif
        JAMScriptStack uc_stack;
    };

    typedef struct JAMScriptUContext JAMScriptUserContext;
    extern int SwapToContext(JAMScriptUserContext *, JAMScriptUserContext *, void*);
    extern void CreateContext(JAMScriptUserContext *ucp, void (*func)(void));

#ifdef __cplusplus
}
#endif

#endif
