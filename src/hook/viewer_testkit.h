#pragma once

// Shared pieces of the self-tests for 0x00577300-0x00588180 (the viewer, gaps, records, career save info:
// selftests_viewer*.cpp): an arena pair at fixed offsets, a call trace with arena-relative words, and recording
// stand-ins for what most of that code calls (memory, script structures, scripts, debug output). Each test runs the
// original and ours on identical stand-in state and compares the traces and the whole arena.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace ViewerTestKit {

using namespace TestStandIns;
using Script::CStruct;

constexpr uint32_t kArenaSize = 0x20000;
inline ArenaPair<kArenaSize> g_storage;
inline uint32_t g_base;
inline uint32_t g_heapNext;
constexpr uint32_t kHeap = 0x10000; // the stand-in allocator's half of the arena

inline uint32_t At(uint32_t offset) { return g_base + offset; }
template <typename T>
T* Ptr(uint32_t offset)
{
    return reinterpret_cast<T*>(g_base + offset);
}
inline bool InArena(uint32_t word) { return word - g_base <= kArenaSize; }
// Our functions' addresses as the game's (code pointers ours stores where the original stores its own).
inline std::map<uint32_t, uint32_t> g_codeAliases;
template <typename F>
void Alias(F ours, uint32_t game)
{
    g_codeAliases[reinterpret_cast<uint32_t>(CodeAddress(ours))] = game;
    g_codeAliases[AddressOf(ours)] = game;
}
// A word in either run's arena as its offset (both arenas, so that a value the two runs share that happens to lie in
// one arena - the DLL moves with ASLR - maps the same in both: batch 109).
inline uint32_t Rel(uint32_t word)
{
    uint32_t start = reinterpret_cast<uint32_t>(g_storage.Get());
    if (word - start < 2 * kArenaSize)
        return ArenaWord(g_storage.Get(), kArenaSize, word);
    auto it = g_codeAliases.find(word);
    return it == g_codeAliases.end() ? word : it->second;
}
inline uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
inline void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
inline void PushArena(std::vector<uint32_t>& out)
{
    const auto* words = reinterpret_cast<const uint32_t*>(g_base);
    for (uint32_t i = 0; i < kArenaSize / 4; ++i)
        out.push_back(Rel(words[i]));
}
inline uint32_t Allocate(uint32_t size)
{
    uint32_t block = At(g_heapNext);
    g_heapNext += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), 0xCD, size);
    return block;
}
inline uint32_t Text(uint32_t pointer) { return pointer ? Hash(reinterpret_cast<const char*>(pointer)) : 0xFFFFFFFFu; }

// The stand-in state scripts and structures answer from.
inline std::map<uint32_t, int> g_integers;                         // script globals (GetInteger)
inline std::map<uint32_t, bool> g_scripts;                         // scripts that exist
inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_params; // (structure, name checksum or hash) -> value
inline std::map<std::pair<uint32_t, uint32_t>, std::vector<float>> g_vectors;

// Debug output's arguments recorded after the format (the first as text; a test sets how many).
inline int g_printfArgs = 0;
inline std::vector<Saved> g_saved;
inline void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_saved);
    g_saved.clear();
    g_codeAliases.clear();
    g_printfArgs = 0;
}

// Memory, structures, scripts and debug output. Without `deleteStruct`, Script::DeleteStruct isn't stood in for
// (for code whose original inlines it: both then reach the memory manager's Free).
inline void InstallCommon(StandIns& s, bool deleteStruct = true)
{
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0x1C30, {Text(r.args[0])});
        // (only as many as the format has conversions: %s as its text, others raw)
        int i = 0;
        for (const char* p = reinterpret_cast<const char*>(r.args[0]); *p && i < g_printfArgs; ++p) {
            if (*p != '%')
                continue;
            while (p[1] && !(p[1] >= 'a' && p[1] <= 'z' && p[1] != 'l') && !(p[1] >= 'A' && p[1] <= 'Z'))
                ++p;
            while (p[1] == 'l')
                ++p;
            ++p;
            g_trace.push_back(*p == 's' ? Text(r.args[1 + i]) : Rel(r.args[1 + i]));
            ++i;
        }
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1], r.args[2]});
        return Allocate(r.args[0]);
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        uint32_t block = Allocate(r.args[0]);
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xB110, {r.ecx});
        return 0u;
    });
    // Structures: made, filled, read, freed.
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Rec(0x6410, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 8);
        return r.ecx;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x7130, {r.ecx});
        return 0u;
    });
    if (deleteStruct)
        s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
            Rec(0x9070, {r.args[0]});
            return 0u;
        });
    s.At({0x00478390, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t, const char*)>(&CStruct::AddString))},
         12, [](Regs& r) {
             Rec(0x8390, {r.ecx, r.args[0], r.args[1], Text(r.args[2])});
             return 0u;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Rec(0x79D0, {r.ecx, r.args[0], Text(r.args[1])});
             return 0u;
         });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Rec(0x7B80, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    // Named getters: the value from g_params / g_vectors (keyed by the structure and the name's hash).
    auto vector = [](Regs& r, uint32_t name) {
        Rec(0x7060, {r.ecx, name, r.args[2] & 0xFF});
        auto it = g_vectors.find({r.ecx, name});
        if (it == g_vectors.end())
            return 0u;
        std::memcpy(reinterpret_cast<void*>(r.args[1]), it->second.data(), 16);
        return 1u;
    };
    s.At({0x00477060, 0x00477010,
          AddressOf(static_cast<bool (CStruct::*)(const char*, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [vector](Regs& r) { return vector(r, Text(r.args[0])); });
    // Scripts.
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x8F60, {r.args[0]});
        auto it = g_integers.find(r.args[0]);
        return it == g_integers.end() ? 0u : static_cast<uint32_t>(it->second);
    });
    s.At({0x00472DE0, AddressOf(&Script::ScriptExists)}, 0, [](Regs& r) {
        Rec(0x2DE0, {r.args[0]});
        return g_scripts[r.args[0]] ? 1u : 0u;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                 bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return At(0x7F00);
         });
}

// A fresh arena for a run: zeroed below kHeap, 0xCD above; the memory manager and the structure pools pointed at
// stand-ins (structures come from the manager: the pools' "in use" words are zero).
inline void SetUpArena(int run)
{
    uint8_t* arena = g_storage.Get()[run];
    g_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kHeap, 0xCD, kArenaSize - kHeap);
    g_heapNext = kHeap;
    g_trace.clear();
    g_params.clear();
    g_vectors.clear();
    GameVariable<void*>(0x007D1800) = Ptr<void>(0x7000); // the memory manager
    GameVariable<bool>(0x007CC979) = true;
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
}

// The globals SetUpArena changes, saved (Cleanup puts them back), plus `more`.
inline void SaveGlobals(std::initializer_list<Saved> more)
{
    g_saved = {{0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}};
    g_saved.insert(g_saved.end(), more);
    SaveAll(g_saved);
}

} // namespace ViewerTestKit
