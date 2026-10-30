// Self-tests for the engine's script commands 0x004489B0-0x0044B7E0 (maths, debugging, objects, script
// utilities, arrays): each original against ours on identical stand-in state; the structure getters run for real on
// hand-built parameters, and every other game function the commands call is stood in for by recording code (over
// the game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/str.h"
#include "game/core/pool.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/object_cfuncs.h"
#include "game/obj/objman.h"
#include "game/obj/proxim.h"
#include "game/obj/smtptr.h"
#include "game/script/array_cfuncs.h"
#include "game/script/cscript.h"
#include "game/script/debug_cfuncs.h"
#include "game/script/math_cfuncs.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/script_cfuncs.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/script/script_startup.h"
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
    TestParams& Structure(uint32_t name, CStruct* value) { return Add(Script::ESYMBOLTYPE_STRUCTURE, name, reinterpret_cast<uint32_t>(value)); }
    TestParams& Array(uint32_t name, Script::CArray* value) { return Add(Script::ESYMBOLTYPE_ARRAY, name, reinterpret_cast<uint32_t>(value)); }
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

// --- objects, nodes, .qb files, languages, proximity (0x00449280-0x00449960) -----------------------------------

alignas(16) uint8_t g_objects[4][0x110];
alignas(16) uint8_t g_components[6][0x100];
alignas(16) uint8_t g_particleSystems[3][0x20];
alignas(16) uint8_t g_mlpManager[0x100];
alignas(16) uint8_t g_proximManager[0x10];
void* g_objectVtable[16];
void* g_componentVtable[16];
uint32_t g_firstUnusedId;

uint32_t ObjRel(uint32_t value)
{
    struct Range {
        const void* base;
        uint32_t size;
        uint32_t tag;
    };
    const Range ranges[] = {{g_objects, sizeof(g_objects), 0xC0000000u},
                            {g_components, sizeof(g_components), 0xC1000000u},
                            {g_particleSystems, sizeof(g_particleSystems), 0xC2000000u}};
    for (const Range& r : ranges) {
        auto base = reinterpret_cast<uint32_t>(r.base);
        if (value >= base && value < base + r.size)
            return r.tag + (value - base);
    }
    return Rel(value);
}

// GetObjScreenCoords' projection (0x004A1090, thiscall, the result in ST0): x, y and depth from the position.
float __fastcall ProjectStandIn(void* camera, void*, const Mth::Vector* position, float* x, float* y, float* depth)
{
    Record({0x1090, reinterpret_cast<uint32_t>(camera), ObjRel(reinterpret_cast<uint32_t>(position))});
    *x = position->x * 2.0f + 1.0f;
    *y = position->y - 3.5f;
    *depth = position->z;
    return 0.75f;
}

void StandInObjectCalls(StandIns& s)
{
    StandInStructChanges(s);
    s.At({0x00477E20, AddressOf(static_cast<Method<void, uint32_t, float, float, float>>(&CStruct::AddVector))}, 16,
         [](Regs& r) {
             Record({0xADDE, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
             return 0u;
         });
    s.At({0x00478670, AddressOf(static_cast<Method<void, uint32_t, CStruct*>>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0xADD7, Rel(r.ecx), r.args[0], ObjRel(r.args[1])});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, ObjRel(r.ecx), ObjRel(r.args[0])});
        return 0u;
    });
    s.At({0x0045C1B0, AddressOf(static_cast<CStruct* (*)(uint32_t, bool)>(&SkateScript::GetNode))}, 0, [](Regs& r) {
        Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
        return r.args[0] == 0xDEAD ? 0u : 0x5D000000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x0045F160, AddressOf(&Obj::InitCoim)}, 0, [](Regs& r) {
        Record({0xF160, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x0045F1C0, AddressOf(&Obj::ResizeCoimHeap)}, 0, [](Regs& r) {
        Record({0xF1C0, r.args[0]});
        return 0u;
    });
    s.At({0x0045F3A0, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObjectFromNode)}, 12, [](Regs& r) {
        Record({0xF3A0, r.ecx, ObjRel(r.args[0]), ObjRel(r.args[1]), r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        uint32_t id = r.args[0];
        if (id >= 0x0B100 && id < 0x0B104)
            return reinterpret_cast<uint32_t>(g_objects[id - 0x0B100]);
        if (id >= 8 && id < g_firstUnusedId)
            return reinterpret_cast<uint32_t>(g_objects[id & 3]);
        return 0u;
    });
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Record({0xF2D0, r.ecx, r.args[0]});
        switch (r.args[0]) {
        case 0xA9DB601E: return reinterpret_cast<uint32_t>(g_components[0]); // particle: 0, 1, 2
        case 0x0000C0C0: return reinterpret_cast<uint32_t>(g_components[1]); // 1, 2
        case 0x07713C7B: return reinterpret_cast<uint32_t>(g_components[3]); // sound: 3, 4
        case 0x0F1641E3: return reinterpret_cast<uint32_t>(g_components[5]); // stream: 5
        default: return 0u;
        }
    });
    s.At({0x004A06D0}, 0, [](Regs& r) {
        Record({0x06D0, r.args[0]});
        return 0x00CA3E7Au;
    });
    s.Bytes(0x004A1090, Branch(0xE9, 0x004A1090, reinterpret_cast<void*>(&ProjectStandIn)));
    s.At({0x0049ADE0}, 4, [](Regs& r) {
        Record({0xADE0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005A5960, AddressOf(&Script::LoadQBFile)}, 0, [](Regs& r) {
        Record({0x5960, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x005A59B0, AddressOf(&Script::UnloadQBFile)}, 0, [](Regs& r) {
        Record({0x59B0, r.args[0]});
        return 0u;
    });
    s.At({0x0048C100, AddressOf(&Nx::ScreenShot)}, 0, [](Regs& r) {
        Record({0xC100, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x0048C0F0, AddressOf(&Nx::Engine::ToggleMetrics)}, 0, [](Regs&) {
        Record({0xC0F0});
        return 0u;
    });
    s.At({0x0046AB50, AddressOf(&Obj::Proxim_Cleanup)}, 0, [](Regs&) {
        Record({0xAB50});
        return 0u;
    });
    g_objectVtable[6] = s.Thunk(0, [](Regs& r) {
        Record({0xDEAD, ObjRel(r.ecx)});
        return 0u;
    });
    g_componentVtable[12] = s.Thunk(12, [](Regs& r) {
        Record({0xCA11, ObjRel(r.ecx), r.args[0], ObjRel(r.args[1]), Rel(r.args[2])});
        return 1u;
    });
}

void TestObjectCFuncs()
{
    // the globals: the symbol table (empty), the composite object manager, the particle manager, the language, the
    // LevelSpecific .qb table, the proximity manager, the main loop manager
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x006F0FB8, 4, {}}, {0x006FA7C0, 4, {}}, {0x007CEFA0, 4, {}},
                                {0x00696900, 15 * 4, {}}, {0x006F2268, 4, {}}, {0x00696D68, 4, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;

    auto* standIns = new StandIns;
    StandInObjectCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        GameVariable<uint32_t>(0x006F0FB8) = 0x00C0FFEE;
        std::memset(g_objects, 0, sizeof(g_objects));
        std::memset(g_components, 0, sizeof(g_components));
        std::memset(g_particleSystems, 0, sizeof(g_particleSystems));
        for (int i = 0; i < 4; ++i) {
            auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_objects[i]);
            object->m_vtable = g_objectVtable;
            object->m_objectFlags = i == 2 ? Obj::OBJECT_FLAG_DEAD : 0x40;
            object->m_pos = {10.0f * i + 0.5f, -2.0f * i, 7.25f, 1.0f};
            object->m_displayMatrix.Ident();
            object->m_displayMatrix.row[0] = {0.0f, 1.0f, 0.0f, 0.0f};
            object->m_displayMatrix.row[1] = {-1.0f, 0.0f, 0.5f, 0.0f};
            object->m_displayMatrix.row[3] = {100.0f * i, 3.0f, -9.0f, 1.0f};
        }
        // components 0-2: particles (objects 0, 1, 2; groups 0x61, 0x62, 0x61), 3-4 sound, 5 stream
        for (int i = 0; i < 6; ++i) {
            auto* c = reinterpret_cast<Obj::CBaseComponent*>(g_components[i]);
            c->m_vtable = g_componentVtable;
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_objects[i % 4]);
            if (i != 2 && i != 4 && i != 5)
                c->mp_nextOfType = reinterpret_cast<Obj::CBaseComponent*>(g_components[i + 1]);
            if (i < 3) {
                *reinterpret_cast<uint8_t**>(g_components[i] + 0x1C) = g_particleSystems[i];
                *reinterpret_cast<uint32_t*>(g_particleSystems[i] + 0x0C) = i == 1 ? 0x62u : 0x61u;
            }
            std::memset(g_components[i] + 0x20, 0x11 * (i + 1), 0xC0);
        }

        // AppendNodeParams; GetNode (found / not); the node names
        {
            TestParams extra; extra.Integer(0x1234, 5);
            TestParams a; a.Checksum(0x7A8017BA, 0x0E01).Structure(0x7031F10C, extra.Struct());
            call(0x00449280, CFuncs::CFunc_AppendNodeParams, a);
            TestParams b; b.Checksum(0, 0x0E02); call(0x00449850, CFuncs::CFunc_GetNode, b);
            TestParams c; c.Checksum(0, 0xDEAD); call(0x00449850, CFuncs::CFunc_GetNode, c);
            TestParams none;
            for (int k = 0; k < 3; ++k) {
                for (uint32_t which : {0x00449890u, 0x004498C0u}) {
                    std::memset(g_script, 0, sizeof(g_script));
                    auto* script = reinterpret_cast<Script::CScript*>(g_script);
                    script->m_params = reinterpret_cast<CStruct*>(g_results);
                    *reinterpret_cast<uint8_t**>(g_script + 0xBC) = k ? g_objects[k] : nullptr;
                    reinterpret_cast<Obj::CObject*>(g_objects[k])->m_id = k == 1 ? 0u : 0x0B0B0 + k;
                    script->m_node = k == 2 ? 0 : 0x4E0D + k;
                    CFunc ours = which == 0x00449890u ? CFuncs::CFunc_GetNodeName : CFuncs::CFunc_GetTriggerNodeName;
                    bool result = original ? Original<CFunc>(which)(none.Struct(), script) : ours(none.Struct(), script);
                    Record({0xC0DE, which, result ? 1u : 0u});
                }
            }
        }
        // the COIM heap
        {
            TestParams a;
            a.Integer(0x083FDB95, 0x40000).Integer(0xB0847D54, 17).Integer(0xBB9A31B5, -3).Float(0x03D6DE47, 123.456f);
            call(0x004492E0, CFuncs::CFunc_InitCOIM, a);
            TestParams b;
            b.Integer(0x083FDB95, 0x1000).Integer(0xB0847D54, 0).Integer(0xBB9A31B5, 9).Float(0x03D6DE47, -1.9f);
            call(0x004492E0, CFuncs::CFunc_InitCOIM, b);
            TestParams c; c.Integer(0x7C554E79, 0x22000); call(0x00449380, CFuncs::CFunc_ResizeCOIMHeap, c);
            TestParams d; call(0x00449380, CFuncs::CFunc_ResizeCOIMHeap, d);
        }
        // composite objects: made (with and without params), ids (free at once, after taken ones, preferred)
        {
            static Script::CArray components;
            TestParams extra; extra.Integer(0x99, 1);
            TestParams a; a.Array(0x11B70A02, &components).Structure(0x7031F10C, extra.Struct());
            call(0x00449540, CFuncs::CFunc_CreateCompositeObject, a);
            TestParams b; b.Array(0x11B70A02, &components);
            call(0x00449540, CFuncs::CFunc_CreateCompositeObject, b);
            TestParams none;
            g_firstUnusedId = 8;
            call(0x004495A0, CFuncs::CFunc_GetUniqueCompositeObjectID, none);
            g_firstUnusedId = 11;
            call(0x004495A0, CFuncs::CFunc_GetUniqueCompositeObjectID, none);
            TestParams c; c.Checksum(0x392649D7, 0x30); call(0x004495A0, CFuncs::CFunc_GetUniqueCompositeObjectID, c);
            TestParams d; d.Checksum(0x392649D7, 9); call(0x004495A0, CFuncs::CFunc_GetUniqueCompositeObjectID, d);
        }
        // screen coordinates, transforms, component scripts, particles
        {
            TestParams a; a.Checksum(0x57525413, 0x0B101); call(0x00449610, CFuncs::CFunc_GetObjScreenCoords, a);
            TestParams b; b.Checksum(0x57525413, 0x0B1FF); call(0x00449610, CFuncs::CFunc_GetObjScreenCoords, b);
            TestParams c; c.Vector(0xA18717AA, 1.5f, -2.0f, 4.0f).Checksum(0x57525413, 0x0B103);
            call(0x00449740, CFuncs::CFunc_TransformLocalToWorld, c);
            TestParams d; d.Vector(0xA18717AA, -0.25f, 8.0f, 0.0f).Checksum(0x57525413, 0x0B100);
            call(0x00449740, CFuncs::CFunc_TransformLocalToWorld, d);
            TestParams extra; extra.Integer(0x55, 2);
            TestParams e; e.Checksum(0xB6015EA8, 0xC0C0).Checksum(0xB990D003, 0x7A26E7).Structure(0x7031F10C, extra.Struct());
            call(0x004496B0, CFuncs::CFunc_RunScriptOnComponentType, e);
            TestParams f; f.Checksum(0xB6015EA8, 0xA9DB601E).Checksum(0xB990D003, 0x7A26E8);
            call(0x004496B0, CFuncs::CFunc_RunScriptOnComponentType, f);
            TestParams g; g.Checksum(0xB6015EA8, 0x1234).Checksum(0xB990D003, 0x7A26E9);
            call(0x004496B0, CFuncs::CFunc_RunScriptOnComponentType, g);
            GameVariable<uint32_t>(0x006FA7C0) = 0x00FA7C00;
            TestParams h; h.Checksum(0x87FA53ED, 0x61); call(0x004497D0, CFuncs::CFunc_DestroyParticlesByGroupID, h);
            GameVariable<uint32_t>(0x006FA7C0) = 0;
            TestParams i; i.Checksum(0x87FA53ED, 0x62); call(0x004497D0, CFuncs::CFunc_DestroyParticlesByGroupID, i);
        }
        // .qb files: plain, LevelSpecific (a free slot after taken ones; the table full), unloading (two slots match)
        {
            uint32_t* files = &GameVariable<uint32_t>(0x00696900);
            for (int k = 0; k < 15; ++k)
                files[k] = k < 3 || k == 5 ? 0x100 + k : 0;
            TestParams a; a.String(0, "levels\\nj\\nj_scripts.qb"); call(0x004493B0, CFuncs::CFunc_LoadQB, a);
            TestParams b; b.String(0, "levels\\nj\\nj_goals.qb").Flag(0x6DD1DECF); call(0x004493B0, CFuncs::CFunc_LoadQB, b);
            TestParams c; c.String(0, "levels\\nj\\nj_more.qb").Flag(0x6DD1DECF); call(0x004493B0, CFuncs::CFunc_LoadQB, c);
            for (int k = 0; k < 15; ++k)
                Record({files[k]});
            for (int k = 0; k < 15; ++k)
                files[k] = 0x200 + k;
            call(0x004493B0, CFuncs::CFunc_LoadQB, b);
            files[2] = files[9] = Crc::GenerateCRCFromString("levels\\nj\\nj_goals.qb");
            call(0x00449450, CFuncs::CFunc_UnloadQB, b);
            for (int k = 0; k < 15; ++k)
                Record({files[k]});
        }
        // screenshots, metrics, languages, proximity, ResetEngine
        {
            TestParams a; a.String(0xC3F4169A, "shots\\one"); call(0x004494A0, CFuncs::CFunc_ScreenShot, a);
            TestParams none;
            call(0x004494A0, CFuncs::CFunc_ScreenShot, none);
            call(0x004494E0, CFuncs::CFunc_ToggleMetrics, none);
            for (int language = 0; language < 8; ++language) {
                GameVariable<int>(0x007CEFA0) = language;
                call(0x004494F0, CFuncs::CFunc_German, none);
                call(0x00449500, CFuncs::CFunc_French, none);
                call(0x00449510, CFuncs::CFunc_Spanish, none);
                call(0x00449520, CFuncs::CFunc_Italian, none);
                call(0x00449530, CFuncs::CFunc_English, none);
            }
            call(0x004498F0, CFuncs::CFunc_ProximCleanup, none);
            GameVariable<void*>(0x006F2268) = g_proximManager;
            for (int entering : {0, 1}) {
                std::memset(g_proximManager, 0xA5, sizeof(g_proximManager));
                g_proximManager[4] = static_cast<uint8_t>(entering);
                call(0x00449950, CFuncs::CFunc_Inside, none);
            }
            GameVariable<void*>(0x00696D68) = g_mlpManager;
            std::memset(g_mlpManager, 0, sizeof(g_mlpManager));
            call(0x00449960, CFuncs::CFunc_ResetEngine, none);
            Record({HashBytes(g_mlpManager, sizeof(g_mlpManager))});
        }
        Record({HashBytes(&g_components[0][0], sizeof(g_components)), HashBytes(&g_objects[0][0], sizeof(g_objects))});
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 200, "object script commands: the original reached its calls");
    Compare(traces[0], traces[1], "object, node, .qb, language and proximity script commands (0x00449280-0x00449960)");
}

// --- RunScriptOnObject, Orient, Change, SetEventHandlerOnObject, Wait, comparisons, script strings, GlobalExists ---
// (0x00449A10-0x0044A380)

alignas(16) uint8_t g_ownScript[0x100];   // an object's own script
alignas(16) uint8_t g_newScript[0x100];   // the script SetEventHandlerOnObject makes
alignas(16) uint8_t g_symbols[8][0x14];   // the stand-in globals
alignas(16) uint8_t g_newSymbols[4][0x14];
alignas(16) uint8_t g_newStructs[2][0x10];
alignas(16) uint8_t g_newArrays[2][0x10];
alignas(16) uint8_t g_skeletonComponent[0x40];
int g_newSymbolCount;
int g_newStructCount;
int g_newArrayCount;
bool g_failBoneMatrix;

uint32_t ScriptRel(uint32_t value)
{
    struct Range {
        const void* base;
        uint32_t size;
        uint32_t tag;
    };
    const Range ranges[] = {{g_ownScript, sizeof(g_ownScript), 0xD0000000u}, {g_newScript, sizeof(g_newScript), 0xD1000000u},
                            {g_symbols, sizeof(g_symbols), 0xD2000000u},     {g_newSymbols, sizeof(g_newSymbols), 0xD3000000u},
                            {g_newStructs, sizeof(g_newStructs), 0xD4000000u}, {g_newArrays, sizeof(g_newArrays), 0xD5000000u},
                            {g_skeletonComponent, sizeof(g_skeletonComponent), 0xD6000000u}};
    for (const Range& r : ranges) {
        auto base = reinterpret_cast<uint32_t>(r.base);
        if (value >= base && value < base + r.size)
            return r.tag + (value - base);
    }
    return ObjRel(value);
}

// The stand-in globals: 0x51000 + i.
Script::CSymbolTableEntry* Symbol(int i) { return reinterpret_cast<Script::CSymbolTableEntry*>(g_symbols[i]); }
uint32_t SymbolFor(uint32_t name)
{
    if (name >= 0x51000 && name < 0x51008)
        return reinterpret_cast<uint32_t>(g_symbols[name - 0x51000]);
    return 0u;
}

void StandInScriptCalls(StandIns& s)
{
    StandInObjectCalls(s);
    s.At({0x004776B0, AddressOf(static_cast<Method<void, uint32_t>>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return r.args[0] >= 0x0B100 && r.args[0] < 0x0B104 ? reinterpret_cast<uint32_t>(g_objects[r.args[0] - 0x0B100]) : 0u;
    });
    s.At({0x00461500, AddressOf(&Obj::CObject::CallScript)}, 8, [](Regs& r) {
        Record({0x1500, ObjRel(r.ecx), r.args[0], ObjRel(r.args[1])});
        return 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, ScriptRel(r.ecx), ScriptRel(r.args[0])});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, ScriptRel(r.ecx), ScriptRel(r.args[0])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, ObjRel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_skeletonComponent);
    });
    s.At({0x004A36E0}, 8, [](Regs& r) {
        Record({0x36E0, r.ecx, r.args[0]});
        if (g_failBoneMatrix)
            return 0u;
        auto* m = reinterpret_cast<float*>(r.args[1]);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? 1.0f : 0.125f * (i + 1);
        return 1u;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return reinterpret_cast<uint32_t>(g_newScript);
    });
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Record({0x38E0, ScriptRel(r.ecx)});
        return r.ecx;
    });
    s.At({0x004735D0, AddressOf(&Script::CScript::SetEventHandler)}, 20, [](Regs& r) {
        Record({0x35D0, ScriptRel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, ObjRel(r.args[4])});
        return 0u;
    });
    s.At({0x00478CC0, AddressOf(&Script::FindSymbol)}, 0, [](Regs& r) {
        Record({0x8CC0, r.args[0]});
        return SymbolFor(r.args[0]);
    });
    s.At({0x00478CF0, AddressOf(&Script::LookUpSymbol)}, 0, [](Regs& r) {
        Record({0x8CF0, r.args[0]});
        return SymbolFor(r.args[0]);
    });
    s.At({0x004711D0, AddressOf(&Script::CleanUpAndRemoveSymbol)}, 0, [](Regs& r) {
        Record({0x11D0, ScriptRel(r.args[0])});
        return 0u;
    });
    s.At({0x00479110, AddressOf(&Script::CreateSymbol)}, 0, [](Regs& r) {
        Record({0x9110, r.args[0]});
        uint8_t* entry = g_newSymbols[g_newSymbolCount++ & 3];
        std::memset(entry, 0, 0x14);
        reinterpret_cast<Script::CSymbolTableEntry*>(entry)->name = r.args[0];
        return reinterpret_cast<uint32_t>(entry);
    });
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, [](Regs&) {
        Record({0x9030});
        return reinterpret_cast<uint32_t>(g_newStructs[g_newStructCount++ & 1]);
    });
    s.At({0x00408F90, AddressOf(&Script::NewArray)}, 0, [](Regs&) {
        Record({0x8F90});
        return reinterpret_cast<uint32_t>(g_newArrays[g_newArrayCount++ & 1]);
    });
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        Record({0x8A60, ScriptRel(r.args[0]), r.args[1]});
        return 0u;
    });
    for (uint32_t address : {0x00472BD0u, 0x00472BF0u, 0x00472C10u, 0x00473310u}) {
        uint32_t ours = 0;
        switch (address) {
        case 0x00472BD0u: ours = AddressOf(&Script::CScript::Wait); break;
        case 0x00472BF0u: ours = AddressOf(&Script::CScript::WaitOnePerFrame); break;
        case 0x00472C10u: ours = AddressOf(&Script::CScript::WaitTime); break;
        default: ours = AddressOf(&Script::CScript::WaitRealTime); break;
        }
        s.At({address, ours}, 4, [address](Regs& r) {
            Record({address, Rel(r.ecx), r.args[0]});
            return 0u;
        });
    }
    s.At({0x00476370, AddressOf(&Script::SetScriptString)}, 0, [](Regs& r) {
        Record({0x6370, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
}

void TestScriptCFuncs()
{
    // the globals: the symbol table (empty), the composite object manager, the script pools
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x006F0FB8, 4, {}}, {0x006F3548, 16, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;

    auto* standIns = new StandIns;
    StandInScriptCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        GameVariable<uint32_t>(0x006F0FB8) = 0x00C0FFEE;
        g_newSymbolCount = g_newStructCount = g_newArrayCount = 0;
        std::memset(g_newSymbols, 0, sizeof(g_newSymbols));
        std::memset(g_objects, 0, sizeof(g_objects));
        std::memset(g_ownScript, 0, sizeof(g_ownScript));
        std::memset(g_newScript, 0xCD, sizeof(g_newScript));
        std::memset(g_newStructs, 0xCD, sizeof(g_newStructs));
        std::memset(g_newArrays, 0xCD, sizeof(g_newArrays));
        std::memset(g_skeletonComponent, 0, sizeof(g_skeletonComponent));
        *reinterpret_cast<uint32_t*>(g_skeletonComponent + 0x18) = 0x05CE1E70; // its skeleton
        for (int i = 0; i < 4; ++i) {
            auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_objects[i]);
            object->m_vtable = g_objectVtable;
            object->m_pos = {10.0f * i + 0.5f, -2.0f * i, 7.25f, 1.0f};
            object->m_displayMatrix.Ident();
            object->m_displayMatrix.row[0] = {0.0f, 1.0f, 0.0f, 0.0f};
            object->m_displayMatrix.row[1] = {-1.0f, 0.0f, 0.5f, 0.0f};
            object->m_displayMatrix.row[3] = {100.0f * i, 3.0f, -9.0f, 1.0f};
            object->m_matrix.Ident();
        }

        // RunScriptOnObject: an object without a script, one whose script has no object, one whose script has one,
        // no object
        {
            TestParams extra; extra.Integer(0x77, 3);
            reinterpret_cast<Obj::CObject*>(g_objects[1])->m_script = reinterpret_cast<Script::CScript*>(g_ownScript);
            reinterpret_cast<Obj::CObject*>(g_objects[2])->m_script = reinterpret_cast<Script::CScript*>(g_newScript);
            std::memset(g_newScript, 0, sizeof(g_newScript));
            *reinterpret_cast<uint8_t**>(g_newScript + 0xBC) = g_objects[3];
            for (uint32_t id : {0x0B100u, 0x0B101u, 0x0B102u, 0x0B1FFu}) {
                TestParams a; a.Checksum(0x40C698AF, id).Checksum(0, 0x5C0000 + id).Structure(0x7031F10C, extra.Struct());
                call(0x00449A10, CFuncs::CFunc_RunScriptOnObject, a);
            }
            TestParams b; b.Checksum(0x40C698AF, 0x0B101).Checksum(0, 0x5C1234); // (no params)
            call(0x00449A10, CFuncs::CFunc_RunScriptOnObject, b);
            Record({ScriptRel(*reinterpret_cast<uint32_t*>(g_ownScript + 0xBC))});
            for (int i = 0; i < 4; ++i)
                reinterpret_cast<Obj::CObject*>(g_objects[i])->m_script = nullptr;
        }
        // Orient: everything default; angles in degrees with an offset; relative to another object; with a bone
        {
            auto recordObjects = [] { Record({HashBytes(&g_objects[0][0], sizeof(g_objects))}); };
            TestParams a; a.Checksum(0x57525413, 0x0B100);
            call(0x00449AC0, CFuncs::CFunc_Orient, a);
            recordObjects();
            TestParams b;
            b.Checksum(0x57525413, 0x0B101).Vector(0x9D2D0915, 30.0f, -45.0f, 90.0f).Vector(0xA6F5352F, 1.0f, 2.0f, 3.0f)
                .Flag(0x30EDA37B);
            call(0x00449AC0, CFuncs::CFunc_Orient, b);
            recordObjects();
            TestParams c;
            c.Checksum(0x57525413, 0x0B102).Vector(0x9D2D0915, 0.5f, 0.25f, -1.0f).Checksum(0xC03F9839, 0x0B103);
            call(0x00449AC0, CFuncs::CFunc_Orient, c);
            recordObjects();
            TestParams d;
            d.Checksum(0x57525413, 0x0B100).Vector(0xA6F5352F, -4.0f, 0.5f, 8.0f).Checksum(0xC03F9839, 0x0B101)
                .Checksum(0xCAB94088, 0x0B0E);
            call(0x00449AC0, CFuncs::CFunc_Orient, d);
            recordObjects();
            TestParams e; e.Checksum(0x57525413, 0x0B103).Checksum(0xC03F9839, 0x0B1FF); // RelativeTo: none
            call(0x00449AC0, CFuncs::CFunc_Orient, e);
            recordObjects();
        }
        // SetEventHandlerOnObject: an object with a script; one without (made); every argument / the defaults
        {
            TestParams extra; extra.Integer(0x31, 1);
            reinterpret_cast<Obj::CObject*>(g_objects[0])->m_script = reinterpret_cast<Script::CScript*>(g_ownScript);
            Mem::PoolPair& pools = GameVariable<Mem::PoolPair>(0x006F3548);
            std::memset(&pools, 0, sizeof(pools));
            pools.pools[0] = reinterpret_cast<Mem::Pool*>(0x00F00100);
            TestParams a;
            a.Checksum(0xF8728BEC, 0xE7E7).Checksum(0xA6D2D890, 0x5C5C).Checksum(0x923FBB3A, 0x6A6A).Flag(0x80367192)
                .Structure(0x7031F10C, extra.Struct()).Checksum(0x57525413, 0x0B100);
            call(0x00449F80, CFuncs::CFunc_SetEventHandlerOnObject, a);
            std::memset(g_newScript, 0xCD, sizeof(g_newScript));
            TestParams b; b.Checksum(0xF8728BEC, 0xE8E8).Checksum(0xA6D2D890, 0x5D5D).Checksum(0x57525413, 0x0B102);
            call(0x00449F80, CFuncs::CFunc_SetEventHandlerOnObject, b);
            Record({ScriptRel(reinterpret_cast<uint32_t>(reinterpret_cast<Obj::CObject*>(g_objects[2])->m_script))});
        }
        // Change: by name=value and by globalname / newValue; each type; resolve; refused names
        {
            static Script::CArray anArray;
            TestParams inner; inner.Integer(0x42, 4);
            std::memset(g_symbols, 0, sizeof(g_symbols));
            const uint8_t types[8] = {1, 2, 10, 12, 13, 7, 8, 3};
            for (int i = 0; i < 8; ++i) {
                Symbol(i)->type = static_cast<Script::ESymbolType>(types[i]);
                Symbol(i)->name = 0x51000 + i;
                Symbol(i)->unknown8 = 0xAB00 + i;
                Symbol(i)->raw = 0x7700 + i;
            }
            std::vector<TestParams*> cases;
            auto add = [&]() { cases.push_back(new TestParams); return cases.back(); };
            add()->Integer(0x51000, 42);
            add()->Float(0x51001, 2.5f);
            add()->Structure(0x51002, inner.Struct());
            add()->Array(0x51003, &anArray);
            add()->Checksum(0x51004, 0xC4C4);
            add()->Checksum(0x51004, 0x51001).Flag(0x991A7BC3);          // resolved: the float's value
            add()->Checksum(0x51004, 0x51002).Flag(0x991A7BC3);          // resolved: a structure (copied)
            add()->Checksum(0x51004, 0x5FFFF).Flag(0x991A7BC3);          // not a global: the name kept
            add()->String(0x51000, "text");                              // a string: the global made empty
            add()->Vector(0x51001, 1.0f, 2.0f, 3.0f);
            add()->Integer(0x51005, 1);                                  // a script: refused
            add()->Integer(0x51006, 1);                                  // a C function: refused
            add()->Integer(0x51007, 9);                                  // a string global: changed
            add()->Integer(0x5FFFF, 1);                                  // no such global
            add()->Checksum(0x60F6F242, 0x51000).Integer(0x4B40630D, -5);   // globalname / newValue
            add()->Checksum(0x60F6F242, 0x51000).Integer(0x12345, -6);      // no newValue
            add();                                                       // nothing
            for (TestParams* p : cases) {
                call(0x00449D00, CFuncs::CFunc_Change, *p);
                Record({HashBytes(&g_newSymbols[0][0], sizeof(g_newSymbols)), HashBytes(&g_newStructs[0][0], sizeof(g_newStructs)),
                        HashBytes(&g_newArrays[0][0], sizeof(g_newArrays))});
                delete p;
            }
        }
        // Wait: every unit, with and without the real-time flag, fractions truncated
        {
            const uint32_t units[] = {0, 0xD029F619, 0x49E0EE96, 0xFC37DF05, 0xB99AE3D6, 0xDCD4CE73, 0x4A07C332,
                                      0x019176C5, 0xECAA3345};
            for (int realTime = 0; realTime < 2; ++realTime) {
                for (uint32_t unit : units) {
                    TestParams p; p.Float(0, 2.7f);
                    if (unit)
                        p.Flag(unit);
                    if (realTime)
                        p.Flag(0xC847F2B9);
                    call(0x0044A0D0, CFuncs::CFunc_Wait, p);
                }
            }
            TestParams q; q.Integer(0, 3).Flag(0x4A07C332); call(0x0044A0D0, CFuncs::CFunc_Wait, q);
            TestParams r; r.Float(0, -1.3f).Flag(0xD029F619); call(0x0044A0D0, CFuncs::CFunc_Wait, r);
            TestParams none; call(0x0044A0D0, CFuncs::CFunc_Wait, none);
        }
        // the comparisons, SetScriptString, GlobalExists
        {
            TestParams a; a.Integer(0x174841BC, 5).Integer(0x8E411006, 5); call(0x0044A200, CFuncs::CFunc_IntegerEquals, a);
            TestParams b; b.Integer(0x174841BC, 5).Integer(0x8E411006, 6); call(0x0044A200, CFuncs::CFunc_IntegerEquals, b);
            TestParams c; c.Integer(0x8E411006, 0); call(0x0044A200, CFuncs::CFunc_IntegerEquals, c);
            TestParams d; d.Integer(0x174841BC, 7); call(0x0044A200, CFuncs::CFunc_IntegerEquals, d);
            TestParams e; e.Checksum(0x174841BC, 0xAA).Checksum(0x8E411006, 0xAA); call(0x0044A260, CFuncs::CFunc_ChecksumEquals, e);
            TestParams f; f.Checksum(0x174841BC, 0xAA).Checksum(0x8E411006, 0xAB); call(0x0044A260, CFuncs::CFunc_ChecksumEquals, f);
            TestParams g; g.Checksum(0x174841BC, 0xAA); call(0x0044A260, CFuncs::CFunc_ChecksumEquals, g);
            TestParams h; h.String(0x174841BC, "Hello").String(0x8E411006, "hELLO"); call(0x0044A2C0, CFuncs::CFunc_StringEquals, h);
            TestParams i; i.String(0x174841BC, "Hello").String(0x8E411006, "Hell"); call(0x0044A2C0, CFuncs::CFunc_StringEquals, i);
            TestParams j; j.Integer(0x7F8C98FE, 3).String(0x61414D56, "a string"); call(0x0044A320, CFuncs::CFunc_SetScriptString, j);
            TestParams k; call(0x0044A320, CFuncs::CFunc_SetScriptString, k);
            const uint32_t typeNames[] = {0x90FEC815, 0x456D28D1, 0x21902065, 0x365AA16A, 0x5EF31148, 0x61414D56,
                                          0xE491B7A4, 0xA1DC81F9, 0xA5F054F4, 0xEBAE254E, 0xF6A5E196, 0x12345678, 0};
            const uint8_t symbolTypes[] = {1, 2, 3, 5, 6, 10, 12, 13};
            for (uint8_t type : symbolTypes) {
                Symbol(0)->type = static_cast<Script::ESymbolType>(type);
                for (uint32_t typeName : typeNames) {
                    TestParams p; p.Checksum(0xA1DC81F9, 0x51000);
                    if (typeName)
                        p.Checksum(0x7321A8D6, typeName);
                    call(0x0044A380, CFuncs::CFunc_GlobalExists, p);
                }
            }
            Symbol(0)->type = Script::ESYMBOLTYPE_NONE;
            TestParams none; none.Checksum(0xA1DC81F9, 0x51000); call(0x0044A380, CFuncs::CFunc_GlobalExists, none);
            TestParams missing; missing.Checksum(0xA1DC81F9, 0x5FFFF); call(0x0044A380, CFuncs::CFunc_GlobalExists, missing);
        }
        Record({HashBytes(&g_newScript[0], sizeof(g_newScript)), HashBytes(&g_ownScript[0], sizeof(g_ownScript))});
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "script commands: the original reached its calls");
    Compare(traces[0], traces[1], "RunScriptOnObject .. GlobalExists (0x00449A10-0x0044A380)");
}

// --- FormatText's worker, EnableScriptPrintf .. CleanUpAndRemoveSymbol (0x0044A480-0x0044AAE0) --------------------

char g_separated[32];

void StandInFormatCalls(StandIns& s)
{
    StandInScriptCalls(s);
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x004779D0, AddressOf(static_cast<Method<void, uint32_t, const char*>>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0xADD3, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 0, [](Regs& r) {
        Record({0xD110, r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] == 0x0A0A ? "TheName" : "unknown");
    });
    s.At({0x00402200, AddressOf(&Str::FormatWithSeparators)}, 0, [](Regs& r) {
        Record({0x2200, r.args[0]});
        std::snprintf(g_separated, sizeof(g_separated), "<%d>", static_cast<int>(r.args[0]));
        return reinterpret_cast<uint32_t>(g_separated);
    });
    s.At({0x00479730, AddressOf(&Script::ResolveNameComponent)}, 0, [](Regs& r) {
        auto* c = reinterpret_cast<CComponent*>(r.args[0]);
        Record({0x9730, Rel(r.args[0]), c->typeAndFlag, c->raw});
        if (c->Type() == Script::ESYMBOLTYPE_NAME && c->raw == 0x5E5E) {
            c->typeAndFlag = static_cast<uint8_t>((Script::ESYMBOLTYPE_INTEGER << 1) | (c->typeAndFlag & 1));
            c->raw = 77;
        }
        return 0u;
    });
    using GetStructureFn = CStruct* (*)(uint32_t);
    s.At({0x00479020, AddressOf(static_cast<GetStructureFn>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return 0x5E000000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x00472FD0, AddressOf(&Script::CScript::Block)}, 4, [](Regs& r) {
        Record({0x2FD0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
}

void TestFormatCFuncs()
{
    // the globals: the symbol table (empty), FormatText's buffer and value, the scripts' printf switch
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x00696940, 0x3EC + 16, {}}, {0x0067D6D4, 1, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;

    auto* standIns = new StandIns;
    StandInFormatCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        std::memset(reinterpret_cast<void*>(0x00696940), 0, 0x3EC + 16);

        // FormatText
        {
            using FormatFn = int(__cdecl*)(CStruct*, CStruct*);
            auto format = [&](TestParams& p) {
                int result = original ? Original<FormatFn>(0x0044A480)(reinterpret_cast<CStruct*>(g_results), p.Struct())
                                      : CFuncs::FormatText(reinterpret_cast<CStruct*>(g_results), p.Struct());
                Record({0xF0F0, static_cast<uint32_t>(result), Hash(reinterpret_cast<const char*>(0x00696940)),
                        GameVariable<uint32_t>(0x00696D2C), GameVariable<uint32_t>(0x00696D34)});
            };
            TestParams a; a.Checksum(0xFF0DB407, 0x7E47); format(a); // no format
            TestParams b; b.String(0, "Score: %s points").Integer(Crc::GenerateCRCFromString("s"), 1234).Checksum(0xFF0DB407, 0x7E47);
            format(b);
            TestParams c;
            c.String(0, "[%a|%b]").Integer(Crc::GenerateCRCFromString("a"), 7).Integer(0xADA5B338, 4)
                .Float(Crc::GenerateCRCFromString("b"), 2.25f).Integer(0xD8E2E09F, 1).Checksum(0x76FA7A8F, 0xC5C5);
            format(c);
            TestParams d; d.String(0, "%n!").Integer(Crc::GenerateCRCFromString("n"), -1234567).Flag(0xF138085F)
                .Checksum(0xFF0DB407, 0x7E48).Checksum(0x76FA7A8F, 0xC5C6);
            format(d);
            GameVariable<uint8_t>(0x00696D2C) = 1; // the value's flag bit kept
            TestParams e;
            e.String(0, "%%long_name1 and %%Other_2: %p %v %q %x %%fl")
                .String(Crc::GenerateCRCFromString("long_name1"), "text")
                .LocalString(Crc::GenerateCRCFromString("Other_2"), "local")
                .Pair(Crc::GenerateCRCFromString("p"), 1.5f, -2.0f)
                .Vector(Crc::GenerateCRCFromString("v"), 0.125f, 3.0f, -7.75f)
                .Checksum(Crc::GenerateCRCFromString("q"), 0x0A0A)
                .Checksum(Crc::GenerateCRCFromString("x"), 0x5E5E)
                .Float(Crc::GenerateCRCFromString("fl"), 3.14159f)
                .Checksum(0xFF0DB407, 0x7E49);
            format(e);
            TestParams inner; inner.Integer(1, 1);
            TestParams f; f.String(0, "100\\% sure %s, 50\\x %t").Structure(Crc::GenerateCRCFromString("s"), inner.Struct())
                .Integer(Crc::GenerateCRCFromString("t"), 9).Checksum(0xFF0DB407, 0x7E4A);
            format(f);
            TestParams g; g.String(0, "%a and %%missing").Integer(Crc::GenerateCRCFromString("a"), 1).Checksum(0xFF0DB407, 0x7E4B);
            format(g);
            TestParams h; h.String(0, "").Checksum(0xFF0DB407, 0x7E4C).Checksum(0x76FA7A8F, 0xC5C7);
            format(h);
            TestParams i; i.String(0, "%a%a").Integer(Crc::GenerateCRCFromString("a"), 3).Integer(Crc::GenerateCRCFromString("a"), 4);
            format(i); // (the last one wins)
            TestParams j; j.String(0, "%f").Float(Crc::GenerateCRCFromString("f"), -0.0004f);
            format(j);
        }
        // EnableScriptPrintf, IfDebugOn
        {
            TestParams off; off.Integer(0, 0);
            TestParams on; on.Integer(0, 5);
            TestParams none;
            for (TestParams* p : {&off, &on, &off, &none}) {
                call(0x0044A900, CFuncs::CFunc_EnableScriptPrintf, *p);
                Record({GameVariable<uint8_t>(0x0067D6D4)});
            }
            call(0x0044A930, CFuncs::CFunc_IfDebugOn, none);
        }
        // StructureContains: a component, a flag, neither, no structure
        {
            TestParams inner; inner.Integer(0x111, 1).Flag(0x222);
            for (uint32_t name : {0x111u, 0x222u, 0x333u}) {
                TestParams p; p.Structure(0x90FEC815, inner.Struct()).Checksum(0, name);
                call(0x0044A940, CFuncs::CFunc_StructureContains, p);
            }
            TestParams q; q.Checksum(0, 0x111); call(0x0044A940, CFuncs::CFunc_StructureContains, q);
        }
        // ClearStruct, ScriptExists, IsArray, Block, IsTrue, CleanUpAndRemoveSymbol
        {
            TestParams a; a.Checksum(0x456D28D1, 0x1A2B); call(0x0044A9B0, CFuncs::CFunc_ClearStruct, a);
            std::memset(g_symbols, 0, sizeof(g_symbols));
            const uint8_t types[8] = {7, 1, 8, 7, 13, 12, 3, 10};
            for (int k = 0; k < 8; ++k)
                Symbol(k)->type = static_cast<Script::ESymbolType>(types[k]);
            for (uint32_t name : {0x51000u, 0x51001u, 0x51002u, 0x51003u, 0x5FFFFu}) {
                TestParams p; p.Checksum(0, name); call(0x0044A9E0, CFuncs::CFunc_ScriptExists, p);
                call(0x0044AAE0, CFuncs::CFunc_CleanUpAndRemoveSymbol, p);
            }
            static Script::CArray anArray;
            TestParams b; b.Array(0, &anArray); call(0x0044AA20, CFuncs::CFunc_IsArray, b);
            TestParams c; c.Integer(0, 1); call(0x0044AA20, CFuncs::CFunc_IsArray, c);
            TestParams none; call(0x0044AA20, CFuncs::CFunc_IsArray, none);
            call(0x0044AA50, CFuncs::CFunc_Block, none);
            TestParams d; d.Checksum(0x7321A8D6, 0xE1); call(0x0044AA50, CFuncs::CFunc_Block, d);
            TestParams e; e.Checksum(0x2C3A27A4, 0xE2); call(0x0044AA50, CFuncs::CFunc_Block, e);
            TestParams f; f.Checksum(0x2C3A27A4, 0xE3).Checksum(0x7321A8D6, 0xE4); call(0x0044AA50, CFuncs::CFunc_Block, f);
            TestParams g; g.Integer(0x1234, 5); call(0x0044AA50, CFuncs::CFunc_Block, g);
            for (int value : {0, 3, -1}) {
                TestParams p; p.Integer(0, value); call(0x0044AAB0, CFuncs::CFunc_IsTrue, p);
            }
            call(0x0044AAB0, CFuncs::CFunc_IsTrue, none);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 200, "FormatText and the script utilities: the original reached its calls");
    Compare(traces[0], traces[1], "FormatText, EnableScriptPrintf .. CleanUpAndRemoveSymbol (0x0044A480-0x0044AAE0)");
}

// --- event handlers, warning level, casts, arrays, AddParams, RemoveComponent, GetScriptString (0x0044AB20-0x0044B790)

// An array built by hand: `type`, the elements (one element is kept in place, as the game does).
struct TestArray {
    Script::CArray array;
    uint32_t elements[8];
    TestArray(ESymbolType type, std::initializer_list<uint32_t> values)
    {
        std::memset(&array, 0, sizeof(array));
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
    Script::CArray* Get() { return &array; }
    uint32_t Raw() { return reinterpret_cast<uint32_t>(&array); }
};

TestParams g_scriptParams; // the calling script's own parameters, where a command reads them
TestArray* g_globalArray;

void StandInArrayCalls(StandIns& s)
{
    StandInFormatCalls(s);
    s.At({0x00477FC0, AddressOf(&CStruct::AddPair)}, 12, [](Regs& r) {
        Record({0xADD2, Rel(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00478B00, AddressOf(static_cast<Method<void, uint32_t, Script::CArray*>>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Record({0xADDA, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using GetArrayFn = Script::CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return reinterpret_cast<uint32_t>(g_globalArray ? g_globalArray->Get() : nullptr);
    });
    // the game's _stricmp (its C runtime isn't started offline) answered by ours - what ours calls (not recorded)
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(
            _stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x004736C0, AddressOf(&Script::CScript::ResetEventHandlersFromTable)}, 8, [](Regs& r) {
        Record({0x36C0, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00473690, AddressOf(&Script::CScript::ClearEventHandler)}, 4, [](Regs& r) {
        Record({0x3690, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00472EA0, AddressOf(&Script::CScript::ClearEventHandlerGroup)}, 4, [](Regs& r) {
        Record({0x2EA0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00472ED0, AddressOf(&Script::CScript::SetOnExceptionScript)}, 4, [](Regs& r) {
        Record({0x2ED0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00472EF0, AddressOf(&Script::CScript::SetOnExitScript)}, 4, [](Regs& r) {
        Record({0x2EF0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00472EC0, AddressOf(&Script::CScript::PrintEventHandlerTable)}, 0, [](Regs& r) {
        Record({0x2EC0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00474250, AddressOf(&Script::StopAllScripts)}, 0, [](Regs&) {
        Record({0x4250});
        return 0u;
    });
    s.At({0x00476390, AddressOf(&Script::GetScriptString)}, 0, [](Regs& r) {
        Record({0x6390, r.args[0]});
        return reinterpret_cast<uint32_t>("script string");
    });
}

void TestArrayCFuncs()
{
    // the globals: the symbol table (empty), FirstTimeThisIsCalled's flag, the warning level
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x0067D6D5, 1, {}}, {0x006F3534, 4, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;

    auto* standIns = new StandIns;
    StandInArrayCalls(*standIns);

    // the arrays
    static Script::CPair pairs[2] = {{0, 1.5f, -2.0f}, {0, 3.0f, 4.25f}};
    static Script::CVector vectors[2] = {{0, 1.0f, 2.0f, 3.0f}, {0, -1.0f, 0.5f, 9.0f}};
    static TestParams structs[2];
    static TestArray ints(Script::ESYMBOLTYPE_INTEGER, {5, static_cast<uint32_t>(-7), 42});
    static TestArray floats(Script::ESYMBOLTYPE_FLOAT, {0x3FC00000, 0xC0200000});
    static TestArray strings(Script::ESYMBOLTYPE_STRING, {reinterpret_cast<uint32_t>("Alpha"), reinterpret_cast<uint32_t>("beta"),
                                                         reinterpret_cast<uint32_t>("GAMMA")});
    static TestArray locals(Script::ESYMBOLTYPE_LOCALSTRING, {reinterpret_cast<uint32_t>("loc0"), reinterpret_cast<uint32_t>("loc1")});
    static TestArray names(Script::ESYMBOLTYPE_NAME, {0xAAA1, 0xAAA2});
    static TestArray pairArray(Script::ESYMBOLTYPE_PAIR, {reinterpret_cast<uint32_t>(&pairs[0]), reinterpret_cast<uint32_t>(&pairs[1])});
    static TestArray vectorArray(Script::ESYMBOLTYPE_VECTOR, {reinterpret_cast<uint32_t>(&vectors[0]), reinterpret_cast<uint32_t>(&vectors[1])});
    static TestArray structArray(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(structs[0].Struct()),
                                                               reinterpret_cast<uint32_t>(structs[1].Struct())});
    static TestArray single(Script::ESYMBOLTYPE_INTEGER, {99});
    static TestArray arrays(Script::ESYMBOLTYPE_ARRAY, {ints.Raw(), single.Raw()});
    static TestArray scripts(Script::ESYMBOLTYPE_QSCRIPT, {1, 2});
    static TestArray grid(Script::ESYMBOLTYPE_ARRAY, {ints.Raw(), floats.Raw(), strings.Raw(), locals.Raw(), names.Raw(),
                                                      pairArray.Raw(), vectorArray.Raw(), structArray.Raw()});
    static TestArray grid2(Script::ESYMBOLTYPE_ARRAY, {arrays.Raw(), scripts.Raw()});
    static TestArray cube(Script::ESYMBOLTYPE_ARRAY, {grid2.Raw(), grid.Raw()});

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        // a command reading the calling script's own parameters
        auto callOn = [&](uint32_t address, CFunc ours, TestParams& params, TestParams& own) {
            std::memset(g_script, 0, sizeof(g_script));
            auto* script = reinterpret_cast<Script::CScript*>(g_script);
            script->m_params = own.Struct();
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        GameVariable<uint8_t>(0x0067D6D5) = 1;
        GameVariable<int>(0x006F3534) = 7;

        // the event handlers
        {
            TestParams extra; extra.Integer(0x31, 1);
            TestParams a;
            a.Checksum(0xF8728BEC, 0xE1).Checksum(0xA6D2D890, 0x5C1).Checksum(0x923FBB3A, 0x6A1).Flag(0x80367192)
                .Structure(0x7031F10C, extra.Struct());
            call(0x0044AB20, CFuncs::CFunc_SetEventHandler, a);
            call(0x0044ABD0, CFuncs::CFunc_SetExceptionHandler, a);
            TestParams b; b.Checksum(0xF8728BEC, 0xE2).Checksum(0xA6D2D890, 0x5C2);
            call(0x0044AB20, CFuncs::CFunc_SetEventHandler, b);
            call(0x0044ABD0, CFuncs::CFunc_SetExceptionHandler, b);
            g_globalArray = &names;
            TestParams c; c.Checksum(0, 0x7AB1E).Checksum(0x923FBB3A, 0x6A3);
            call(0x0044AC70, CFuncs::CFunc_ResetEventHandlersFromTable, c);
            TestParams d; d.Checksum(0, 0x7AB1F);
            call(0x0044AC70, CFuncs::CFunc_ResetEventHandlersFromTable, d);
            TestParams e; e.Checksum(0, 0xE5);
            call(0x0044ACD0, CFuncs::CFunc_ClearEventHandler, e);
            call(0x0044AD00, CFuncs::CFunc_ClearEventHandlerGroup, e);
            call(0x0044AD30, CFuncs::CFunc_OnExceptionRun, e);
            call(0x0044AD60, CFuncs::CFunc_OnExitRun, e);
            TestParams none;
            call(0x0044AD00, CFuncs::CFunc_ClearEventHandlerGroup, none);
            call(0x0044AD30, CFuncs::CFunc_OnExceptionRun, none);
            call(0x0044AD60, CFuncs::CFunc_OnExitRun, none);
            call(0x0044AD90, CFuncs::CFunc_PrintEventHandlerTable, none);
            call(0x0044ADA0, CFuncs::CFunc_StopAllScripts, none);
            call(0x0044ADB0, CFuncs::CFunc_FirstTimeThisIsCalled, none);
            call(0x0044ADB0, CFuncs::CFunc_FirstTimeThisIsCalled, none);
        }
        // the warning level
        for (uint32_t level : {0x4E10B054u, 0x806FFF30u, 0xD6454EFAu, 0x12345u, 0u}) {
            GameVariable<int>(0x006F3534) = 7;
            TestParams p;
            if (level)
                p.Checksum(0x651533EC, level);
            call(0x0044ADD0, CFuncs::CFunc_SetScriptWarningLevel, p);
            Record({GameVariable<uint32_t>(0x006F3534)});
        }
        // CastToInteger / StringToInteger on the script's own parameters
        {
            TestParams own;
            own.Float(0xF1, 3.99f).Float(0xF2, -2.5f).Integer(0xF3, 8).String(0xF4, "1234").String(0xF5, "9x7")
                .String(0xF6, "");
            for (uint32_t name : {0xF1u, 0xF2u, 0xF3u, 0xF4u, 0xF9u}) {
                TestParams p; p.Checksum(0, name);
                callOn(0x0044AE30, CFuncs::CFunc_CastToInteger, p, own);
            }
            TestParams none;
            callOn(0x0044AE30, CFuncs::CFunc_CastToInteger, none, own);
            for (uint32_t name : {0xF4u, 0xF5u, 0xF6u, 0xF1u, 0xF9u}) {
                TestParams p; p.Checksum(0, name);
                callOn(0x0044AEB0, CFuncs::CFunc_StringToInteger, p, own);
            }
            callOn(0x0044AEB0, CFuncs::CFunc_StringToInteger, none, own);
        }
        // ArrayContains: integers, strings (case ignored), names, another type, an empty array
        {
            static TestArray empty(Script::ESYMBOLTYPE_INTEGER, {});
            auto contains = [&](TestArray& array, auto add) {
                TestParams p; p.Array(0x5EF31148, array.Get());
                add(p);
                call(0x0044AF50, CFuncs::CFunc_ArrayContains, p);
            };
            contains(ints, [](TestParams& p) { p.Integer(0x71059581, 42); });
            contains(ints, [](TestParams& p) { p.Integer(0x71059581, -7); });
            contains(ints, [](TestParams& p) { p.Integer(0x71059581, 6); });
            contains(strings, [](TestParams& p) { p.String(0x71059581, "gamma"); });
            contains(strings, [](TestParams& p) { p.String(0x71059581, "delta"); });
            contains(names, [](TestParams& p) { p.Checksum(0x71059581, 0xAAA2); });
            contains(names, [](TestParams& p) { p.Checksum(0x71059581, 0xAAA3); });
            contains(floats, [](TestParams& p) { p.Float(0x71059581, 1.5f); });
            contains(empty, [](TestParams& p) { p.Integer(0x71059581, 0); });
            contains(single, [](TestParams& p) { p.Integer(0x71059581, 99); });
        }
        // GetArraySize: the array, then one, two, three indices deep
        {
            TestParams a; a.Array(0, grid.Get()); call(0x0044B030, CFuncs::CFunc_GetArraySize, a);
            TestParams b; b.Array(0, grid.Get()).Integer(0x0BA453B9, 2); call(0x0044B030, CFuncs::CFunc_GetArraySize, b);
            TestParams c; c.Array(0, cube.Get()).Integer(0x0BA453B9, 0).Integer(0x92AD0203, 0); call(0x0044B030, CFuncs::CFunc_GetArraySize, c);
            TestParams d; d.Array(0, cube.Get()).Integer(0x0BA453B9, 0).Integer(0x92AD0203, 0).Integer(0xE5AA3295, 1);
            call(0x0044B030, CFuncs::CFunc_GetArraySize, d);
            TestParams e; e.Array(0, cube.Get()).Integer(0x92AD0203, 0); call(0x0044B030, CFuncs::CFunc_GetArraySize, e);
        }
        // Get2DArrayData over every row type (the array in the script's parameters, then the global one)
        {
            TestParams own; own.Array(0xA7A7, grid.Get());
            for (int row = 0; row < 8; ++row) {
                for (int column = 0; column < 2; ++column) {
                    TestParams p; p.Checksum(0x8F61D602, 0xA7A7).Integer(0x0BA453B9, row).Integer(0x92AD0203, column);
                    callOn(0x0044B0F0, CFuncs::CFunc_Get2DArrayData, p, own);
                }
            }
            g_globalArray = &grid2;
            TestParams q; q.Checksum(0x8F61D602, 0xB7B7).Integer(0x0BA453B9, 0).Integer(0x92AD0203, 1);
            callOn(0x0044B0F0, CFuncs::CFunc_Get2DArrayData, q, own);
            TestParams r; r.Checksum(0x8F61D602, 0xB7B7).Integer(0x0BA453B9, 1).Integer(0x92AD0203, 0);
            callOn(0x0044B0F0, CFuncs::CFunc_Get2DArrayData, r, own); // (a script array: nothing)
        }
        // Get3DArrayData
        {
            TestParams own; own.Array(0xC0BE, cube.Get());
            const int cases[][3] = {{1, 0, 2}, {1, 1, 1}, {1, 4, 0}, {0, 0, 1}, {1, 7, 1}, {1, 6, 0}, {1, 2, 2}};
            for (const auto& c : cases) {
                TestParams p;
                p.Checksum(0x8F61D602, 0xC0BE).Integer(0x0BA453B9, c[0]).Integer(0x92AD0203, c[1]).Integer(0xE5AA3295, c[2]);
                callOn(0x0044B300, CFuncs::CFunc_Get3DArrayData, p, own);
            }
            g_globalArray = &cube;
            TestParams q; q.Checksum(0x8F61D602, 0xD0D0).Integer(0x0BA453B9, 0).Integer(0x92AD0203, 0).Integer(0xE5AA3295, 0);
            callOn(0x0044B300, CFuncs::CFunc_Get3DArrayData, q, own);
        }
        // GetNDArrayData: the indices in the script's own parameters - one, two and three deep
        {
            static TestArray one(Script::ESYMBOLTYPE_INTEGER, {2});
            static TestArray two(Script::ESYMBOLTYPE_INTEGER, {5, 1});
            static TestArray three(Script::ESYMBOLTYPE_INTEGER, {1, 3, 0});
            static TestArray threeB(Script::ESYMBOLTYPE_INTEGER, {0, 0, 1});
            TestParams p; p.Checksum(0x8F61D602, 0xC0BE);
            for (TestArray* indices : {&one, &two, &three, &threeB}) {
                TestParams own; own.Array(0xC0BE, indices == &one ? ints.Get() : indices == &two ? grid.Get() : cube.Get())
                    .Array(0xFFC8E484, indices->Get());
                callOn(0x0044B530, CFuncs::CFunc_GetNDArrayData, p, own);
            }
            g_globalArray = &grid;
            TestParams own2; own2.Array(0xFFC8E484, two.Get());
            TestParams q; q.Checksum(0x8F61D602, 0xE0E0);
            callOn(0x0044B530, CFuncs::CFunc_GetNDArrayData, q, own2);
        }
        // AddParams, RemoveComponent, GetScriptString
        {
            TestParams a; a.Integer(0x99, 1);
            call(0x0044B720, CFuncs::CFunc_AddParams, a);
            TestParams b; b.Checksum(0xA1DC81F9, 0xD1).Checksum(0, 0xD2); call(0x0044B740, CFuncs::CFunc_RemoveComponent, b);
            TestParams c; c.Checksum(0, 0xD3); call(0x0044B740, CFuncs::CFunc_RemoveComponent, c);
            TestParams d; d.Integer(0x7F8C98FE, 5); call(0x0044B790, CFuncs::CFunc_GetScriptString, d);
            TestParams none; call(0x0044B790, CFuncs::CFunc_GetScriptString, none);
        }
        g_globalArray = nullptr;
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "array and event handler commands: the original reached its calls");
    Compare(traces[0], traces[1], "SetEventHandler .. GetScriptString (0x0044AB20-0x0044B790)");
}

} // namespace

SELFTEST(Early, TestMathCFuncs, RemoveStandIns);
SELFTEST(Early, TestObjectCFuncs, RemoveStandIns);
SELFTEST(Early, TestScriptCFuncs, RemoveStandIns);
SELFTEST(Early, TestFormatCFuncs, RemoveStandIns);
SELFTEST(Early, TestArrayCFuncs, RemoveStandIns);
