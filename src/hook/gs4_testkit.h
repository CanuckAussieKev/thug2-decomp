#pragma once

// Shared pieces of the self-tests for the game's script commands 0x005B0A40-0x005B6560 (selftests_gs4_*.cpp):
// hand-built structures and arrays, a stand-in calling script, global structures / arrays by name, recording
// stand-ins for the structure-changing methods (the game's and ours alike), and calling a command (original or ours)
// with its result recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/pool.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace Gs4Tests {

using namespace TestStandIns;
using Script::CArray;
using Script::CComponent;
using Script::CStruct;
using Script::ESymbolType;

// A script structure built by hand (no memory manager needed): components chained in the order added.
struct TestParams {
    alignas(16) uint8_t storage[8];
    CComponent components[16];
    int count = 0;
    TestParams() { Reset(); }
    void Reset()
    {
        std::memset(storage, 0, sizeof(storage));
        std::memset(components, 0, sizeof(components));
        count = 0;
    }
    CStruct* Struct() { return reinterpret_cast<CStruct*>(storage); }
    TestParams& Add(ESymbolType type, uint32_t name, uint32_t raw)
    {
        CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.raw = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<CComponent**>(storage)[1] = &c;
        ++count;
        return *this;
    }
    TestParams& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
    TestParams& Flag(uint32_t flag) { return Add(Script::ESYMBOLTYPE_NAME, 0, flag); }
    TestParams& Integer(uint32_t name, int value) { return Add(Script::ESYMBOLTYPE_INTEGER, name, static_cast<uint32_t>(value)); }
    TestParams& Float(uint32_t name, float value)
    {
        uint32_t raw;
        std::memcpy(&raw, &value, 4);
        return Add(Script::ESYMBOLTYPE_FLOAT, name, raw);
    }
    TestParams& String(uint32_t name, const char* value) { return Add(Script::ESYMBOLTYPE_STRING, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Structure(uint32_t name, CStruct* value) { return Add(Script::ESYMBOLTYPE_STRUCTURE, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Array(uint32_t name, CArray* value) { return Add(Script::ESYMBOLTYPE_ARRAY, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Vector(uint32_t name, void* value) { return Add(Script::ESYMBOLTYPE_VECTOR, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Pair(uint32_t name, void* value) { return Add(Script::ESYMBOLTYPE_PAIR, name, reinterpret_cast<uint32_t>(value)); }
};

// A script array built by hand (one element is kept in place of the pointer).
struct TestArray {
    CArray array;
    uint32_t elements[16];
    TestArray() { Set(Script::ESYMBOLTYPE_NONE, {}); }
    TestArray(ESymbolType type, std::initializer_list<uint32_t> values) { Set(type, values); }
    void Set(ESymbolType type, std::initializer_list<uint32_t> values)
    {
        std::memset(&array, 0, sizeof(array));
        std::memset(elements, 0, sizeof(elements));
        array.m_type = type;
        array.m_size = static_cast<uint32_t>(values.size());
        uint32_t i = 0;
        for (uint32_t v : values)
            elements[i++] = v;
        if (array.m_size == 1)
            array.m_single = elements[0];
        else
            array.m_elements = elements;
    }
    CArray* Get() { return &array; }
};

inline uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

// The calling script and its parameters.
alignas(16) inline uint8_t g_script[0x200];
inline TestParams g_own;
inline Script::CScript* NewScript(void* object = nullptr)
{
    std::memset(g_script, 0, sizeof(g_script));
    reinterpret_cast<CStruct**>(g_script)[0x14 / 4] = g_own.Struct();
    reinterpret_cast<void**>(g_script)[0xBC / 4] = object;
    return reinterpret_cast<Script::CScript*>(g_script);
}

// Global structures and arrays by name (Script::GetStructure 0x00479020 / GetArray 0x00479070 stood in for).
inline std::map<uint32_t, CStruct*> g_globalStructs;
inline std::map<uint32_t, CArray*> g_globalArrays;

// Arrays made by the commands (the array pool's Allocate stood in for: these, in order).
alignas(16) inline uint8_t g_newArrays[8][16];
inline int g_newArrayCount;
inline uint32_t ArrayId(uint32_t value)
{
    auto base = reinterpret_cast<uint32_t>(g_newArrays);
    if (value >= base && value < base + sizeof(g_newArrays))
        return 0xA7700000u + (value - base);
    return value;
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);
// Call the command at `address` (the original) or `ours` and record its result.
inline void Call(bool original, uint32_t address, CFunc ours, TestParams& params, void* object = nullptr)
{
    Script::CScript* script = NewScript(object);
    bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
    Record({0xC0DE, address, result ? 1u : 0u});
}

// The symbol table, emptied while the tests run (names in the structures look nothing up).
inline std::vector<Saved> g_saved;
inline Script::CSymbolTableEntry* g_table[0x1000];
inline void SaveGlobals(std::initializer_list<Saved> more)
{
    g_saved = {{0x006F3624, 4, {}}, {0x006F22B4, 16, {}}, {0x006F3614, 16, {}}};
    g_saved.insert(g_saved.end(), more);
    SaveAll(g_saved);
    std::memset(g_table, 0, sizeof(g_table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_table;
}
inline void RestoreGlobals()
{
    RemoveStandIns();
    if (!g_saved.empty())
        RestoreAll(g_saved);
    g_saved.clear();
    g_globalStructs.clear();
    g_globalArrays.clear();
}

// A stand-in array pool (the pool pair 0x006F22B4's current pool: its Allocate stood in for).
alignas(16) inline uint8_t g_arrayPool[0x40];
inline void UseStandInArrayPool()
{
    auto& pair = GameVariable<Mem::PoolPair>(0x006F22B4);
    pair.current = 0;
    pair.pools[0] = reinterpret_cast<Mem::Pool*>(g_arrayPool);
    pair.pools[1] = nullptr;
    g_newArrayCount = 0;
    std::memset(g_newArrays, 0xCD, sizeof(g_newArrays));
}
// The structure pool (0x006F3614) likewise: new structures come from the same stand-in buffers.
inline void UseStandInStructPool()
{
    auto& pair = GameVariable<Mem::PoolPair>(0x006F3614);
    pair.current = 0;
    pair.pools[0] = reinterpret_cast<Mem::Pool*>(g_arrayPool);
    pair.pools[1] = nullptr;
}

// Both traces logged side by side when THUG2_GS4_DUMP is set (finding where they part).
inline void DumpTraces(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    if (!GetEnvironmentVariableA("THUG2_GS4_DUMP", nullptr, 0))
        return;
    for (size_t i = 0; i < a.size() || i < b.size(); ++i)
        Log("  %5u %08X %08X%s", static_cast<unsigned>(i), i < a.size() ? a[i] : 0u, i < b.size() ? b[i] : 0u,
            i < a.size() && i < b.size() && a[i] == b[i] ? "" : "  <<");
}

// The structure methods that change a structure, the global look-ups, the array methods, the debug output.
inline void StandInStructCalls(StandIns& s)
{
    using AddInt = void (CStruct::*)(uint32_t, int);
    using AddIntN = void (CStruct::*)(const char*, int);
    using AddFlt = void (CStruct::*)(uint32_t, float);
    using AddStr = void (CStruct::*)(uint32_t, const char*);
    using AddSum = void (CStruct::*)(uint32_t, uint32_t);
    using AddSumN = void (CStruct::*)(const char*, uint32_t);
    using AddSt = void (CStruct::*)(uint32_t, CStruct*);
    using AddArr = void (CStruct::*)(uint32_t, CArray*);
    using AddVec = void (CStruct::*)(uint32_t, float, float, float);
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00477860, AddressOf(&CStruct::RemoveFlag)}, 4, [](Regs& r) {
        Record({0x7860, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<AddInt>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477C30, AddressOf(static_cast<AddIntN>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7C30, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00477C60, AddressOf(static_cast<AddFlt>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004779D0, AddressOf(static_cast<AddStr>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, r.ecx, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00478390, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t, const char*)>(&CStruct::AddString))}, 12,
         [](Regs& r) {
             Record({0x8390, r.ecx, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
             return 0u;
         });
    s.At({0x00477D40, AddressOf(static_cast<AddSum>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477DF0, AddressOf(static_cast<AddSumN>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7DF0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00477E20, AddressOf(static_cast<AddVec>(&CStruct::AddVector))}, 16, [](Regs& r) {
        Record({0x7E20, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x00478670, AddressOf(static_cast<AddSt>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478B00, AddressOf(static_cast<AddArr>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Record({0x8B00, r.ecx, r.args[0], ArrayId(r.args[1])});
        return 0u;
    });
    s.At({0x004780D0, AddressOf(static_cast<AddArr>(&CStruct::AddArrayPointer))}, 8, [](Regs& r) {
        Record({0x80D0, r.ecx, r.args[0], ArrayId(r.args[1])});
        return 0u;
    });
    s.At({0x004781B0, AddressOf(static_cast<AddSt>(&CStruct::AddStructurePointer))}, 8, [](Regs& r) {
        Record({0x81B0, r.ecx, r.args[0], ArrayId(r.args[1])});
        return 0u;
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, ArrayId(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, ArrayId(r.args[0])});
        return 0u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        auto it = g_globalStructs.find(r.args[0]);
        return it == g_globalStructs.end() ? 0u : reinterpret_cast<uint32_t>(it->second);
    });
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        auto it = g_globalArrays.find(r.args[0]);
        return it == g_globalArrays.end() ? 0u : reinterpret_cast<uint32_t>(it->second);
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx == reinterpret_cast<uint32_t>(g_arrayPool) ? 1u : r.ecx});
        return g_newArrayCount < 8 ? reinterpret_cast<uint32_t>(g_newArrays[g_newArrayCount++]) : 0u;
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, ArrayId(r.ecx), r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Record({0xCB10, ArrayId(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        auto* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, Hash(format)});
        int arguments = 0; // (only the arguments the format uses: the rest is stack garbage)
        for (const char* p = format; *p && arguments < 8; ++p)
            if (*p == '%')
                Record({r.args[1 + arguments++]});
        return 0u;
    });
}

} // namespace Gs4Tests
