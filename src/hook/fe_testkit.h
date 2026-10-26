#pragma once

// Shared pieces of the front-end range's self-tests (selftests_fe*.cpp): an arena pair (the original's run and
// ours), a call trace with arena-relative words, stand-in parameter structures (their getters answer from a
// table; adders are recorded), stand-ins returning floats in ST0, and the run-both-and-compare loop.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

#include "game/core/crc.h"
#include "game/script/struct.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace FeTest {

using namespace TestStandIns;
using Script::CStruct;

// The arenas: the original's, then ours, one after the other; 64K-aligned (VirtualAlloc), so stale half-pointers
// map the same in both runs.
constexpr uint32_t kArenaSize = 0x40000;
inline uint8_t* g_arenas = nullptr;
inline uint32_t g_base = 0;
inline void AllocateArenas()
{
    if (!g_arenas)
        g_arenas = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
}
// A word pointing into either arena (ArenaWord, selftest.h): the mapping is the same in both runs. Mapping only
// the run's own arena made a value that merely falls inside one of them - a packed colour 0x04030605, flags
// 0x01000000, a font's 0x01A53A61 - compare differently whenever VirtualAlloc happened to put the arenas there
// (between 0x00F90000 and 0x04080000 offline): "screen element: parameters" and five other tests failed in
// about one thug2_check run in thirty.
inline bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_arenas) <= 2 * kArenaSize; }
// The current thread's stack (its locals' addresses differ between the original's run and ours).
inline bool OnStack(uint32_t word)
{
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    return word >= reinterpret_cast<uint32_t>(tib->StackLimit) && word < reinterpret_cast<uint32_t>(tib->StackBase);
}
inline uint32_t Rel(uint32_t word)
{
    if (InArena(word))
        return ArenaWord(g_arenas, kArenaSize, word);
    return OnStack(word) ? 0x57AC0000u : word;
}
inline uint32_t At(uint32_t offset) { return g_base + offset; }
template <typename T>
T* Ptr(uint32_t offset)
{
    return reinterpret_cast<T*>(g_base + offset);
}
inline void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
inline void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
inline uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
inline float FloatOf(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
// The arena's words (pointers made relative; stale half-pointers too, into either arena: ArenaWordOrHigh),
// appended to the trace.
inline void Snapshot(uint32_t offset, uint32_t size, uint32_t mark)
{
    g_trace.push_back(0x5AA50000u + mark);
    const auto* w = reinterpret_cast<const uint32_t*>(g_base + offset);
    for (uint32_t i = 0; i < size / 4; ++i)
        g_trace.push_back(InArena(w[i]) || !OnStack(w[i]) ? ArenaWordOrHigh(g_arenas, kArenaSize, w[i]) : 0x57AC0000u);
}

// Starts a run: its arena (zeroed, then `dirty` bytes of 0xCD from `dirtyFrom`), an empty trace.
inline void BeginRun(bool original, uint32_t dirtyFrom = 0, uint32_t dirty = 0)
{
    AllocateArenas();
    g_base = reinterpret_cast<uint32_t>(g_arenas + (original ? 0 : kArenaSize));
    std::memset(g_arenas + (original ? 0 : kArenaSize), 0, kArenaSize);
    if (dirty)
        std::memset(reinterpret_cast<void*>(At(dirtyFrom)), 0xCD, dirty);
    g_trace.clear();
}

// --- stand-in parameter structures --------------------------------------------------------------------------
// Answers by (kind, structure, name): 'K' checksum, 'F' float, 'I' integer, 'S' structure / array pointer,
// 'V' vector, 'T' string (a pointer), 'C' contains (component), 'L' flag.
inline std::map<std::tuple<char, uint32_t, uint32_t>, std::vector<uint32_t>> g_answers;
inline void Answer(char kind, uint32_t structure, uint32_t name, std::vector<uint32_t> value)
{
    g_answers[{kind, Rel(structure), name}] = std::move(value);
}
inline const std::vector<uint32_t>* Lookup(char kind, uint32_t structure, uint32_t name)
{
    auto it = g_answers.find({kind, Rel(structure), name});
    return it == g_answers.end() ? nullptr : &it->second;
}

inline void StandInStructGetters(StandIns& s)
{
    using Script::VectorValue;
    auto getter = [&](std::initializer_list<uint32_t> addresses, char kind, uint32_t tag, bool byString) {
        s.At(addresses, 12, [kind, tag, byString](Regs& r) {
            uint32_t name = byString ? Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]))
                                     : r.args[0];
            Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
            const std::vector<uint32_t>* value = Lookup(kind, r.ecx, name);
            if (!value)
                return 0u;
            std::memcpy(reinterpret_cast<void*>(r.args[1]), value->data(), value->size() * 4);
            return 1u;
        });
    };
    getter({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
           'K', 0x6E50, false);
    getter({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 'F',
           0x6E51, false);
    getter({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, VectorValue*, int)>(&CStruct::GetVector))},
           'V', 0x6E52, false);
    getter({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 'I',
           0x6E54, false);
    getter({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
           'S', 0x6E55, false);
    getter({0x00476860,
            AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
           'S', 0x6E56, false);
    getter({0x00476590,
            AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
           'T', 0x6E57, false);
    // pairs: 'P' answers {x, y}, into two floats or a CPair (+4, +8)
    s.At({0x00476770, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, float*, int)>(&CStruct::GetPair))},
         16, [](Regs& r) {
             Rec(0x6E58, {r.ecx, r.args[0], r.args[3] & 0xFF});
             const std::vector<uint32_t>* value = Lookup('P', r.ecx, r.args[0]);
             if (!value)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = (*value)[0];
             *reinterpret_cast<uint32_t*>(r.args[2]) = (*value)[1];
             return 1u;
         });
    s.At({0x00476DF0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CPair*, bool)>(&CStruct::GetPair))},
         12, [](Regs& r) {
             Rec(0x6E59, {r.ecx, r.args[0], r.args[2] & 0xFF});
             const std::vector<uint32_t>* value = Lookup('P', r.ecx, r.args[0]);
             if (!value)
                 return 0u;
             reinterpret_cast<uint32_t*>(r.args[1])[1] = (*value)[0];
             reinterpret_cast<uint32_t*>(r.args[1])[2] = (*value)[1];
             return 1u;
         });
    auto flag = [&](std::initializer_list<uint32_t> addresses, uint32_t tag, char kind, bool byString) {
        s.At(addresses, 4, [tag, kind, byString](Regs& r) {
            uint32_t name = byString ? Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]))
                                     : r.args[0];
            Rec(tag, {r.ecx, name});
            return Lookup(kind, r.ecx, name) ? 1u : 0u;
        });
    };
    flag({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 0x6AF0,
         'C', false);
    flag({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 0x6B40, 'L',
         false);
    flag({0x00476BA0, AddressOf(static_cast<bool (CStruct::*)(const char*)>(&CStruct::ContainsFlag))}, 0x6BA0, 'L',
         true);
}
inline void StandInStructAdders(StandIns& s)
{
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Rec(0x7B80, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8,
         [](Regs& r) {
             Rec(0x7C60, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477FC0, AddressOf(&CStruct::AddPair)}, 12, [](Regs& r) {
        Rec(0x7FC0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // (an array: its type, size and up to 8 elements)
    s.At({0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArray))}, 8,
         [](Regs& r) {
             auto* array = reinterpret_cast<Script::CArray*>(r.args[1]);
             Rec(0x8B00, {r.ecx, r.args[0], r.args[1]});
             if (array) {
                 Rec(0x8B01, {array->m_type, array->m_size});
                 const uint32_t* e = array->m_size == 1 ? &array->m_single : array->m_elements;
                 for (uint32_t i = 0; e && i < array->m_size && i < 8; ++i)
                     Rec(0x8B02, {e[i]});
             }
             return 0u;
         });
}

// --- stand-ins returning a float in ST0 ---------------------------------------------------------------------
// The handler gets the registers and the arguments as for an ordinary stand-in and returns the float.
inline float g_floatResult;
inline uint8_t* g_floatCode = nullptr;
inline size_t g_floatUsed = 0;
inline void AtFloat(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop,
                    std::function<float(Regs&)> handler)
{
    if (!g_floatCode)
        g_floatCode = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    // The inner thunk sees one more return address (the wrapper's): the arguments shifted back for the handler.
    void* inner = s.Thunk(0, [handler](Regs& r) {
        Regs shifted = r;
        shifted.ret = r.args[0];
        std::memmove(shifted.args, r.args + 1, sizeof(r.args) - 4);
        g_floatResult = handler(shifted);
        r.ecx = shifted.ecx;
        r.edx = shifted.edx;
        return 0u;
    });
    if (g_floatUsed + 16 > 0x1000)
        g_floatUsed = 0;
    uint8_t* p = g_floatCode + g_floatUsed;
    g_floatUsed += 16;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(inner) - (reinterpret_cast<uintptr_t>(p) + 5));
    p[0] = 0xE8;
    std::memcpy(p + 1, &rel, 4);
    p[5] = 0xD9;
    p[6] = 0x05;
    uint32_t where = reinterpret_cast<uint32_t>(&g_floatResult);
    std::memcpy(p + 7, &where, 4);
    p[11] = 0xC2;
    p[12] = static_cast<uint8_t>(pop);
    p[13] = static_cast<uint8_t>(pop >> 8);
    FlushInstructionCache(GetCurrentProcess(), p, 16);
    for (uint32_t address : addresses)
        s.Bytes(address, Branch(0xE9, address, p));
}

inline bool g_verbose = false; // (log each run's start: to place a crash)

// --- the comparison -----------------------------------------------------------------------------------------
// Runs `body` for the original (true) and ours (false), each in a fresh arena, and compares the traces.
inline void RunBoth(const char* what, const std::function<void(bool)>& body)
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        if (g_verbose)
            Log("  [%s: %s]", what, run == 0 ? "original" : "ours");
        body(run == 0);
        traces[run] = g_trace;
    }
    Compare(traces[0], traces[1], what);
}

} // namespace FeTest
