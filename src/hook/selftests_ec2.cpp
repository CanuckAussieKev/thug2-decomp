// Self-tests for the engine's script commands 0x004489B0-0x0044B7E0 (maths, debugging, objects, script
// utilities, arrays): each original against ours on identical stand-in state; the structure getters run for real on
// hand-built parameters, and every other game function the commands call is stood in for by recording code (over
// the game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/script/cscript.h"
#include "game/script/debug_cfuncs.h"
#include "game/script/math_cfuncs.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CComponent;
using Script::CStruct;
using Script::ESymbolType;

// A script structure built by hand (no memory manager needed): components chained in the order added.
struct TestParams {
    alignas(16) uint8_t storage[8];
    CComponent components[16];
    Script::CVector vectors[4];
    Script::CPair pairs[4];
    int count = 0;
    int vectorCount = 0;
    int pairCount = 0;
    TestParams()
    {
        std::memset(storage, 0, sizeof(storage));
        std::memset(components, 0, sizeof(components));
        std::memset(vectors, 0, sizeof(vectors));
        std::memset(pairs, 0, sizeof(pairs));
    }
    TestParams(const TestParams&) = delete;
    TestParams& operator=(const TestParams&) = delete;
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
    TestParams& LocalString(uint32_t name, const char* value) { return Add(Script::ESYMBOLTYPE_LOCALSTRING, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Structure(uint32_t name, CStruct* value) { return Add(Script::ESYMBOLTYPE_STRUCTUREPOINTER, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Array(uint32_t name, Script::CArray* value) { return Add(Script::ESYMBOLTYPE_ARRAYPOINTER, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Vector(uint32_t name, float x, float y, float z)
    {
        Script::CVector& v = vectors[vectorCount++];
        v.x = x;
        v.y = y;
        v.z = z;
        return Add(Script::ESYMBOLTYPE_VECTOR, name, reinterpret_cast<uint32_t>(&v));
    }
    TestParams& Pair(uint32_t name, float x, float y)
    {
        Script::CPair& p = pairs[pairCount++];
        p.x = x;
        p.y = y;
        return Add(Script::ESYMBOLTYPE_PAIR, name, reinterpret_cast<uint32_t>(&p));
    }
};

uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

// The calling script and its parameters (only their address is used: every change to them is stood in for).
alignas(16) uint8_t g_script[0x100];
alignas(16) uint8_t g_results[0x40];
// DisplayFreeMem's stand-in heaps, regions and the structure it makes.
alignas(16) uint8_t g_heaps[2][sizeof(Mem::Heap)];
alignas(16) uint8_t g_regions[2][sizeof(Mem::Region)];
alignas(16) uint8_t g_newStruct[0x10];
alignas(16) uint8_t g_fakePool[sizeof(Mem::Pool)];
bool g_poolEmpty;
int g_freeMem;

// Addresses of the stand-in objects, made position-independent.
uint32_t Rel(uint32_t value)
{
    struct Range {
        const void* base;
        uint32_t size;
        uint32_t tag;
    };
    const Range ranges[] = {{g_script, sizeof(g_script), 0xB0000000u},  {g_results, sizeof(g_results), 0xB1000000u},
                            {g_heaps, sizeof(g_heaps), 0xB2000000u},    {g_regions, sizeof(g_regions), 0xB3000000u},
                            {g_newStruct, sizeof(g_newStruct), 0xB4000000u}, {g_fakePool, sizeof(g_fakePool), 0xB5000000u}};
    for (const Range& r : ranges) {
        auto base = reinterpret_cast<uint32_t>(r.base);
        if (value >= base && value < base + r.size)
            return r.tag + (value - base);
    }
    return value;
}

template <typename R, typename... A>
using Method = R (CStruct::*)(A...);

double __cdecl AsinForBoth(double x) { return std::asin(x); }
double __cdecl AcosForBoth(double x) { return std::acos(x); }
// The C runtime's _CIasin / _CIacos (argument and result in ST0), answered by std::asin / std::acos for both runs.
__declspec(naked) void CIasinStandIn()
{
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call AsinForBoth
        add esp, 8
        ret
    }
}
__declspec(naked) void CIacosStandIn()
{
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call AcosForBoth
        add esp, 8
        ret
    }
}

// The structure changes the commands make to the script's parameters (and DisplayFreeMem's), recorded.
void StandInStructChanges(StandIns& s)
{
    s.At({0x00477C60, AddressOf(static_cast<Method<void, uint32_t, float>>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0xADDF, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<Method<void, uint32_t, int>>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0xADD1, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<Method<void, uint32_t, uint32_t>>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0xADDC, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477F70, AddressOf(static_cast<Method<void, uint32_t, Script::VectorValue>>(&CStruct::AddVector))}, 20,
         [](Regs& r) {
             Record({0xADD5, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
             return 0u;
         });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA, Rel(r.ecx)});
        return 0u;
    });
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

// Runs one command (the original or ours) on `params` with the stand-in script; records its result.
void Call(bool original, uint32_t address, CFunc ours, TestParams& params)
{
    std::memset(g_script, 0, sizeof(g_script));
    auto* script = reinterpret_cast<Script::CScript*>(g_script);
    script->m_params = reinterpret_cast<CStruct*>(g_results);
    bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
    Record({0xC0DE, address, result ? 1u : 0u});
}

// --- maths, DisplayFreeMem, Test (0x004489B0-0x00449110) ----------------------------------------------------------

void StandInMathCalls(StandIns& s)
{
    StandInStructChanges(s);
    s.Bytes(0x00623560, Branch(0xE9, 0x00623560, reinterpret_cast<void*>(&CIasinStandIn)));
    s.Bytes(0x00623630, Branch(0xE9, 0x00623630, reinterpret_cast<void*>(&CIacosStandIn)));
    s.At({0x005BAC10, AddressOf(&Mem::Manager::FirstHeap)}, 0, [](Regs& r) {
        Record({0xAC10, r.ecx});
        return reinterpret_cast<uint32_t>(g_heaps[0]);
    });
    s.At({0x005BAC20, AddressOf(&Mem::Manager::NextHeap)}, 4, [](Regs& r) {
        Record({0xAC20, r.ecx, Rel(r.args[0])});
        return r.args[0] == reinterpret_cast<uint32_t>(g_heaps[0]) ? reinterpret_cast<uint32_t>(g_heaps[1]) : 0u;
    });
    s.At({0x005BC530, AddressOf(&Mem::Region::MemAvailable)}, 0, [](Regs& r) {
        Record({0xC530, Rel(r.ecx)});
        return static_cast<uint32_t>(g_freeMem += 0x1111);
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, Rel(r.ecx)});
        return g_poolEmpty ? 0u : reinterpret_cast<uint32_t>(g_newStruct);
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Rel(r.args[0])});
        return 0u;
    });
    using RunFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], Rel(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        auto* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, Hash(format), std::strchr(format, '%') ? r.args[1] : 0u}); // (no argument: stack garbage)
        return 0u;
    });
}

void TestMathCFuncs()
{
    // the globals: the symbol table (empty), the structure pools, the memory manager pointer
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x006F3614, 16, {}}, {0x007D1800, 4, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;

    auto* standIns = new StandIns;
    StandInMathCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };

        // the trigonometry: degrees in, out of range and NaN arguments too
        const float angles[] = {30.0f, -45.5f, 1000.25f, 0.0f, 90.0f, 1e-3f};
        for (float a : angles) {
            TestParams p; p.Float(0, a);
            call(0x00448AE0, CFuncs::CFunc_Sin, p);
            call(0x00448B20, CFuncs::CFunc_Cos, p);
            call(0x00448B60, CFuncs::CFunc_Tan, p);
        }
        const float values[] = {0.5f, -1.0f, 1.0f, 0.3333f, 2.0f, -0.7071f, 1e5f, 0.0f};
        for (float v : values) {
            TestParams p; p.Float(0, v);
            call(0x00448BA0, CFuncs::CFunc_ASin, p);
            call(0x00448BE0, CFuncs::CFunc_ACos, p);
            call(0x00448C20, CFuncs::CFunc_ATan, p);
            call(0x00448C60, CFuncs::CFunc_Sqrt, p);
        }
        { TestParams p; p.Integer(0, 7); call(0x00448C60, CFuncs::CFunc_Sqrt, p); } // an integer converts
        // NormalizeVector: an ordinary vector, a zero one, a tiny one
        {
            TestParams a; a.Vector(0, 3.0f, -4.0f, 12.5f); call(0x00448CA0, CFuncs::CFunc_NormalizeVector, a);
            TestParams b; b.Vector(0, 0.0f, 0.0f, 0.0f); call(0x00448CA0, CFuncs::CFunc_NormalizeVector, b);
            TestParams c; c.Vector(0, 1e-3f, 2e-3f, -7e-4f); call(0x00448CA0, CFuncs::CFunc_NormalizeVector, c);
            TestParams d; d.Vector(0, -0.1f, 0.7f, 1.3f); call(0x00448CA0, CFuncs::CFunc_NormalizeVector, d);
        }
        // LinearMap: inside, below, above, at the bounds, a reversed range, an empty one
        {
            struct Case {
                float from, to, value, lower, upper;
            };
            const Case cases[] = {{10.0f, 20.0f, 0.3f, 0.1f, 0.9f},  {-5.0f, 5.0f, -3.0f, 0.0f, 1.0f},
                                  {100.0f, 0.5f, 7.0f, 1.0f, 3.0f},  {1.0f, 3.0f, 2.0f, 2.0f, 4.0f},
                                  {1.0f, 3.0f, 4.0f, 2.0f, 4.0f},    {0.25f, 7.75f, 3.0f, 5.0f, 1.0f},
                                  {2.0f, 9.0f, 1.0f, 1.0f, 1.0f},    {2.0f, 9.0f, 1.5f, 3.0f, 3.0f},
                                  {-1.0f, 1.0f, 0.333f, 0.0f, 1.0f},  {1.0f, 3.0f, 4.5f, 2.0f, 4.0f},
                                  {0.0f, 1e9f, 0.3f, 0.0f, 0.9f},    {-7.0f, 3e8f, 0.1f, 0.0f, 0.7f},
                                  {-1000.0f, 1000.0f, 0.3f, 0.0f, 0.7f}, {-1000.0f, 1000.0f, 0.351f, 0.0f, 0.7f},
                                  {-3.0f, 3.0f, 1.1f, 0.1f, 2.1f}};
            uint32_t name = 0x5E5A17;
            for (const Case& c : cases) {
                TestParams p;
                p.Checksum(0xEC953EEC, name++).Float(0x46E55E8F, c.from).Float(0x28782D3B, c.to)
                    .Float(0x08296457, c.value).Float(0x0EA9E0A8, c.lower).Float(0xACAECE33, c.upper);
                call(0x00448D70, CFuncs::CFunc_LinearMap, p);
            }
        }
        // Clamp: inside, below, above, min > max, NaN
        {
            const float cases[][3] = {{5.0f, 1.0f, 10.0f}, {-3.0f, 1.0f, 10.0f}, {12.0f, 1.0f, 10.0f},
                                      {5.0f, 8.0f, 2.0f},  {1.0f, 1.0f, 1.0f},   {std::nanf(""), 0.0f, 1.0f},
                                      {0.5f, std::nanf(""), 0.25f}, {0.5f, 0.75f, std::nanf("")}};
            for (const auto& c : cases) {
                TestParams p; p.Float(0, c[0]).Float(0x5E84E22F, c[1]).Float(0x6289DD76, c[2]);
                call(0x00448E60, CFuncs::CFunc_Clamp, p);
            }
        }
        // RotateVector: all three angles, none, one at a time
        {
            TestParams a;
            a.Float(0x7323E97C, 1.5f).Float(0x0424D9EA, -2.25f).Float(0x9D2D8850, 4.0f).Float(0xFDF10F7A, 30.0f)
                .Float(0x8AF63FEC, -75.0f).Float(0x13FF6E56, 200.0f);
            call(0x00448EF0, CFuncs::CFunc_RotateVector, a);
            TestParams b; b.Float(0x7323E97C, 3.0f).Float(0x0424D9EA, 5.0f).Float(0x9D2D8850, -7.0f);
            call(0x00448EF0, CFuncs::CFunc_RotateVector, b);
            TestParams c; c.Float(0x7323E97C, 1.0f).Float(0x0424D9EA, 2.0f).Float(0x9D2D8850, 3.0f).Float(0x8AF63FEC, 90.0f);
            call(0x00448EF0, CFuncs::CFunc_RotateVector, c);
            TestParams d; d.Float(0x7323E97C, 1.0f).Float(0x0424D9EA, 2.0f).Float(0x9D2D8850, 3.0f).Float(0x13FF6E56, -33.0f);
            call(0x00448EF0, CFuncs::CFunc_RotateVector, d);
            TestParams e; e.Float(0x7323E97C, 1.0f).Float(0x0424D9EA, 2.0f).Float(0x9D2D8850, 3.0f).Float(0xFDF10F7A, 45.0f);
            call(0x00448EF0, CFuncs::CFunc_RotateVector, e);
        }
        // DisplayFreeMem: two heaps (each with its own region and name), from the pool and with the pool empty
        {
            GameVariable<void*>(0x007D1800) = reinterpret_cast<void*>(0x00ABC000);
            Mem::PoolPair& pools = GameVariable<Mem::PoolPair>(0x006F3614);
            std::memset(&pools, 0, sizeof(pools));
            pools.pools[0] = reinterpret_cast<Mem::Pool*>(g_fakePool);
            std::memset(g_heaps, 0, sizeof(g_heaps));
            std::memset(g_regions, 0, sizeof(g_regions));
            const char* names[2] = {"TopDown", "ScriptHeap"};
            for (int i = 0; i < 2; ++i) {
                auto* heap = reinterpret_cast<Mem::Allocator*>(g_heaps[i]);
                heap->m_region = reinterpret_cast<Mem::Region*>(g_regions[1 - i]);
                heap->m_name = names[i];
                reinterpret_cast<Mem::Region*>(g_regions[i])->m_minAvailable = 0x7000 + 0x123 * i;
            }
            TestParams none;
            g_freeMem = 0x40000;
            g_poolEmpty = false;
            std::memset(g_newStruct, 0xCD, sizeof(g_newStruct));
            call(0x004489B0, CFuncs::CFunc_DisplayFreeMem, none);
            Record({HashBytes(g_newStruct, sizeof(g_newStruct))});
            g_poolEmpty = true;
            call(0x004489B0, CFuncs::CFunc_DisplayFreeMem, none);
        }
        {
            TestParams none;
            call(0x00449110, CFuncs::CFunc_Test, none);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "math script commands: the original reached its calls");
    Compare(traces[0], traces[1], "math script commands, DisplayFreeMem, Test (0x004489B0-0x00449110)");
}

} // namespace

SELFTEST(Early, TestMathCFuncs, RemoveStandIns);
