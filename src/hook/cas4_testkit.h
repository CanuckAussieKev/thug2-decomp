#pragma once

// Shared pieces of the self-tests for the skater models' appearance, model builder, bone volumes and engine entry
// points (0x00488DB0-0x0048C74F, selftests_cas4_*.cpp): script structures stood in for by recording code that answers
// from the test's tables (values by structure and name, flags, components to walk, global structures and arrays), on
// top of rfx_testkit.h's arena memory and recording stand-ins. The original and ours run on the same stand-in state,
// so the traces and the arena are compared word for word.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "hook/rfx_testkit.h"

namespace Cas4Tests {

using namespace RfxTests;
using Script::CStruct;

// The stand-in structures' contents: a value by (structure, name) - a checksum, an integer, float bits or a pointer -,
// flags by (structure, flag), the components GetNextComponent walks, and the global structures / arrays by name.
inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_values;
inline std::set<std::pair<uint32_t, uint32_t>> g_flags;
inline std::map<uint32_t, std::vector<uint32_t>> g_components;
inline std::map<uint32_t, uint32_t> g_globalStructs, g_globalArrays;

inline void ClearStructs()
{
    g_values.clear();
    g_flags.clear();
    g_components.clear();
    g_globalStructs.clear();
    g_globalArrays.clear();
}
inline uint32_t Ptr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
inline void SetValue(const void* s, uint32_t name, uint32_t value) { g_values[{Ptr(s), name}] = value; }
inline void SetFloat(const void* s, uint32_t name, float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    g_values[{Ptr(s), name}] = bits;
}

// A getter: recorded (a bool-assert overload and the int one alike - `tag`), answered from g_values.
inline uint32_t AnswerGetter(uint32_t tag, Regs& r)
{
    Record({tag, Relocate(r.ecx), r.args[0], r.args[2] & 0xFF});
    auto it = g_values.find({r.ecx, r.args[0]});
    if (it == g_values.end())
        return 0;
    *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
    return 1;
}

inline void StandInStructs(StandIns& s)
{
    using GetSum = bool (CStruct::*)(uint32_t, uint32_t*, int);
    using GetInt = bool (CStruct::*)(uint32_t, int*, int);
    using GetFlt = bool (CStruct::*)(uint32_t, float*, int);
    using GetStr = bool (CStruct::*)(uint32_t, const char**, int);
    using GetSt = bool (CStruct::*)(uint32_t, CStruct**, int);
    using GetArr = bool (CStruct::*)(uint32_t, Script::CArray**, int);
    using Contains = bool (CStruct::*)(uint32_t);
    using Remove = void (CStruct::*)(uint32_t);
    using AddInt = void (CStruct::*)(uint32_t, int);
    using AddFlt = void (CStruct::*)(uint32_t, float);
    using AddSum = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<GetSum>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return AnswerGetter(0x6950, r); });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<GetInt>(&CStruct::GetInteger))}, 12,
         [](Regs& r) { return AnswerGetter(0x6630, r); });
    s.At({0x004766D0, 0x00476D50, AddressOf(static_cast<GetFlt>(&CStruct::GetFloat))}, 12,
         [](Regs& r) { return AnswerGetter(0x66D0, r); });
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<GetStr>(&CStruct::GetString))}, 12,
         [](Regs& r) { return AnswerGetter(0x6590, r); });
    s.At({0x004767C0, 0x00476E70, AddressOf(static_cast<GetSt>(&CStruct::GetStructure))}, 12,
         [](Regs& r) { return AnswerGetter(0x67C0, r); });
    s.At({0x00476860, AddressOf(static_cast<GetArr>(&CStruct::GetArray))}, 12,
         [](Regs& r) { return AnswerGetter(0x6860, r); });
    s.At({0x00476B40, AddressOf(static_cast<Contains>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Relocate(r.ecx), r.args[0]});
        return g_flags.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00476AF0, AddressOf(static_cast<Contains>(&CStruct::ContainsComponentNamed))}, 4, [](Regs& r) {
        Record({0x6AF0, Relocate(r.ecx), r.args[0]});
        return g_values.count({r.ecx, r.args[0]}) || g_flags.count({r.ecx, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Record({0x6490, Relocate(r.ecx), Relocate(r.args[0])});
        const std::vector<uint32_t>& list = g_components[r.ecx];
        if (!r.args[0])
            return list.empty() ? 0u : list[0];
        for (size_t i = 0; i + 1 < list.size(); ++i) {
            if (list[i] == r.args[0])
                return list[i + 1];
        }
        return 0u;
    });
    s.At({0x004776B0, AddressOf(static_cast<Remove>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<AddInt>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Relocate(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477C60, AddressOf(static_cast<AddFlt>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Relocate(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<AddSum>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Relocate(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Relocate(r.ecx)});
        auto* words = reinterpret_cast<uint32_t*>(r.ecx);
        words[0] = 0x5717;
        words[1] = 0;
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x00478560, AddressOf(&CStruct::ExpandInto)}, 8, [](Regs& r) {
        Record({0x8560, Relocate(r.ecx), Relocate(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x00478650, AddressOf(&CStruct::Assign)}, 4, [](Regs& r) {
        Record({0x8650, Relocate(r.ecx), Relocate(r.args[0])});
        return r.ecx;
    });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Record({0x83C0, Relocate(r.ecx), r.args[0], r.args[1], Relocate(r.args[2])});
        return 0u;
    });
    using GlobalStruct = CStruct* (*)(uint32_t);
    using GlobalArray = Script::CArray* (*)(uint32_t);
    s.At({0x00479020, AddressOf(static_cast<GlobalStruct>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        auto it = g_globalStructs.find(r.args[0]);
        return it == g_globalStructs.end() ? 0u : it->second;
    });
    s.At({0x00479070, AddressOf(static_cast<GlobalArray>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        auto it = g_globalArrays.find(r.args[0]);
        return it == g_globalArrays.end() ? 0u : it->second;
    });
}

// Structures from the structure pool (0x006F3614: a pool when `pooled`, else the memory manager) and given back.
inline void StandInStructPool(StandIns& s)
{
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return Bump(8);
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9071, Relocate(r.args[0])});
        return 0u;
    });
}
inline void UseStructPool(bool pooled)
{
    auto* pair = reinterpret_cast<uint32_t*>(0x006F3614);
    pair[0] = 1;
    pair[1] = 0;
    pair[2] = 0;
    pair[3] = pooled ? 0x5B001000u : 0;
}

// CompareTwoRuns, with the entries before the first difference logged (both runs) on a mismatch.
template <typename Body>
bool CompareRuns(const char* what, bool& reported, Body body)
{
    std::vector<uint32_t> expected = body(true);
    std::vector<uint32_t> actual = body(false);
    if (expected == actual)
        return true;
    if (!reported) {
        Compare(expected, actual, what);
        size_t first = 0;
        while (first < expected.size() && first < actual.size() && expected[first] == actual[first])
            ++first;
        for (size_t i = first > 16 ? first - 16 : 0; i < first + 4; ++i)
            Log("  %s [%u] original %08X ours %08X", what, static_cast<unsigned>(i),
                i < expected.size() ? expected[i] : 0xDEADDEAD, i < actual.size() ? actual[i] : 0xDEADDEAD);
    }
    reported = true;
    return false;
}

} // namespace Cas4Tests
