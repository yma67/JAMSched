#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdint.h>
#include <stddef.h>

typedef struct sigaltstack {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
} jam_stack_t;

struct jam_ucontext{
#if defined(__x86_64__)
    uintptr_t registers[16];
#elif defined(__aarch64__)
#error "platform not supported"
    uint32_t registers[24];
#elif defined(__arm__)
    uint32_t registers[16];
#elif defined(__mips__)
#error "platform not supported"
    uint32_t registers[32];
#else
#error "platform not supported"
#endif
    jam_stack_t	uc_stack;
};

#if defined(__APPLE__)
#define jam_mcontext libthread_mcontext
#define jam_mcontext_t libthread_mcontext_t
#define jam_ucontext libthread_ucontext
#define jam_ucontext_t libthread_ucontext_t
#endif

typedef struct jam_ucontext jam_ucontext_t;
int	swapcontext(jam_ucontext_t*, jam_ucontext_t*);
void makecontext(jam_ucontext_t *ucp, void (*func)(void), int argc, ...);

#endif