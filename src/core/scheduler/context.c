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

#if defined(__linux__) && defined(__arm__)
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
		uc->uc_mcontext.gregs[i] = va_arg(arg, uint);
	va_end(arg);
	uc->uc_mcontext.gregs[13] = (uint)sp;
	uc->uc_mcontext.gregs[14] = (uint)fn;
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
    ".intel_syntax noprefix\n\t"
    ASM_SYMBOL(swapcontext) ":\n\t"
    "mov     rdx,QWORD PTR [rsp]\n\t"      // retaddr
    "lea     rcx,[rsp+0x8]\n\t"            // rsp
    "mov     QWORD PTR [rdi+0x0], r12\n\t"
    "mov     QWORD PTR [rdi+0x8], r13\n\t"
    "mov     QWORD PTR [rdi+0x10],r14\n\t"
    "mov     QWORD PTR [rdi+0x18],r15\n\t"
    "mov     QWORD PTR [rdi+0x20],rdx\n\t" // retaddr
    "mov     QWORD PTR [rdi+0x28],rcx\n\t" // rsp
    "mov     QWORD PTR [rdi+0x30],rbx\n\t"
    "mov     QWORD PTR [rdi+0x38],rbp\n\t"
    "mov     QWORD PTR [rdi+112],rdi\n\t"
    "mov     QWORD PTR [rdi+120],rsi\n\t"
    "fnstcw  WORD PTR  [rdi+0x40]\n\t"
    "stmxcsr DWORD PTR [rdi+0x44]\n\t"
    
    "mov     r12,QWORD PTR [rsi+0x0]\n\t"
    "mov     r13,QWORD PTR [rsi+0x8]\n\t"
    "mov     r14,QWORD PTR [rsi+0x10]\n\t"
    "mov     r15,QWORD PTR [rsi+0x18]\n\t"
    "mov     rax,QWORD PTR [rsi+0x20]\n\t" // retaddr
    "mov     rcx,QWORD PTR [rsi+0x28]\n\t" // rsp
    "mov     rbx,QWORD PTR [rsi+0x30]\n\t"
    "mov     rbp,QWORD PTR [rsi+0x38]\n\t"
    "fldcw   WORD PTR      [rsi+0x40]\n\t"
    "ldmxcsr DWORD PTR     [rsi+0x44]\n\t"
    "mov     rdi, QWORD PTR [rsi+112]\n\t"
    "mov     rsi, QWORD PTR [rsi+120]\n\t"
    "mov     rsp,rcx\n\t"
    "jmp rax\n\t");
#else
int
swapcontext(jam_ucontext_t *oucp, const jam_ucontext_t *ucp)
{
	if(getcontext(oucp) == 0)
		setcontext(ucp);
	return 0;
}
#endif
#endif