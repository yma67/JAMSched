/// Copyright 2020 Yuxiang Ma, Muthucumaru Maheswaran
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// contains CreateContext from
///
/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */
///
/// contains x86-64 SwapToContext from
///
/// Copyright 2018 Sen Han <00hnes@gmail.com>
///
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
#include "core/coroutine/context.h"
#include <stdarg.h>
#include <string.h>

#if defined(__x86_64__)
void CreateContext(JAMScriptUserContext *ucp, void (*func)(void), int argc, ...)
{
    va_list va;
    if (argc != 2)
        __builtin_trap();
    va_start(va, argc);
    ucp->registers[8] = va_arg(va, int);
    ucp->registers[9] = va_arg(va, int);
    va_end(va);
    uintptr_t u_p =
        (uintptr_t)(ucp->uc_stack.ss_size - (sizeof(void *) << 1) + (uintptr_t)ucp->uc_stack.ss_sp);
    u_p = (u_p >> 4) << 4;
    ucp->registers[4] = (uintptr_t)(func);
    ucp->registers[5] = (uintptr_t)(u_p - sizeof(void *));
    *((void **)(ucp->registers[5])) = (void *)(NULL);
}
asm(".text                      \n\t"
    ".p2align 5                 \n\t"
#ifdef __APPLE__
    ".globl _SwapToContext      \n\t"
    "_SwapToContext:            \n\t"
#else
    ".globl SwapToContext       \n\t"
    "SwapToContext:             \n\t"
#endif
    "movq       (%rsp), %rdx    \n\t"
    "leaq       0x8(%rsp), %rcx \n\t"
    "movq       %r12, (%rdi)    \n\t"
    "movq       %r13, 0x8(%rdi) \n\t"
    "movq       %r14, 0x10(%rdi)\n\t"
    "movq       %r15, 0x18(%rdi)\n\t"
    "movq       %rdx, 0x20(%rdi)\n\t"
    "movq       %rcx, 0x28(%rdi)\n\t"
    "movq       %rbx, 0x30(%rdi)\n\t"
    "movq       %rbp, 0x38(%rdi)\n\t"
    "movq       %rdi, 0x40(%rdi)\n\t"
    "movq       %rsi, 0x48(%rdi)\n\t"
    "movq       0x0(%rsi),  %r12\n\t"
    "movq       0x8(%rsi),  %r13\n\t"
    "movq       0x10(%rsi), %r14\n\t"
    "movq       0x18(%rsi), %r15\n\t"
    "movq       0x20(%rsi), %rax\n\t"
    "movq       0x28(%rsi), %rcx\n\t"
    "movq       0x30(%rsi), %rbx\n\t"
    "movq       0x38(%rsi), %rbp\n\t"
    "movq       0x40(%rsi), %rdi\n\t"
    "movq       0x48(%rsi), %rsi\n\t"
    "movq       %rcx, %rsp      \n\t"
    "jmp        *%rax           \n\t");
#elif defined(__aarch64__)
void CreateContext(JAMScriptUserContext *ucp, void (*func)(void), int argc, ...)
{
    va_list va;
    memset(ucp->registers, 0, 25 * 8);
    if (argc != 2)
        __builtin_trap();
    va_start(va, argc);
    ucp->registers[23] = va_arg(va, uint32_t);
    ucp->registers[24] = va_arg(va, uint32_t);
    va_end(va);
    uintptr_t u_p =
        (uintptr_t)(ucp->uc_stack.ss_size - (sizeof(void *) << 1) + (uintptr_t)ucp->uc_stack.ss_sp);
    u_p = (u_p >> 4) << 4;
    ucp->registers[13] = (uintptr_t)(func);
    ucp->registers[14] = (uintptr_t)(u_p);
}
asm(".text                      \n\t"
    ".p2align 5                 \n\t"
    ".globl SwapToContext       \n\t"
    "SwapToContext:             \n\t"
    "stp x16, x17, [x0]         \n\t"
    "stp x19, x20, [x0, #16]    \n\t"
    "stp x21, x22, [x0, #32]    \n\t"
    "stp x23, x24, [x0, #48]    \n\t"
    "stp x25, x26, [x0, #64]    \n\t"
    "stp x27, x28, [x0, #80]    \n\t"
    "stp fp,  lr,  [x0, #96]    \n\t"
    "mov x2,  sp                \n\t"
    "str x2,       [x0, #112]   \n\t"
    "stp d8,  d9,  [x0, #120]   \n\t"
    "stp d10, d11, [x0, #136]   \n\t"
    "stp d12, d13, [x0, #152]   \n\t"
    "stp d14, d15, [x0, #168]   \n\t"
    "mov x2,  x0                \n\t"
    "stp x0,  x1,  [x2, #184]   \n\t"
    "ldp x16, x17, [x1]         \n\t"
    "ldp x19, x20, [x1, #16]    \n\t"
    "ldp x21, x22, [x1, #32]    \n\t"
    "ldp x23, x24, [x1, #48]    \n\t"
    "ldp x25, x26, [x1, #64]    \n\t"
    "ldp x27, x28, [x1, #80]    \n\t"
    "ldp fp,  lr,  [x1, #96]    \n\t"
    "ldr x2,       [x1, #112]   \n\t"
    "mov sp,  x2                \n\t"
    "ldp d8,  d9,  [x1, #120]   \n\t"
    "ldp d10, d11, [x1, #136]   \n\t"
    "ldp d12, d13, [x1, #152]   \n\t"
    "ldp d14, d15, [x1, #168]   \n\t"
    "mov x2,  x1                \n\t"
    "ldp x0,  x1,  [x2, #184]   \n\t"
    "ret                        \n\t");
#elif defined(__arm__)
void CreateContext(JAMScriptUserContext *uc, void (*fn)(void), int argc, ...)
{
    va_list arg;
    uintptr_t u_p =
        (uintptr_t)(uc->uc_stack.ss_size - (sizeof(void *) << 1) + (uintptr_t)uc->uc_stack.ss_sp);
    u_p = (u_p >> 4) << 4;
    va_start(arg, argc);
    uc->registers[0] = va_arg(arg, uint32_t);
    uc->registers[1] = va_arg(arg, uint32_t);
    va_end(arg);
    uc->registers[13] = (uint32_t)u_p;
    uc->registers[14] = (uint32_t)fn;
}
asm(".text\n\t"
    ".p2align 5\n\t"
    ".globl SwapToContext       \n\t"
    "SwapToContext:             \n\t"
    "stmia  r0, {r0-r14}        \n\t"
    "ldr    r0, [r1]            \n\t"
    "add    r1, r1, #8          \n\t"
    "ldmia  r1, {r2-r14}        \n\t"
    "sub    r1, r1, #8          \n\t"
    "ldr    r1, [r1, #4]        \n\t"
    "bx     lr                  \n\t");
#elif defined(__mips__)
#error "not implemented yet"
void CreateContext(JAMScriptUserContext *uc, void (*fn)(void), int argc, ...)
{
    int i, *sp;
    va_list arg;
    va_start(arg, argc);
    sp = (int *)uc->uc_stack.ss_sp + uc->uc_stack.ss_size / 4;
    for (i = 0; i < 4 && i < argc; i++)
        uc->uc_mcontext.mc_regs[i + 4] = va_arg(arg, int);
    va_end(arg);
    uc->uc_mcontext.mc_regs[29] = (int)sp;
    uc->uc_mcontext.mc_regs[31] = (int)fn;
}
#endif