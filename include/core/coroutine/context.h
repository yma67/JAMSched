#ifndef CONTEXT_H
#define CONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>

typedef struct _JAMScriptStack {
    void *ss_sp;
    int ss_flags;
    size_t ss_size;
} JAMScriptStack;

struct JAMScriptUContext {
#if defined(__x86_64__)
    uintptr_t registers[16];
#elif defined(__aarch64__)
    uintptr_t registers[25];
#elif defined(__arm__)
    uint32_t registers[16];
#elif defined(__mips__)
#error "platform not supported"
    uint32_t registers[32];
#else
#error "platform not supported"
#endif
    JAMScriptStack uc_stack;
};

typedef struct JAMScriptUContext JAMScriptUserContext;
extern int SwapToContext(JAMScriptUserContext *, JAMScriptUserContext *);
extern void CreateContext(JAMScriptUserContext *ucp, void (*func)(void), int argc, ...);
#ifdef __cplusplus
}
#endif

#endif
