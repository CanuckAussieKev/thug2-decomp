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
#include "game/mth/random.h"
#include "game/script/cscript.h"
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
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

// --- batch EC3-1: the script's parameters, random values, arrays, goto, checksums and text -------------------

// The symbol table (empty while the tests run: names in the structures look nothing up).
std::vector<Saved> g_ec3Saved;
void EmptySymbolTable()
{
    g_ec3Saved = {{0x006F3624, 4, {}}};
    SaveAll(g_ec3Saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
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

} // namespace

SELFTEST(Early, TestScriptUtilCFuncs, RestoreEc3);
