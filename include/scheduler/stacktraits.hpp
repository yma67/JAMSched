#pragma once
#include <cstdint>

namespace jamc
{
    struct StackTraits
    {
        bool useSharedStack, canSteal, launchImmediately, directSwap;
        uint32_t stackSize;
        int pinCore;
        constexpr StackTraits() : useSharedStack(false), stackSize(4096U), canSteal(true), pinCore(-1), launchImmediately(true), directSwap(false) {}
        constexpr StackTraits(bool ux, uint32_t ssz) : useSharedStack(ux), stackSize(ssz), canSteal(true), pinCore(-1), launchImmediately(true), directSwap(false) {}
        constexpr StackTraits(bool ux, uint32_t ssz, bool cs) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1), launchImmediately(true), directSwap(false) {}
        constexpr StackTraits(bool ux, uint32_t ssz, bool cs, int pc) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(pc), launchImmediately(true), directSwap(false) {}
        constexpr StackTraits(bool ux, uint32_t ssz, bool cs, bool immediate) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1),launchImmediately(immediate), directSwap(false) {}
        constexpr StackTraits(bool ux, uint32_t ssz, bool cs, bool immediate, bool ds) : useSharedStack(ux), stackSize(ssz), canSteal(cs), pinCore(-1),launchImmediately(immediate), directSwap(ds) {}
    };
}
