#pragma once
#include <cstdint>

namespace JAMScript
{
    struct StackTraits
    {
        bool useSharedStack, canSteal, launchImmediately;
        uint32_t stackSize;
        int pinCore;
        StackTraits() : useSharedStack(false), stackSize(4096U), canSteal(true), pinCore(-1), launchImmediately(false) {}
        StackTraits(bool ux, uint32_t ssz) : useSharedStack(ux), stackSize(ssz), canSteal(true), pinCore(-1), launchImmediately(false) {}
        StackTraits(bool ux, uint32_t ssz, bool cs) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1), launchImmediately(false) {}
        StackTraits(bool ux, uint32_t ssz, bool cs, int pc) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(pc), launchImmediately(false) {}
        StackTraits(bool ux, uint32_t ssz, bool cs, bool immediate) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1),launchImmediately(immediate) {}
    };
}