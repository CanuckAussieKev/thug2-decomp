// Self-tests for the engine's script commands 0x0044B7E0-0x0044E6B0 (script utilities, arrays, music, streams,
// sounds, timers): each original against ours on identical stand-in state; the structure-changing and engine
// functions they call are stood in for by recording code (over the game's function and our version of it alike),
// so they're compared call by call. Structure getters run for real on hand-built structures.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/pool.h"
#include "game/mth/random.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/pcm/music_cfuncs.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/script/script_util_cfuncs.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CArray;
using Script::CComponent;
using Script::CStruct;
using Script::ESymbolType;

// A script structure built by hand (no memory manager needed): components chained in the order added.
struct TestParams {
    alignas(16) uint8_t storage[8];
    CComponent components[12];
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
};

// A script array built by hand (one element is kept in place of the pointer).
struct TestArray {
    CArray array;
    uint32_t elements[8];
    TestArray(ESymbolType type, std::initializer_list<uint32_t> values)
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

uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

alignas(16) uint8_t g_ec3Script[0x100];
TestParams g_ec3Own; // the calling script's parameters
uint32_t g_rndCalls;
const bool g_ec3Trace = GetEnvironmentVariableA("THUG2_EC3_TRACE", nullptr, 0) != 0; // each call logged (finding a crash)

// The calling script's address, made position-independent.
uint32_t ScriptRel(uint32_t value)
{
    auto script = reinterpret_cast<uint32_t>(g_ec3Script);
    if (value >= script && value < script + sizeof(g_ec3Script))
        return 0x5C000000u + (value - script);
    auto own = reinterpret_cast<uint32_t>(&g_ec3Own);
    if (value >= own && value < own + sizeof(g_ec3Own))
        return 0x5D000000u + (value - own);
    return value;
}

// The structure methods that change a structure, and the script's other calls (game address and ours).
void StandInStructCalls(StandIns& s)
{
    using AddInt = void (CStruct::*)(uint32_t, int);
    using AddFlt = void (CStruct::*)(uint32_t, float);
    using AddStr = void (CStruct::*)(uint32_t, const char*);
    using AddSum = void (CStruct::*)(uint32_t, uint32_t);
    using AddSt = void (CStruct::*)(uint32_t, CStruct*);
    using AddArr = void (CStruct::*)(uint32_t, CArray*);
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        ++g_rndCalls;
        int n = static_cast<int>(r.args[0]);
        return n > 0 ? static_cast<uint32_t>((g_rndCalls * 7 + 3) % static_cast<uint32_t>(n)) : 0u;
    });
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, ScriptRel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00477860, AddressOf(&CStruct::RemoveFlag)}, 4, [](Regs& r) {
        Record({0x7860, ScriptRel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<AddInt>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, ScriptRel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477C60, AddressOf(static_cast<AddFlt>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, ScriptRel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004779D0, AddressOf(static_cast<AddStr>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, ScriptRel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00477AC0, AddressOf(&CStruct::AddLocalString)}, 8, [](Regs& r) {
        Record({0x7AC0, ScriptRel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<AddSum>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, ScriptRel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478670, AddressOf(static_cast<AddSt>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, ScriptRel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478B00, AddressOf(static_cast<AddArr>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Record({0x8B00, ScriptRel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Record({0x83C0, ScriptRel(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x004747A0, AddressOf(static_cast<void (Script::CScript::*)(uint32_t, CStruct*, Obj::CObject*)>(
                          &Script::CScript::SetScript))},
         12, [](Regs& r) {
             Record({0x47A0, ScriptRel(r.ecx), r.args[0], r.args[1], r.args[2]});
             return 0u;
         });
    s.At({0x0044A480}, 0, [](Regs& r) {
        Record({0xA480, ScriptRel(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        uint32_t argument = r.args[1] < 0x10000 ? r.args[1] : Hash(reinterpret_cast<const char*>(r.args[1])); // (a type or a text)
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), argument});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

// --- batch EC3-1: the script's parameters, random values, arrays, goto, checksums and text -------------------

// The symbol table (empty while the tests run: names in the structures look nothing up).
std::vector<Saved> g_ec3Saved;
Script::CSymbolTableEntry* g_ec3Table[0x1000];
void EmptySymbolTable()
{
    g_ec3Saved = {{0x006F3624, 4, {}}};
    SaveAll(g_ec3Saved);
    std::memset(g_ec3Table, 0, sizeof(g_ec3Table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_ec3Table;
}
// A global integer symbol in the stand-in table.
void AddTestSymbol(Script::CSymbolTableEntry& entry, uint32_t name, Script::ESymbolType type, uint32_t raw)
{
    std::memset(&entry, 0, sizeof(entry));
    entry.unknown0 = 1;
    entry.type = type;
    entry.name = name;
    entry.raw = raw;
    g_ec3Table[name & 0xFFF] = &entry;
}
void RestoreEc3()
{
    RemoveStandIns();
    if (!g_ec3Saved.empty())
        RestoreAll(g_ec3Saved);
    g_ec3Saved.clear();
}

void TestScriptUtilCFuncs()
{
    EmptySymbolTable();
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_rndCalls = 0;
        auto call = [&](uint32_t address, CFunc ours, TestParams& params, void* object = nullptr) {
            std::memset(g_ec3Script, 0, sizeof(g_ec3Script));
            reinterpret_cast<CStruct**>(g_ec3Script)[0x14 / 4] = g_ec3Own.Struct();
            reinterpret_cast<void**>(g_ec3Script)[0xBC / 4] = object;
            auto* script = reinterpret_cast<Script::CScript*>(g_ec3Script);
            if (g_ec3Trace)
                Log("ec3: %08X %s", address, original ? "original" : "ours");
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        using namespace CFuncs;
        TestParams none;
        g_ec3Own.Reset();

        // RemoveParameter / RemoveFlag / AddFlag / GotParam
        {
            TestParams a; a.Checksum(0, 0x1234);
            call(0x0044B7E0, CFunc_RemoveParameter, a);
            call(0x0044B7E0, CFunc_RemoveParameter, none);
            TestParams b; b.Checksum(0, 0x5678);
            call(0x0044B820, CFunc_RemoveFlag, b);
            call(0x0044BFA0, CFunc_AddFlag, b);
            g_ec3Own.Integer(0x1111, 3).Flag(0x2222);
            TestParams c; c.Checksum(0, 0x1111); call(0x0044BD30, CFunc_GotParam, c);
            TestParams d; d.Checksum(0, 0x2222); call(0x0044BD30, CFunc_GotParam, d);
            TestParams e; e.Checksum(0, 0x3333); call(0x0044BD30, CFunc_GotParam, e);
            call(0x0044BD30, CFunc_GotParam, none);
            g_ec3Own.Reset();
        }
        // GetRandomValue: integers (in order, reversed, equal), floats (default and own resolution, reversed)
        {
            TestParams a; a.Checksum(0xA1DC81F9, 0x77).Flag(0xA5F054F4).Integer(0x174841BC, 3).Integer(0x8E411006, 17);
            call(0x0044B850, CFunc_GetRandomValue, a);
            TestParams b; b.Checksum(0xA1DC81F9, 0x78).Flag(0xA5F054F4).Integer(0x174841BC, 40).Integer(0x8E411006, -6);
            call(0x0044B850, CFunc_GetRandomValue, b);
            TestParams c; c.Flag(0xA5F054F4).Integer(0x8E411006, 5);
            call(0x0044B850, CFunc_GetRandomValue, c);
            TestParams d; d.Checksum(0xA1DC81F9, 0x79).Float(0x174841BC, 1.3f).Float(0x8E411006, 7.9f);
            call(0x0044B850, CFunc_GetRandomValue, d);
            TestParams e; e.Checksum(0xA1DC81F9, 0x7A).Float(0x174841BC, 10.1f).Float(0x8E411006, -2.7f).Integer(0x022CF075, 37);
            call(0x0044B850, CFunc_GetRandomValue, e);
            TestParams f; f.Checksum(0xA1DC81F9, 0x7B).Integer(0x174841BC, 2).Float(0x8E411006, 0.3f).Integer(0x022CF075, 3);
            call(0x0044B850, CFunc_GetRandomValue, f);
        }
        // SizeOf
        {
            static TestArray three(Script::ESYMBOLTYPE_INTEGER, {1, 2, 3});
            static TestArray one(Script::ESYMBOLTYPE_NAME, {0xAAAA});
            TestParams a; a.Array(0, three.Get()); call(0x0044B9A0, CFunc_SizeOf, a);
            TestParams b; b.Array(0, one.Get()); call(0x0044B9A0, CFunc_SizeOf, b);
            call(0x0044B9A0, CFunc_SizeOf, none);
        }
        // GetNextArrayElement / GetRandomArrayElement: every element type, the index's edges
        {
            static TestParams inner;
            if (!inner.count)
                inner.Integer(0x4444, 9);
            static TestArray ints(Script::ESYMBOLTYPE_INTEGER, {5, 0xFFFFFFF0u, 77});
            static TestArray floats(Script::ESYMBOLTYPE_FLOAT, {Bits(1.5f), Bits(-2.25f)});
            static TestArray strings(Script::ESYMBOLTYPE_STRING, {reinterpret_cast<uint32_t>("alpha"), reinterpret_cast<uint32_t>("beta")});
            static TestArray locals(Script::ESYMBOLTYPE_LOCALSTRING, {reinterpret_cast<uint32_t>("gamma"), reinterpret_cast<uint32_t>("delta")});
            static TestArray structs(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(inner.Struct()), 0x1234});
            static TestArray arrays(Script::ESYMBOLTYPE_ARRAY, {reinterpret_cast<uint32_t>(ints.Get()), reinterpret_cast<uint32_t>(floats.Get())});
            static TestArray names(Script::ESYMBOLTYPE_NAME, {0xC0FFEE, 0xBEEF, 0xF00D});
            static TestArray vectors(Script::ESYMBOLTYPE_VECTOR, {0x10, 0x20});
            static TestArray empty(Script::ESYMBOLTYPE_INTEGER, {});
            CArray* all[] = {ints.Get(), floats.Get(), strings.Get(), locals.Get(), structs.Get(), arrays.Get(),
                             names.Get(), vectors.Get(), empty.Get()};
            for (CArray* array : all) {
                for (int index : {-5, -1, 0, 1, 2, 7, 1000}) {
                    g_ec3Own.Reset();
                    if (index != 1000)
                        g_ec3Own.Integer(0x7F8C98FE, index);
                    TestParams a; a.Array(0, array);
                    call(0x0044B9E0, CFunc_GetNextArrayElement, a);
                }
                g_ec3Own.Reset();
                TestParams b; b.Array(0, array);
                call(0x0044BBD0, CFunc_GetRandomArrayElement, b);
                call(0x0044BBD0, CFunc_GetRandomArrayElement, b);
            }
            g_ec3Own.Reset();
            call(0x0044B9E0, CFunc_GetNextArrayElement, none);
            call(0x0044BBD0, CFunc_GetRandomArrayElement, none);
            g_ec3Own.Reset();
        }
        // Goto / GotoRandomScript (with and without an object; arrays of the wrong type, empty, one)
        {
            static TestParams inner;
            if (!inner.count)
                inner.Integer(0x4444, 9);
            TestParams a; a.Checksum(0, 0x5C417).Structure(0x7031F10C, inner.Struct());
            call(0x0044BD60, CFunc_Goto, a, reinterpret_cast<void*>(0x0B1EC700));
            TestParams b; b.Checksum(0, 0x5C418);
            call(0x0044BD60, CFunc_Goto, b);
            call(0x0044BD60, CFunc_Goto, none);
            static TestArray names(Script::ESYMBOLTYPE_NAME, {0x51, 0x52, 0x53, 0x54});
            static TestArray one(Script::ESYMBOLTYPE_NAME, {0x61});
            static TestArray ints(Script::ESYMBOLTYPE_INTEGER, {0x71, 0x72});
            static TestArray empty(Script::ESYMBOLTYPE_NAME, {});
            for (CArray* array : {names.Get(), one.Get(), ints.Get(), empty.Get()}) {
                TestParams c; c.Array(0, array);
                call(0x0044BDC0, CFunc_GotoRandomScript, c, reinterpret_cast<void*>(0x0B1EC701));
                call(0x0044BDC0, CFunc_GotoRandomScript, c);
            }
            call(0x0044BDC0, CFunc_GotoRandomScript, none);
            call(0x0044BE20, CFunc_FormatText, a);
            call(0x0044C120, CFunc_IsObjectScript, none);
            call(0x0044C120, CFunc_IsObjectScript, none, reinterpret_cast<void*>(0x0B1EC702));
        }
        // MangleChecksums / AppendSuffixToChecksum / GetUpperCaseString / AppendSuffix
        {
            TestParams a; a.Checksum(0x174841BC, 0x10000001).Checksum(0x8E411006, 0x0F000002);
            call(0x0044BE40, CFunc_MangleChecksums, a);
            TestParams b; b.Integer(0x174841BC, 77).Checksum(0x8E411006, 0xFFFFFFF0u);
            call(0x0044BE40, CFunc_MangleChecksums, b);
            TestParams c; c.Checksum(0x174841BC, 5).Integer(0x8E411006, -9);
            call(0x0044BE40, CFunc_MangleChecksums, c);
            TestParams d; d.Checksum(0x3F4B019E, 0x12345678).String(0x94542EEA, "_Suffix");
            call(0x0044BEC0, CFunc_AppendSuffixToChecksum, d);
            TestParams e; e.String(0, "Mixed case Text 123 xyz");
            call(0x0044BF20, CFunc_GetUpperCaseString, e);
            // AppendSuffix: the defaults; a long text cut to the limit; a number that just fits; another limit
            TestParams f; f.String(0xC4745838, "Name");
            call(0x0044BFD0, CFunc_AppendSuffix, f);
            TestParams g; g.String(0xC4745838, "AVeryLongNameIndeed").Integer(0x4A4F7821, 123).Checksum(0x50060694, 0xD00D);
            call(0x0044BFD0, CFunc_AppendSuffix, g);
            TestParams h; h.String(0xC4745838, "Twelve_Chars").Integer(0x4A4F7821, 456).Checksum(0x50060694, 0xD00E);
            call(0x0044BFD0, CFunc_AppendSuffix, h);
            TestParams i; i.String(0xC4745838, "Thirteen_Char").Integer(0x4A4F7821, -45).Integer(0xA6931595, 8);
            call(0x0044BFD0, CFunc_AppendSuffix, i);
            TestParams j; j.Integer(0x4A4F7821, 99999).Integer(0xA6931595, 5);
            call(0x0044BFD0, CFunc_AppendSuffix, j);
        }
        traces[run] = g_trace;
    }
    RestoreEc3();
    Check(traces[0].size() > 600, "script utility commands: the original reached its calls");
    Compare(traces[0], traces[1], "script utility commands (0x0044B7E0-0x0044C120)");
}

// --- batch EC3-2: ForEachIn, array building and changing, printf text, Dump ----------------------------------

// Memory the pools and arrays hand out (the same addresses in both runs: reset before each).
alignas(16) uint8_t g_ec3Arena[0x8000];
uint32_t g_ec3ArenaUsed;
alignas(16) uint8_t g_ec3Pools[3][0x40]; // stand-in pools: structures, arrays, components
char g_ec3Strings[0x400];               // CreateString's copies
uint32_t g_ec3StringsUsed;
bool g_formatOk;
TestParams g_globalStruct;
CComponent g_printfText;

void* ArenaBlock(uint32_t size)
{
    uint8_t* block = g_ec3Arena + g_ec3ArenaUsed;
    g_ec3ArenaUsed += (size + 15) & ~15u;
    std::memset(block, 0xCD, size);
    return block;
}

// An array's contents (strings by their text).
void RecordArray(CArray* array)
{
    if (!array) {
        Record({0xA0A0});
        return;
    }
    Record({0xA0A1, array->m_type, array->m_size});
    for (uint32_t i = 0; i < array->m_size && i < 32; ++i) {
        uint32_t value = array->m_size == 1 ? array->m_single : array->m_elements[i];
        Record({array->m_type == Script::ESYMBOLTYPE_STRING ? Hash(reinterpret_cast<const char*>(value)) : value});
    }
}

void StandInArrayCalls(StandIns& s)
{
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        uint32_t which = (r.ecx - reinterpret_cast<uint32_t>(g_ec3Pools)) / 0x40;
        Record({0xA3D0, which});
        return reinterpret_cast<uint32_t>(ArenaBlock(which == 2 ? 16 : which == 1 ? 12 : 8));
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return 0u;
    });
    s.At({0x00408FD0, AddressOf(&Script::DeleteArray)}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        return 0u;
    });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Record({0x8480, ScriptRel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, ScriptRel(r.ecx), ScriptRel(r.args[0])});
        return 0u;
    });
    using RunFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, r.ecx, r.args[0], r.args[1]});
        auto* array = reinterpret_cast<CArray*>(r.ecx);
        array->m_type = static_cast<ESymbolType>(r.args[1]);
        array->m_size = r.args[0];
        if (r.args[0] > 1) {
            array->m_elements = static_cast<uint32_t*>(ArenaBlock(r.args[0] * 4));
            std::memset(array->m_elements, 0, r.args[0] * 4);
        } else {
            array->m_single = 0;
        }
        return 0u;
    });
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        auto* dest = reinterpret_cast<CArray*>(r.args[0]);
        auto* source = reinterpret_cast<CArray*>(r.args[1]);
        Record({0x8A60, r.args[0], r.args[1]});
        dest->m_type = source->m_type;
        dest->m_size = source->m_size;
        if (source->m_size > 1) {
            dest->m_elements = static_cast<uint32_t*>(ArenaBlock(source->m_size * 4));
            std::memcpy(dest->m_elements, source->m_elements, source->m_size * 4);
        } else {
            dest->m_single = source->m_single;
        }
        return 0u;
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        Record({0x7140, r.args[0]});
        if (r.args[0] - reinterpret_cast<uint32_t>(g_ec3Arena) < sizeof(g_ec3Arena)) // (one the command made)
            RecordArray(reinterpret_cast<CArray*>(r.args[0]));
        return 0u;
    });
    s.At({0x0046CAB0, AddressOf(&CArray::Clear)}, 0, [](Regs& r) {
        Record({0xCAB0, r.ecx});
        return 0u;
    });
    s.At({0x004780D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, CArray*)>(&CStruct::AddArrayPointer))}, 8,
         [](Regs& r) {
             Record({0x80D0, ScriptRel(r.ecx), r.args[0], r.args[1]});
             RecordArray(reinterpret_cast<CArray*>(r.args[1]));
             return 0u;
         });
    s.At({0x004781B0, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructurePointer))},
         8, [](Regs& r) {
             Record({0x81B0, r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return reinterpret_cast<uint32_t>(g_globalStruct.Struct());
    });
    s.At({0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 0, [](Regs& r) {
        Record({0xD110, r.args[0]});
        return r.args[0] == 0x1111 ? reinterpret_cast<uint32_t>("speed") : reinterpret_cast<uint32_t>("0x....");
    });
    s.At({0x004773E0, AddressOf(&Script::CopyComponent)}, 0, [](Regs& r) {
        auto* dest = reinterpret_cast<CComponent*>(r.args[0]);
        auto* source = reinterpret_cast<CComponent*>(r.args[1]);
        Record({0x73E0, r.args[0], r.args[1], r.args[2]});
        dest->typeAndFlag = source->typeAndFlag;
        dest->name = source->name;
        dest->raw = source->raw;
        return 0u;
    });
    s.At({0x00477920, AddressOf(&CStruct::AddComponent)}, 4, [](Regs& r) {
        auto* c = reinterpret_cast<CComponent*>(r.args[0]);
        Record({0x7920, r.ecx, r.args[0], c->typeAndFlag, c->name, c->raw});
        return 0u;
    });
    s.At({0x00476340, AddressOf(&Script::FreeString)}, 0, [](Regs& r) {
        Record({0x6340, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004763A0, AddressOf(&Script::CreateString)}, 0, [](Regs& r) {
        auto* text = reinterpret_cast<const char*>(r.args[0]);
        Record({0x63A0, Hash(text)});
        char* copy = g_ec3Strings + g_ec3StringsUsed;
        size_t length = std::strlen(text);
        std::memcpy(copy, text, length + 1);
        g_ec3StringsUsed += static_cast<uint32_t>(length + 1);
        return reinterpret_cast<uint32_t>(copy);
    });
    s.At({0x0044C1D0, AddressOf(&Script::FreePair)}, 0, [](Regs& r) {
        Record({0xC1D0, r.args[0]});
        return 0u;
    });
    s.At({0x0044C230, AddressOf(&Script::FreeVector)}, 0, [](Regs& r) {
        Record({0xC230, r.args[0]});
        return 0u;
    });
    s.At({0x0044C170, AddressOf(&Script::FreeComponent)}, 0, [](Regs& r) {
        auto* c = reinterpret_cast<CComponent*>(r.args[0]);
        Record({0xC170, r.args[0], c->typeAndFlag, c->name, c->raw});
        return 0u;
    });
    // FormatText (0x0044A480): fails, or leaves PrintfText="formatted" in the structure
    s.At({0x0044A480}, 0, [](Regs& r) {
        Record({0xA480, r.args[0], r.args[1]});
        if (g_formatOk) {
            std::memset(&g_printfText, 0, sizeof(g_printfText));
            g_printfText.typeAndFlag = Script::ESYMBOLTYPE_STRING << 1;
            g_printfText.name = 0xBC4AC08B;
            g_printfText.string = "formatted text";
            reinterpret_cast<CComponent**>(r.args[0])[1] = &g_printfText;
        }
        return g_formatOk ? 1u : 0u;
    });
}

void TestScriptArrayCFuncs()
{
    EmptySymbolTable();
    // the pool pairs (structures, arrays, components): our stand-in pools
    g_ec3Saved.push_back({0x006F3614, 16, {}});
    g_ec3Saved.push_back({0x006F22B4, 16, {}});
    g_ec3Saved.push_back({0x006F2338, 16, {}});
    for (size_t i = 1; i < g_ec3Saved.size(); ++i) {
        Saved& s = g_ec3Saved[i];
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    const uint32_t pairs[] = {0x006F3614, 0x006F22B4, 0x006F2338};
    for (int i = 0; i < 3; ++i) {
        auto* pair = reinterpret_cast<uint32_t*>(pairs[i]);
        pair[0] = 0;
        pair[1] = 0;
        pair[2] = reinterpret_cast<uint32_t>(g_ec3Pools[i]);
        pair[3] = 0;
    }
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInArrayCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_rndCalls = 0;
        g_ec3ArenaUsed = 0;
        g_ec3StringsUsed = 0;
        std::memset(g_ec3Arena, 0, sizeof(g_ec3Arena));
        auto call = [&](uint32_t address, CFunc ours, TestParams& params, void* object = nullptr) {
            std::memset(g_ec3Script, 0, sizeof(g_ec3Script));
            reinterpret_cast<CStruct**>(g_ec3Script)[0x14 / 4] = g_ec3Own.Struct();
            reinterpret_cast<void**>(g_ec3Script)[0xBC / 4] = object;
            auto* script = reinterpret_cast<Script::CScript*>(g_ec3Script);
            if (g_ec3Trace)
                Log("ec3: %08X %s", address, original ? "original" : "ours");
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u, g_ec3ArenaUsed});
        };
        using namespace CFuncs;
        TestParams none;
        g_ec3Own.Reset();

        // ForEachIn: with params (structure elements, a null one), without (elements as the parameters), empty
        {
            static TestParams inner, extra;
            if (!inner.count) {
                inner.Integer(0x4444, 9);
                extra.Integer(0x5555, 1);
            }
            static TestArray structs(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(inner.Struct()), 0, 0x77});
            static TestArray empty(Script::ESYMBOLTYPE_STRUCTURE, {});
            TestParams a; a.Checksum(0x62BA3F6A, 0xD0D0).Array(0, structs.Get()).Structure(0x7031F10C, extra.Struct());
            call(0x0044C290, CFunc_ForEachIn, a, reinterpret_cast<void*>(0x0B1EC700));
            TestParams b; b.Checksum(0x62BA3F6A, 0xD0D1).Array(0, structs.Get());
            call(0x0044C290, CFunc_ForEachIn, b, reinterpret_cast<void*>(0x0B1EC701));
            TestParams c; c.Checksum(0x62BA3F6A, 0xD0D2).Array(0, empty.Get()).Structure(0x7031F10C, extra.Struct());
            call(0x0044C290, CFunc_ForEachIn, c);
            TestParams d; d.Array(0, empty.Get());
            call(0x0044C290, CFunc_ForEachIn, d);
        }
        // GetElement: each supported type, an unsupported one, the index's edges, no array
        {
            static TestParams inner;
            if (!inner.count)
                inner.Integer(0x4444, 9);
            static TestArray ints(Script::ESYMBOLTYPE_INTEGER, {5, 6, 7});
            static TestArray structs(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(inner.Struct()), 0x1234});
            static TestArray arrays(Script::ESYMBOLTYPE_ARRAY, {reinterpret_cast<uint32_t>(ints.Get()), 0x5678});
            static TestArray names(Script::ESYMBOLTYPE_NAME, {0xAB, 0xCD});
            for (CArray* array : {ints.Get(), structs.Get(), arrays.Get(), names.Get()}) {
                for (int index : {-1, 0, 1, 2, 3}) {
                    TestParams a; a.Array(0, array).Integer(0x7F8C98FE, index); // ("Index")
                    call(0x0044C430, CFunc_GetElement, a);
                }
            }
            TestParams b; b.Integer(0x7F8C98FE, 0);
            call(0x0044C430, CFunc_GetElement, b);
            TestParams c; c.Array(0, ints.Get());
            call(0x0044C430, CFunc_GetElement, c);
        }
        // PermuteArray: a copy, in place, the first-differs rule (forced to apply), no name, no array, one element
        {
            static TestArray source(Script::ESYMBOLTYPE_INTEGER, {11, 22, 33, 44, 55, 66});
            TestArray inPlace(Script::ESYMBOLTYPE_NAME, {1, 2, 3, 4});
            TestArray same(Script::ESYMBOLTYPE_INTEGER, {9, 9, 9});
            static TestArray one(Script::ESYMBOLTYPE_INTEGER, {42});
            static TestArray two(Script::ESYMBOLTYPE_INTEGER, {7, 8});
            static TestArray emptyArray(Script::ESYMBOLTYPE_INTEGER, {});
            auto permute = [&](CArray* array, bool sourceFlag, bool differ, uint32_t newName) {
                TestParams p;
                p.Array(0x5EF31148, array); // "Array"
                if (sourceFlag)
                    p.Flag(0xDD311E71); // "PermuteSource"
                if (differ)
                    p.Flag(0x282676C6); // "MakeNewFirstDifferFromOldLast"
                if (newName)
                    p.Checksum(0xE6C81F4E, newName); // "NewArrayName"
                call(0x0044C5A0, CFunc_PermuteArray, p);
            };
            permute(source.Get(), false, false, 0xC0B1);
            permute(source.Get(), false, true, 0xC0B2);
            permute(same.Get(), false, true, 0xC0B3);
            permute(two.Get(), false, true, 0xC0B4);
            permute(two.Get(), false, true, 0xC0B4);
            static TestArray twoSame(Script::ESYMBOLTYPE_INTEGER, {5, 5});
            permute(twoSame.Get(), false, true, 0xC0B9);
            static TestArray lastTwice(Script::ESYMBOLTYPE_INTEGER, {3, 1, 3, 2, 3});
            static TestArray lastMostly(Script::ESYMBOLTYPE_INTEGER, {4, 4, 7, 4});
            for (int k = 0; k < 6; ++k) {
                permute(lastTwice.Get(), false, true, 0xC0BA);
                permute(lastMostly.Get(), false, true, 0xC0BB);
            }
            permute(one.Get(), false, true, 0xC0B5);
            permute(emptyArray.Get(), false, true, 0xC0B6);
            permute(source.Get(), false, false, 0);
            permute(inPlace.Get(), true, true, 0);
            RecordArray(inPlace.Get());
            permute(same.Get(), true, true, 0xC0B7);
            RecordArray(same.Get());
            permute(nullptr, false, false, 0xC0B8);
        }
        // GotoPreserveParams
        {
            static TestParams extra;
            if (!extra.count)
                extra.Integer(0x5555, 1);
            TestParams a; a.Checksum(0, 0x5C417).Structure(0x7031F10C, extra.Struct());
            call(0x0044C730, CFunc_GotoPreserveParams, a, reinterpret_cast<void*>(0x0B1EC703));
            TestParams b; b.Checksum(0, 0x5C418);
            call(0x0044C730, CFunc_GotoPreserveParams, b);
        }
        // FormatPrintfText / ScriptAssert: a string (formatting works / fails), an integer, neither
        {
            using PrintfFn = bool(__cdecl*)(char*, CStruct*, Script::CScript*);
            auto printfText = [&](TestParams& p) {
                char out[0x100];
                std::memset(out, 0x5A, sizeof(out));
                out[sizeof(out) - 1] = 0;
                auto* script = reinterpret_cast<Script::CScript*>(g_ec3Script);
                bool result = original ? Original<PrintfFn>(0x0044C840)(out, p.Struct(), script)
                                       : FormatPrintfText(out, p.Struct(), script);
                Record({0xC840, result ? 1u : 0u, Hash(out), g_ec3ArenaUsed});
            };
            TestParams a; a.String(0, "score: %d").Integer(0x1234, 5);
            g_formatOk = true;
            printfText(a);
            g_formatOk = false;
            printfText(a);
            TestParams b; b.Integer(0, -77);
            printfText(b);
            printfText(none);
            g_formatOk = true;
            call(0x0044CB70, CFunc_ScriptAssert, a);
            call(0x0044CB70, CFunc_ScriptAssert, b);
            g_formatOk = false;
        }
        // Dump: a parameter there and not
        {
            g_ec3Own.Reset();
            g_ec3Own.Integer(0x1111, 3).Float(0x2222, 2.5f);
            TestParams a; a.Checksum(0, 0x1111); call(0x0044C9B0, CFunc_Dump, a);
            TestParams b; b.Checksum(0, 0x2222); call(0x0044C9B0, CFunc_Dump, b);
            TestParams c; c.Checksum(0, 0x3333); call(0x0044C9B0, CFunc_Dump, c);
            g_ec3Own.Reset();
        }
        // CreateIndexArray / AppendStruct / StringToCharArray
        {
            for (int size : {0, 1, 5}) {
                TestParams a; a.Integer(0, size); call(0x0044CBC0, CFunc_CreateIndexArray, a);
            }
            static TestParams extra;
            if (!extra.count)
                extra.Integer(0x5555, 1);
            TestParams b; b.Checksum(0x456D28D1, 0x6106A1).Checksum(0xA40ABAA7, 0xF1E1D).Structure(0x7031F10C, extra.Struct());
            call(0x0044CCC0, CFunc_AppendStruct, b);
            TestParams c; c.String(0x61414D56, "Hello!"); call(0x0044CDB0, CFunc_StringToCharArray, c);
            TestParams d; d.String(0x61414D56, "x"); call(0x0044CDB0, CFunc_StringToCharArray, d);
            TestParams e; e.String(0x61414D56, ""); call(0x0044CDB0, CFunc_StringToCharArray, e);
        }
        // SetArrayElement: every array type, float arrays given an integer, a name resolving to nothing
        {
            TestArray ints(Script::ESYMBOLTYPE_INTEGER, {1, 2, 3});
            TestArray floats(Script::ESYMBOLTYPE_FLOAT, {Bits(1.0f), Bits(2.0f)});
            TestArray strings(Script::ESYMBOLTYPE_STRING, {reinterpret_cast<uint32_t>("a"), reinterpret_cast<uint32_t>("b")});
            TestArray locals(Script::ESYMBOLTYPE_LOCALSTRING, {reinterpret_cast<uint32_t>("c"), reinterpret_cast<uint32_t>("d")});
            TestArray pairArray(Script::ESYMBOLTYPE_PAIR, {0x1001, 0x1002});
            TestArray vectors(Script::ESYMBOLTYPE_VECTOR, {0x2001, 0x2002});
            TestArray structs(Script::ESYMBOLTYPE_STRUCTURE, {0x3001, 0});
            TestArray arrays(Script::ESYMBOLTYPE_ARRAY, {0, 0x4002});
            TestArray names(Script::ESYMBOLTYPE_NAME, {0x5001, 0x5002});
            TestArray others(Script::ESYMBOLTYPE_QSCRIPT, {0x6001, 0x6002});
            CArray* all[] = {ints.Get(), floats.Get(), strings.Get(), locals.Get(), pairArray.Get(), vectors.Get(),
                             structs.Get(), arrays.Get(), names.Get(), others.Get()};
            int which = 0;
            for (CArray* array : all) {
                for (int index : {0, 1}) {
                    g_ec3Own.Reset();
                    g_ec3Own.Array(0xA77A0000u + which, array);
                    TestParams p;
                    p.Checksum(0x8F61D602, 0xA77A0000u + which).Integer(0x7F8C98FE, index);
                    bool stringArray = array->m_type == Script::ESYMBOLTYPE_STRING ||
                                   array->m_type == Script::ESYMBOLTYPE_LOCALSTRING;
                    if (stringArray)
                        p.String(0x4B40630D, index ? "second" : "first");
                    else if (index == 0)
                        p.Integer(0x4B40630D, 1234 + which);
                    else
                        p.Float(0x4B40630D, -0.75f - which).Integer(0x4B40630D, 99); // (the last one counts)
                    call(0x0044CEC0, CFunc_SetArrayElement, p);
                    RecordArray(array);
                }
                ++which;
            }
            // a name naming a global: resolved to its value
            static Script::CSymbolTableEntry symbol;
            AddTestSymbol(symbol, 0x0ABC0123, Script::ESYMBOLTYPE_INTEGER, 555);
            {
                g_ec3Own.Reset();
                g_ec3Own.Array(0xA77A0000u, ints.Get());
                TestParams r;
                r.Checksum(0x8F61D602, 0xA77A0000u).Integer(0x7F8C98FE, 2).Checksum(0x4B40630D, 0x0ABC0123);
                call(0x0044CEC0, CFunc_SetArrayElement, r);
                RecordArray(ints.Get());
            }
            g_ec3Table[0x123] = nullptr;
            TestParams q;
            g_ec3Own.Reset();
            g_ec3Own.Array(0xA77A0000u, floats.Get());
            q.Checksum(0x8F61D602, 0xA77A0000u).Integer(0x7F8C98FE, 1).Float(0x4B40630D, 6.5f);
            call(0x0044CEC0, CFunc_SetArrayElement, q);
            RecordArray(floats.Get());
            g_ec3Own.Reset();
        }
        traces[run] = g_trace;
    }
    RestoreEc3();
    Check(traces[0].size() > 800, "script array commands: the original reached its calls");
    Compare(traces[0], traces[1], "script array commands (0x0044C290-0x0044CEC0)");
}

// --- batch EC3-3: the music and stream commands -------------------------------------------------------------

uint32_t g_pcmResult;        // what the stood-in Pcm functions return (varied between calls)
uint64_t g_tracksOff[2];     // GetTracksOff's masks
int g_numTracks;

// Every Pcm function a command calls: the game's address and ours, recording its `count` arguments.
void StandInPcmCalls(StandIns& s)
{
    struct Entry {
        uint32_t address;
        uint32_t ours;
        int count;
    };
    const Entry entries[] = {
        {0x00450500, AddressOf(&Pcm::StreamExists), 1},
        {0x00451B80, AddressOf(&Pcm::AddStream), 3},
        {0x00450AE0, AddressOf(&Pcm::RemoveStream), 3},
        {0x004506D0, AddressOf(&Pcm::StopStream), 1},
        {0x00450690, AddressOf(static_cast<void(__cdecl*)(int)>(&Pcm::StopStreams)), 1},
        {0x00451660, AddressOf(&Pcm::IsStreamPlaying), 1},
        {0x00450650, AddressOf(&Pcm::StopMusic), 0},
        {0x004520D0, AddressOf(&Pcm::PlayTrack), 2},
        {0x00450530, AddressOf(&Pcm::SetCurrentTrack), 1},
        {0x00450EF0, AddressOf(&Pcm::PlayMusicStream), 2},
        {0x004516A0, AddressOf(&Pcm::LoadStreamFrameAmp), 1},
        {0x00451720, AddressOf(&Pcm::FreeStreamFrameAmp), 1},
        {0x004510B0, AddressOf(static_cast<void(__cdecl*)(float)>(&Pcm::SetMusicVolume)), 1},
        {0x00451130, AddressOf(&Pcm::SetMusicStreamVolume), 1},
        {0x00450F70, AddressOf(&Pcm::PauseMusic), 1},
        {0x00451010, AddressOf(&Pcm::PauseStream), 1},
        {0x004511A0, AddressOf(&Pcm::AddTrackToPlaylist), 3},
        {0x00451C70, AddressOf(&Pcm::ChangeTrackState), 3},
        {0x004512D0, AddressOf(&Pcm::ClearPlaylist), 1},
        {0x00451350, AddressOf(&Pcm::SkipMusicTrack), 0},
        {0x004515A0, AddressOf(&Pcm::SetMusicMode), 1},
        {0x00451380, AddressOf(&Pcm::SetRandomMode), 1},
        {0x00450470, AddressOf(&Pcm::SetMusicLooping), 1},
        {0x00450940, AddressOf(&Pcm::PreLoadStreamDone), 1},
        {0x004503D0, AddressOf(&Pcm::UseUserSoundtrack), 1},
        {0x00450410, AddressOf(&Pcm::UseStandardSoundtrack), 0},
    };
    for (const Entry& e : entries) {
        uint32_t tag = e.address & 0xFFFF;
        int count = e.count;
        s.At({e.address, e.ours}, 0, [tag, count](Regs& r) {
            Record({tag});
            for (int i = 0; i < count; ++i) {
                uint32_t value = r.args[i];
                // (strings by their text; bools by their low byte)
                if (tag == 0x11A0 && i != 1)
                    value = Hash(reinterpret_cast<const char*>(value));
                else if (tag == 0x20D0 && i == 0)
                    value = Hash(reinterpret_cast<const char*>(value));
                else if ((tag == 0x20D0 && i == 1) || (tag == 0x1C70 && i == 1) || tag == 0x0470)
                    value &= 0xFF;
                Record({value});
            }
            return g_pcmResult;
        });
    }
    s.At({0x00451500, AddressOf(&Pcm::GetTrackName)}, 0, [](Regs& r) {
        Record({0x1500, r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>(r.args[0] == 3 ? "track_three.ogg" : "Song");
    });
    s.At({0x004514C0, AddressOf(&Pcm::GetNumTracks)}, 0, [](Regs& r) {
        Record({0x14C0, r.args[0]});
        return static_cast<uint32_t>(g_numTracks);
    });
    s.At({0x00450480, AddressOf(&Pcm::GetTracksOff)}, 0, [](Regs& r) {
        Record({0x0480});
        *reinterpret_cast<uint64_t*>(r.args[0]) = g_tracksOff[0];
        *reinterpret_cast<uint64_t*>(r.args[1]) = g_tracksOff[1];
        return 0u;
    });
}

void TestMusicCFuncs()
{
    EmptySymbolTable();
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInPcmCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) {
            std::memset(g_ec3Script, 0, sizeof(g_ec3Script));
            reinterpret_cast<CStruct**>(g_ec3Script)[0x14 / 4] = g_ec3Own.Struct();
            auto* script = reinterpret_cast<Script::CScript*>(g_ec3Script);
            if (g_ec3Trace)
                Log("ec3: %08X %s", address, original ? "original" : "ours");
            for (uint32_t result : {0x00000000u, 0x00000001u, 0xFFFFFF00u, 0x00ABC000u}) {
                g_pcmResult = result;
                bool returned = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
                Record({0xC0DE, address, returned ? 1u : 0u});
            }
        };
        using namespace CFuncs;
        TestParams none;
        g_ec3Own.Reset();
        // the stream commands: with and without their names
        {
            TestParams a; a.Checksum(0, 0x5EA);
            TestParams b; b.Checksum(0, 0x5EB).Checksum(0x02E58C18, 0xADD);
            TestParams c; c.Checksum(0x02E58C18, 0xADE);
            TestParams d; d.Checksum(0, 0x5EC).Checksum(0x977FE2CF, 0x4E3);
            TestParams e; e.Checksum(0x977FE2CF, 0x4E4);
            for (TestParams* p : {&none, &a, &b, &c, &d, &e}) {
                call(0x0044D100, CFunc_StreamExists, *p);
                call(0x0044D140, CFunc_AddStream, *p);
                call(0x0044D1B0, CFunc_RemoveStream, *p);
                call(0x0044D220, CFunc_StopStream, *p);
                call(0x0044D260, CFunc_IsStreamPlaying, *p);
                call(0x0044D3E0, CFunc_LoadStreamFrameAmp, *p);
                call(0x0044D420, CFunc_FreeStreamFrameAmp, *p);
                if (p == &a || p == &b || p == &d) // (without a name the original passes an uninitialised id)
                    call(0x0044D770, CFunc_PreLoadStreamDone, *p);
            }
            TestParams f; f.Checksum(0, 0x3051).Float(0xF6A36814, 72.5f);
            for (TestParams* p : {&none, &a, &f})
                call(0x0044D380, CFunc_PlayMusicStream, *p);
        }
        // PlayTrack: a file (looping), a track number, nothing
        {
            TestParams a; a.String(0, "music\\vag\\songs\\menu").Integer(0x5EA0E211, 2);
            TestParams b; b.Integer(0, 3);
            TestParams c; c.Integer(0, 7).Integer(0x5EA0E211, 0);
            for (TestParams* p : {&a, &b, &c, &none})
                call(0x0044D2A0, CFunc_PlayTrack, *p);
        }
        // the volume and mode commands: the defaults and given values
        {
            TestParams a; a.Float(0, 33.25f);
            TestParams b; b.Integer(0, 0);
            TestParams c; c.Integer(0, -1);
            TestParams d; d.Integer(0, 5);
            for (TestParams* p : {&none, &a, &b, &c, &d}) {
                call(0x0044D460, CFunc_SetMusicVolume, *p);
                call(0x0044D490, CFunc_SetMusicStreamVolume, *p);
                call(0x0044D4D0, CFunc_PauseMusic, *p);
                call(0x0044D500, CFunc_PauseStream, *p);
                call(0x0044D6D0, CFunc_SetMusicMode, *p);
                call(0x0044D710, CFunc_SetRandomMode, *p);
                call(0x0044D740, CFunc_SetMusicLooping, *p);
                if (p != &none && p != &a) // (without an integer the original passes an uninitialised one)
                    call(0x0044D790, CFunc_UseUserSoundtrack, *p);
                call(0x0044D5B0, CFunc_ChangeTrackState, *p);
            }
            TestParams e; e.Integer(0, 12).Flag(0xD443A2BC); // Off
            call(0x0044D5B0, CFunc_ChangeTrackState, e);
            call(0x0044D4C0, CFunc_StopMusic, none);
            call(0x0044D6C0, CFunc_SkipMusicTrack, none);
            call(0x0044D7C0, CFunc_UseStandardSoundtrack, none);
        }
        // AddMusicTrack / ClearMusicTrackList: with and without the playlist-0 flag, title, file
        {
            TestParams a; a.String(0, "music\\vag\\songs\\one").String(0x14BADF8A, "One");
            TestParams b; b.String(0, "two").Flag(0x389129E4);
            TestParams c; c.String(0x14BADF8A, "No file");
            for (TestParams* p : {&none, &a, &b, &c}) {
                call(0x0044D530, CFunc_AddMusicTrack, *p);
                call(0x0044D690, CFunc_ClearMusicTrackList, *p);
            }
        }
        // ChangeTrackState Off / TrackEnabled over the masks' edges
        {
            g_tracksOff[0] = 0x8000000000000005ull;
            g_tracksOff[1] = 0x0000000180000003ull;
            for (int tracks : {0, 9}) {
                g_numTracks = tracks;
                for (int track : {0, 1, 2, 31, 32, 63, 64, 65, 95, 96, 127, 128, 200, 320, -1, -64, -200, -126, 194}) {
                    TestParams p; p.Integer(0, track);
                    call(0x0044D5F0, CFunc_TrackEnabled, p);
                }
            }
        }
        traces[run] = g_trace;
    }
    RestoreEc3();
    Check(traces[0].size() > 1500, "music commands: the original reached its calls");
    Compare(traces[0], traces[1], "music and stream commands (0x0044D100-0x0044D7C0)");
}

} // namespace

SELFTEST(Early, TestScriptUtilCFuncs, RestoreEc3);
SELFTEST(Early, TestScriptArrayCFuncs, RestoreEc3);
SELFTEST(Early, TestMusicCFuncs, RestoreEc3);
