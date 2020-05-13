/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */
// Copyright 2018 Sen Han <00hnes@gmail.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "core/scheduler/context.h"
#include <inttypes.h>
#include <string.h>
#include <stdarg.h> 

#define ulong task_ulong
#define uint task_uint
#define uchar task_uchar
#define ushort task_ushort
#define uvlong task_uvlong
#define vlong task_vlong

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long long uvlong;
typedef long long vlong;

#if defined(__APPLE__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#if defined(__APPLE__)
#if defined(__i386__)
#define NEEDX86MAKECONTEXT
#define NEEDSWAPCONTEXT
#elif defined(__x86_64__)
#define NEEDAMD64MAKECONTEXT
#define NEEDSWAPCONTEXT
#else
#define NEEDPOWERMAKECONTEXT
#define NEEDSWAPCONTEXT
#endif
#endif

#if defined(__x86_64__)
#define NEEDAMD64MAKECONTEXT
#define NEEDSWAPCONTEXT
#endif

#if defined(__FreeBSD__) && defined(__i386__) && __FreeBSD__ < 5
#define NEEDX86MAKECONTEXT
#define NEEDSWAPCONTEXT
#endif

#if defined(__OpenBSD__) && defined(__i386__)
#define NEEDX86MAKECONTEXT
#define NEEDSWAPCONTEXT
#endif

#if defined(__arm__)
#define NEEDSWAPCONTEXT
#define NEEDARMMAKECONTEXT
#endif

#if defined(__linux__) && defined(__mips__)
#define	NEEDSWAPCONTEXT
#define	NEEDMIPSMAKECONTEXT
#endif

#ifdef NEEDPOWERMAKECONTEXT
void
makecontext(jam_ucontext_t *ucp, void (*func)(void), int argc, ...)
{
	ulong *sp, *tos;
	va_list arg;

	tos = (ulong*)ucp->uc_stack.ss_sp+ucp->uc_stack.ss_size/sizeof(ulong);
	sp = tos - 16;	
	ucp->mc.pc = (long)func;
	ucp->mc.sp = (long)sp;
	va_start(arg, argc);
	ucp->mc.r3 = va_arg(arg, long);
	va_end(arg);
}
#endif

#ifdef NEEDX86MAKECONTEXT
void
makecontext(jam_ucontext_t *ucp, void (*func)(void), int argc, ...)
{
	int *sp;

	sp = (int*)ucp->uc_stack.ss_sp+ucp->uc_stack.ss_size/4;
	sp -= argc;
	sp = (void*)((uintptr_t)sp - (uintptr_t)sp%16);	/* 16-align for OS X */
	memmove(sp, &argc+1, argc*sizeof(int));

	*--sp = 0;		/* return address */
	ucp->uc_mcontext.mc_eip = (long)func;
	ucp->uc_mcontext.mc_esp = (int)sp;
}
#endif

#ifdef NEEDAMD64MAKECONTEXT
void
makecontext(jam_ucontext_t *ucp, void (*func)(void), int argc, ...)
{

	va_list va;
	memset(ucp->registers, 0, 15 * 8);
	if(argc != 2) *(int*)0 = 0;
	va_start(va, argc);
	ucp->registers[14] = va_arg(va, int);
	ucp->registers[15] = va_arg(va, int);
	va_end(va);
    uintptr_t u_p = (uintptr_t)(ucp->uc_stack.ss_size - (sizeof(void*) << 1) + 
                    (uintptr_t)ucp->uc_stack.ss_sp);
    u_p = (u_p >> 4) << 4;
    ucp->registers[4] = (uintptr_t)(func);
    ucp->registers[5] = (uintptr_t)(u_p - sizeof(void*));
    *((void**)(ucp->registers[5])) = (void*)(NULL);
}
#endif

#ifdef NEEDARMMAKECONTEXT
void
makecontext(jam_ucontext_t *uc, void (*fn)(void), int argc, ...)
{
	int i, *sp;
	va_list arg;
	
	sp = (int*)uc->uc_stack.ss_sp+uc->uc_stack.ss_size/4;
	va_start(arg, argc);
	for(i=0; i<4 && i<argc; i++)
		uc->registers[i] = va_arg(arg, uint);
	va_end(arg);
	uc->registers[13] = (uint)sp;
	uc->registers[14] = (uint)fn;
}
#endif

#ifdef NEEDMIPSMAKECONTEXT
void
makecontext(jam_ucontext_t *uc, void (*fn)(void), int argc, ...)
{
	int i, *sp;
	va_list arg;
	
	va_start(arg, argc);
	sp = (int*)uc->uc_stack.ss_sp+uc->uc_stack.ss_size/4;
	for(i=0; i<4 && i<argc; i++)
		uc->uc_mcontext.mc_regs[i+4] = va_arg(arg, int);
	va_end(arg);
	uc->uc_mcontext.mc_regs[29] = (int)sp;
	uc->uc_mcontext.mc_regs[31] = (int)fn;
}
#endif
#if defined(NEEDSWAPCONTEXT)
#ifdef __x86_64__
/**
 * reference: libaco 
 */
asm(".text\n\t"
    ".p2align 5\n\t"
    ".globl " ASM_SYMBOL(swapcontext) "\n\t"
    ASM_SYMBOL(swapcontext) ":\n\t"
    "movq    (%rsp), %rdx\n\t"
    "leaq     0x8(%rsp), %rcx\n\t"
    "movq     %r12, (%rdi)\n\t"
    "movq     %r13, 0x8(%rdi)\n\t"
    "movq     %r14, 0x10(%rdi)\n\t"
    "movq     %r15, 0x18(%rdi)\n\t"
    "movq     %rdx, 0x20(%rdi)\n\t"
    "movq     %rcx, 0x28(%rdi)\n\t"
    "movq     %rbx, 0x30(%rdi)\n\t"
    "movq     %rbp, 0x38(%rdi)\n\t"
    "movq     %rdi, 0x70(%rdi)\n\t"
    "movq     %rsi, 0x78(%rdi)\n\t"
    "fnstcw  0x40(%rdi)\n\t"
    "stmxcsr 0x44(%rdi)\n\t"
    "movq     0x0(%rsi), %r12\n\t"
    "movq     0x8(%rsi), %r13\n\t"
    "movq     0x10(%rsi), %r14\n\t"
    "movq     0x18(%rsi), %r15\n\t"
    "movq     0x20(%rsi), %rax\n\t"
    "movq     0x28(%rsi), %rcx\n\t"
    "movq     0x30(%rsi), %rbx\n\t"
    "movq     0x38(%rsi), %rbp\n\t"
    "fldcw   0x40(%rsi)\n\t"
    "ldmxcsr 0x44(%rsi)\n\t"
    "movq     0x70(%rsi), %rdi\n\t"
    "movq     0x78(%rsi), %rsi\n\t"
    "movq     %rcx, %rsp\n\t"
    "jmp     *%rax\n\t");
#else
#if defined(__arm__)
asm(".text\n\t"
    ".p2align 5\n\t"
    ".globl " ASM_SYMBOL(getcontext) "\n\t"
    ASM_SYMBOL(getcontext) ":\n\t"
    "str	r1, [r0,#4]\n\t"
	"str	r2, [r0,#8]\n\t"
	"str	r3, [r0,#12]\n\t"
	"str	r4, [r0,#16]\n\t"
	"str	r5, [r0,#20]\n\t"
	"str	r6, [r0,#24]\n\t"
	"str	r7, [r0,#28]\n\t"
	"str	r8, [r0,#32]\n\t"
	"str	r9, [r0,#36]\n\t"
	"str	r10, [r0,#40]\n\t"
	"str	r11, [r0,#44]\n\t"
	"str	r12, [r0,#48]\n\t"
	"str	r13, [r0,#52]\n\t"
	"str	r14, [r0,#56]\n\t"
	/* store 1 as r0-to-restore */
	"mov	r1, #1\n\t"
	"str	r1, [r0]\n\t"
	/* return 0 */
	"mov	r0, #0\n\t"
	"mov	pc, lr\n\t"
);
asm(".text\n\t"
    ".p2align 5\n\t"
    ".globl " ASM_SYMBOL(setcontext) "\n\t"
    ASM_SYMBOL(setcontext) ":\n\t"
    "ldr	r1, [r0,#4]\n\t"
	"ldr	r2, [r0,#8]\n\t"
	"ldr	r3, [r0,#12]\n\t"
	"ldr	r4, [r0,#16]\n\t"
	"ldr	r5, [r0,#20]\n\t"
	"ldr	r6, [r0,#24]\n\t"
	"ldr	r7, [r0,#28]\n\t"
	"ldr	r8, [r0,#32]\n\t"
	"ldr	r9, [r0,#36]\n\t"
	"ldr	r10, [r0,#40]\n\t"
	"ldr	r11, [r0,#44]\n\t"
	"ldr	r12, [r0,#48]\n\t"
	"ldr	r13, [r0,#52]\n\t"
	"ldr	r14, [r0,#56]\n\t"
	"ldr	r0, [r0]\n\t"
	"mov	pc, lr\n\t");
#endif
int
swapcontext(jam_ucontext_t *oucp, const jam_ucontext_t *ucp)
{
	if(getcontext(oucp) == 0)
		setcontext(ucp);
	return 0;
}
#endif
#endif