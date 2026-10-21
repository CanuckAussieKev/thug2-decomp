#pragma once

// Shared pieces of the self-tests for the skater's components of 0x00510823-0x005197F2 (selftests_skater_*.cpp of
// that range): one arena that holds the component under test, its object and the other components it reaches (filled with the same pseudo-random values in
// both runs; pointers into it relocated so the runs compare), and recording stand-ins for every function the code
// calls - each records a tag, `this` and its arguments and answers what the test says.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace Sk3Tests {

using namespace AppMainTests;

constexpr uint32_t kArenaSize = 0x10000;
struct alignas(16) Arena {
    uint8_t bytes[kArenaSize];
};
inline Arena g_arena;
inline uint8_t* At(uint32_t offset) { return g_arena.bytes + offset; }
template <typename T>
T& F(uint32_t offset)
{
    return *reinterpret_cast<T*>(At(offset));
}
template <typename T>
T& F(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
inline uint32_t Addr(uint32_t offset) { return reinterpret_cast<uint32_t>(At(offset)); }

// Arena pointers as 0x77000000 + offset, the stack as one value.
inline uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(g_arena.bytes);
    if (word >= base && word < base + kArenaSize)
        return 0x77000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}
inline uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
inline float Float(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The arena filled from `seed`: floats (-125 to 125, in 1/16ths) in every word, so stray reads are numbers.
inline void FillArena(uint32_t seed)
{
    Lcg random(0x5C3A0000 + seed);
    for (uint32_t i = 0; i < kArenaSize; i += 4) {
        float f = static_cast<float>(static_cast<int>(random.Below(4001)) - 2000) / 16.0f;
        std::memcpy(g_arena.bytes + i, &f, 4);
    }
}
// The whole arena into the trace, pointers relocated.
inline void DumpArena(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        trace.push_back(Rel(*reinterpret_cast<uint32_t*>(g_arena.bytes + i)));
}

inline bool g_logCalls = false; // (log each recorded call: to place a crash)

// A recording stand-in for the functions at `addresses`: `args` stack arguments popped by the callee (`callerPops`:
// cdecl, none popped), recorded with `tag` and ECX (`withThis`); returns `answer`'s value (0 without one).
inline void* Rec(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t tag,
                 std::function<uint32_t(Regs&)> answer = nullptr, bool withThis = true, bool callerPops = false)
{
    return s.At(addresses, static_cast<uint16_t>(callerPops ? 0 : 4 * args),
                [args, tag, answer, withThis](Regs& r) {
                    if (g_logCalls)
                        Log("  call %X", tag);
                    g_trace.push_back(tag);
                    if (withThis)
                        g_trace.push_back(Rel(r.ecx));
                    for (int i = 0; i < args; ++i)
                        g_trace.push_back(Rel(r.args[i]));
                    return answer ? answer(r) : 0u;
                });
}
// The same for a vtable slot (a thunk).
inline void* RecThunk(StandIns& s, int args, uint32_t tag, std::function<uint32_t(Regs&)> answer = nullptr)
{
    return s.Thunk(static_cast<uint16_t>(4 * args), [args, tag, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return answer ? answer(r) : 0u;
    });
}

// The functions every component test stands in for: the allocation (answering `*calloc`, 0 = out of memory), the
// base component's constructor and destructor body, the memory manager's Free.
inline void StandInComponentBase(StandIns& s, const uint32_t* calloc)
{
    Rec(s, {0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 1, 0x1CC0,
        [calloc](Regs&) { return *calloc; }, false, true);
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    Rec(s, {0x005BABA0, AddressOf(&Mem::Manager::Free)}, 1, 0xABA0);
}

// Create, the destructor body and Destroy (freed or not) of a component at arena offset `offset`: the trace gets
// what Create returns (and out of memory), the made component's words, the vtable after each.
inline void TestLifecycle(bool original, uint32_t create, uint32_t destroyBody, uint32_t destroy, uint32_t offset,
                          uint32_t size, uint32_t& calloc, std::vector<uint32_t>& trace)
{
    calloc = 0;
    trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, create))()));
    calloc = Addr(offset);
    std::memset(At(offset), 0xCD, size); // (dirty: what Create leaves alone shows)
    trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, create))()));
    for (uint32_t i = 0; i < size; i += 4)
        trace.push_back(Rel(F<uint32_t>(offset + i)));
    F<uint32_t>(offset) = 0x0BADF00D;
    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, destroyBody))(At(offset));
    trace.push_back(Rel(F<uint32_t>(offset)));
    for (unsigned flags : {0u, 1u, 3u}) {
        F<uint32_t>(offset) = 0x0BADF00D;
        trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, destroy))(
            At(offset), flags)));
        trace.push_back(Rel(F<uint32_t>(offset)));
    }
}

// --- stand-in script structures -------------------------------------------------------------------------------
// The structures' components by the structure's address: name -> value (flags: present, any value). The getters
// (the game's and ours) answer from here and record {tag, the structure, the name, the assert flag}.
inline std::map<uint32_t, std::map<uint32_t, uint32_t>> g_structs;
inline void SetComponent(void* structure, uint32_t name, uint32_t value)
{
    g_structs[reinterpret_cast<uint32_t>(structure)][name] = value;
}
inline bool FindComponent(uint32_t structure, uint32_t name, uint32_t* value)
{
    auto s = g_structs.find(structure);
    if (s == g_structs.end())
        return false;
    auto c = s->second.find(name);
    if (c == s->second.end())
        return false;
    *value = c->second;
    return true;
}
inline void StandInStructGetters(StandIns& s)
{
    using Script::CStruct;
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            Record({tag, Rel(r.ecx), r.args[0], (r.args[2] & 0xFF) != 0 ? 1u : 0u});
            uint32_t value;
            if (!FindComponent(r.ecx, r.args[0], &value))
                return 0u;
            // (types: an array or structure is a pointer into the arena; a number isn't one)
            bool pointer = (Rel(value) & 0xFF000000) == 0x77000000;
            if ((tag == 0x6860 || tag == 0x67C0) != pointer)
                return 0u;
            *reinterpret_cast<uint32_t*>(r.args[1]) = value;
            return 1u;
        };
    };
    // (the overloads taking the assert flag as a bool that search the structure themselves - 0x00476CB0,
    // 0x00476D50, 0x00476E70, 0x00476F10 - answer as the getters they stand for; the others jump to them)
    s.At({0x00476CB0}, 12, getter(0x6630));
    s.At({0x00476D50}, 12, getter(0x66D0));
    s.At({0x00476E70}, 12, getter(0x67C0));
    s.At({0x00476F10}, 12, getter(0x6860));
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         getter(0x6950));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, getter(0x6590));
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, getter(0x6860));
    s.At({0x004767C0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CStruct**, int)>(&CStruct::GetStructure))},
         12, getter(0x67C0));
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        uint32_t value;
        return FindComponent(r.ecx, r.args[0], &value) ? 1u : 0u;
    });
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [](Regs& r) {
             Record({0x6AF0, Rel(r.ecx), r.args[0]});
             uint32_t value;
             return FindComponent(r.ecx, r.args[0], &value) ? 1u : 0u;
         });
}

// A stand-in returning a float in ST0 (`answer`'s value), recorded as Rec does.
inline float g_floatAnswer;
inline uint8_t* g_floatCode = nullptr;
inline size_t g_floatUsed = 0;
inline void RecFloat(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t tag,
                     std::function<float(Regs&)> answer, bool withThis = true)
{
    if (!g_floatCode)
        g_floatCode = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                                         PAGE_EXECUTE_READWRITE));
    // The recording thunk is called from a wrapper (one more return address: the arguments are one further up);
    // the wrapper then loads the answer and returns, popping the arguments.
    void* inner = s.Thunk(0, [args, tag, answer, withThis](Regs& r) {
        Regs shifted = r;
        shifted.ret = r.args[0];
        std::memmove(shifted.args, r.args + 1, sizeof(r.args) - 4);
        g_trace.push_back(tag);
        if (withThis)
            g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i)
            g_trace.push_back(Rel(shifted.args[i]));
        g_floatAnswer = answer(shifted);
        return 0u;
    });
    if (g_floatUsed + 16 > 0x1000)
        g_floatUsed = 0;
    uint8_t* p = g_floatCode + g_floatUsed;
    g_floatUsed += 16;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(inner) - (reinterpret_cast<uintptr_t>(p) + 5));
    uint16_t pop = static_cast<uint16_t>(4 * args);
    p[0] = 0xE8;
    std::memcpy(p + 1, &relative, 4);
    p[5] = 0xD9; // fld dword ptr [g_floatAnswer]
    p[6] = 0x05;
    uint32_t where = reinterpret_cast<uint32_t>(&g_floatAnswer);
    std::memcpy(p + 7, &where, 4);
    p[11] = 0xC2; // ret pop
    p[12] = static_cast<uint8_t>(pop);
    p[13] = static_cast<uint8_t>(pop >> 8);
    FlushInstructionCache(GetCurrentProcess(), p, 16);
    for (uint32_t address : addresses)
        s.Bytes(address, Branch(0xE9, address, p));
}

} // namespace Sk3Tests
