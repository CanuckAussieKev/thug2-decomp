#pragma once

#include <cstdint>

// Replacing original game functions with our own.
//
// Each decompiled function registers itself with REPLACE(address, function). On startup every registered
// replacement is installed by writing a jump at the original function's entry point, so all callers in the
// exe end up in our code.
//
// The replacement must use the same calling convention and arguments as the original. Check the original's
// disassembly: VC7.1 with whole-program optimisation can give internal functions non-standard conventions.

struct Replacement {
    uint32_t address;
    void* function;
    const char* name;
};

void RegisterReplacement(const Replacement& r);
void InstallReplacements();

// Calls the original game function at `address` (only valid before replacements are installed).
template <typename Fn>
Fn Original(uint32_t address)
{
    return reinterpret_cast<Fn>(address);
}

struct ReplacementRegistrar {
    ReplacementRegistrar(uint32_t address, void* function, const char* name)
    {
        RegisterReplacement({address, function, name});
    }
};

#define REPLACE_CONCAT2(a, b) a##b
#define REPLACE_CONCAT(a, b) REPLACE_CONCAT2(a, b)
#define REPLACE(address, function)                                                                   \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, reinterpret_cast<void*>(&function), #function)
