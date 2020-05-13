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
#include <string.h>
#include <stdarg.h> 

#if defined(__APPLE__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#if defined(__x86_64__)
void makecontext(jam_ucontext_t *ucp, void (*func)(void), int argc, ...) {

	va_list va;
	memset(ucp->registers, 0, 15 * 8);
	if(argc != 2) *(int*)0 = 0;
	va_start(va, argc);
	ucp->registers[14] = va_arg(va, int);
	ucp->registers[15] = va_arg(va, int);
	va_end(va);
    uintptr_t u_p = (uintptr_t)(ucp->uc_stack.ss_size - 
                    (sizeof(void*) << 1) + 
                    (uintptr_t)ucp->uc_stack.ss_sp);
    u_p = (u_p >> 4) << 4;
    ucp->registers[4] = (uintptr_t)(func);
    ucp->registers[5] = (uintptr_t)(u_p - sizeof(void*));
    *((void**)(ucp->registers[5])) = (void*)(NULL);
}
asm(".text\n\t"
    ".p2align 5\n\t"
    ".globl "ASM_SYMBOL(swapcontext)"\n\t"
    ASM_SYMBOL(swapcontext) ":  \n\t"
    "movq       (%rsp), %rdx    \n\t"
    "leaq       0x8(%rsp), %rcx \n\t"
    "movq       %r12, (%rdi)    \n\t"
    "movq       %r13, 0x8(%rdi)\n\t"
    "movq       %r14, 0x10(%rdi)\n\t"
    "movq       %r15, 0x18(%rdi)\n\t"
    "movq       %rdx, 0x20(%rdi)\n\t"
    "movq       %rcx, 0x28(%rdi)\n\t"
    "movq       %rbx, 0x30(%rdi)\n\t"
    "movq       %rbp, 0x38(%rdi)\n\t"
    "movq       %rdi, 0x70(%rdi)\n\t"
    "movq       %rsi, 0x78(%rdi)\n\t"
    "fnstcw     0x40(%rdi)      \n\t"
    "stmxcsr    0x44(%rdi)      \n\t"
    "movq       0x0(%rsi),  %r12\n\t"
    "movq       0x8(%rsi),  %r13\n\t"
    "movq       0x10(%rsi), %r14\n\t"
    "movq       0x18(%rsi), %r15\n\t"
    "movq       0x20(%rsi), %rax\n\t"
    "movq       0x28(%rsi), %rcx\n\t"
    "movq       0x30(%rsi), %rbx\n\t"
    "movq       0x38(%rsi), %rbp\n\t"
    "fldcw      0x40(%rsi)      \n\t"
    "ldmxcsr    0x44(%rsi)      \n\t"
    "movq       0x70(%rsi), %rdi\n\t"
    "movq       0x78(%rsi), %rsi\n\t"
    "movq       %rcx, %rsp      \n\t"
    "jmp        *%rax           \n\t");
#elif defined(__aarch64__)
#error "not implemented yet"
#elif defined(__arm__)
void makecontext(jam_ucontext_t *uc, void (*fn)(void), int argc, ...) {
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
asm(".text\n\t"
    ".p2align 5\n\t"
    ".globl swapcontext \n\t"
    "swapcontext:       \n\t"
    "stmia  r0, {r0-r14}\n\t"
    "ldr    r0, [r1]    \n\t"
    "add    r1, r1, #8  \n\t"
    "ldmia  r1, {r2-r14}\n\t"
    "sub    r1, r1, #8  \n\t"
    "ldr    r1, [r1, #4]\n\t"
    "bx     lr          \n\t");
#elif defined(__mips__)
#error "not implemented yet"
void makecontext(jam_ucontext_t *uc, void (*fn)(void), int argc, ...) {
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