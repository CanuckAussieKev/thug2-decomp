#pragma once

// Shared pieces of the renderer-FX self-tests (0x4B2280-0x4D6D40, selftests_engine_glue.cpp and the others of the
// range): allocations from an arena (recorded; failing on request), recording stand-ins for thiscall / cdecl game
// functions, and calling the original or ours at an address.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/appmain_testkit.h"
#include "hook/standins.h"

namespace RfxTests {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// Allocations: blocks from an arena (or null, as the test says), recorded; pointers into it relocated.
alignas(16) inline uint8_t g_arena[0x140000];
inline uint32_t g_used;
inline std::vector<bool> g_failAllocations; // in order (none left: succeed)
inline void ResetArena(uint8_t fill = 0)
{
    g_used = 0;
    std::memset(g_arena, fill, 0x10000); // (the rest is filled as it's handed out)
    g_failAllocations.clear();
}
inline uint32_t Bump(uint32_t size)
{
    bool fail = false;
    if (!g_failAllocations.empty()) {
        fail = g_failAllocations.front();
        g_failAllocations.erase(g_failAllocations.begin());
    }
    if (fail)
        return 0;
    uint32_t at = reinterpret_cast<uint32_t>(g_arena) + g_used;
    g_used += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}
inline uint32_t ArenaRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(g_arena);
    if (word >= base && word <= base + sizeof(g_arena))
        return 0xA0000000u + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}
inline void DumpArena(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0; i < g_used; i += 4) // (what's been handed out)
        trace.push_back(ArenaRelocate(*reinterpret_cast<uint32_t*>(g_arena + i)));
}

// The memory manager's allocate / free and Mem::Calloc (the game's and ours).
inline void StandInMemory(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        uint32_t at = Bump(r.args[0]);
        if (at)
            std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
}

// A thiscall function stood in: records `this` and its stack arguments, returns `this`.
inline void StandInThis(StandIns& s, std::initializer_list<uint32_t> addresses, int args)
{
    uint32_t tag = *addresses.begin() & 0xFFFFFF;
    s.At(addresses, static_cast<uint16_t>(4 * args), [tag, args](Regs& r) {
        Record({tag, Relocate(r.ecx)});
        for (int i = 0; i < args; ++i)
            Record({Relocate(r.args[i])});
        return r.ecx;
    });
}
// A cdecl function stood in: records its arguments, returns `result`.
inline void StandInCdecl(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t result = 0)
{
    uint32_t tag = *addresses.begin() & 0xFFFFFF;
    s.At(addresses, 0, [tag, args, result](Regs& r) {
        Record({tag});
        for (int i = 0; i < args; ++i)
            Record({Relocate(r.args[i])});
        return result;
    });
}

// Two runs (the original's, then ours) of `body`, which returns its trace; a mismatch reported once per test.
template <typename Body>
bool CompareTwoRuns(const char* what, bool& reported, Body body)
{
    std::vector<uint32_t> expected = body(true);
    std::vector<uint32_t> actual = body(false);
    if (expected == actual)
        return true;
    if (!reported)
        Compare(expected, actual, what);
    reported = true;
    return false;
}

} // namespace RfxTests
