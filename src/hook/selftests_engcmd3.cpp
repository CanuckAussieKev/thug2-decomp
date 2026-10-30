// Self-tests for the engine's script commands 0x004472D0-0x00447F30 (spawned polygons, the scenes' vertex colours,
// the light direction, metrics): each original against ours on the same params and stand-in scenes; the memory
// manager's allocations, the geometry's accessors and every other callee are stood in for by recording code.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/script/cfuncs_engine.h"
#include "game/script/struct.h"
#include "game/gfx/texture_dict.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CArray;
using Script::CStruct;

// The stand-in heap: blocks handed out in order (the same addresses in both runs), each filled with 0xCD.
alignas(16) uint8_t g_heap[0x8000];
uint32_t g_heapUsed;
bool g_allocFails;
uint32_t HeapRel(uint32_t value)
{
    auto start = reinterpret_cast<uint32_t>(g_heap);
    return value - start < sizeof(g_heap) ? 0x4EA00000u + (value - start) : value;
}

// A stand-in geometry: its vertices' positions and colours.
struct Geometry {
    int count;
    Script::VectorValue positions[6];
    uint32_t colours[6];
};
Geometry g_geometry[4];
Geometry g_geometrySaved[4];

uint32_t g_lightResult;

void StandInCalls(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        if (g_allocFails)
            return 0u;
        uint32_t size = (r.args[0] + 15) & ~15u;
        if (g_heapUsed + size > sizeof(g_heap))
            return 0u;
        uint8_t* block = g_heap + g_heapUsed;
        g_heapUsed += size;
        std::memset(block, 0xCD, size);
        return reinterpret_cast<uint32_t>(block);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) { Record({0xABA0, HeapRel(r.args[0])}); return 0u; });
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) { Record({0xF540, r.ecx, r.args[0]}); return 0x7E400000u + (r.args[0] & 0xFFFF); });
    s.At({0x00492450}, 0, [](Regs& r) {
        Record({0x2450, HeapRel(r.args[0])});
        const auto* details = reinterpret_cast<const uint32_t*>(r.args[0]);
        for (int i = 0; i < 0xB8 / 4; ++i)
            Record({details[i]});
        for (int v = 1; v <= 2; ++v)
            for (int i = 0; i < 4; ++i)
                Record({reinterpret_cast<const uint32_t*>(r.args[v])[i]});
        return 0u;
    });
    // the geometry
    s.At({0x0048DF70}, 0, [](Regs& r) {
        Record({0xDF70, r.ecx});
        return static_cast<uint32_t>(reinterpret_cast<Geometry*>(r.ecx)->count);
    });
    s.At({0x0048DF80}, 4, [](Regs& r) {
        Record({0xDF80, r.ecx, HeapRel(r.args[0])});
        auto* geometry = reinterpret_cast<Geometry*>(r.ecx);
        std::memcpy(reinterpret_cast<void*>(r.args[0]), geometry->positions, 16 * geometry->count);
        return 0u;
    });
    s.At({0x0048E150}, 4, [](Regs& r) {
        Record({0xE150, r.ecx, HeapRel(r.args[0])});
        auto* geometry = reinterpret_cast<Geometry*>(r.ecx);
        std::memcpy(reinterpret_cast<void*>(r.args[0]), geometry->colours, 4 * geometry->count);
        return 0u;
    });
    s.At({0x0048DFA0}, 4, [](Regs& r) {
        Record({0xDFA0, r.ecx, HeapRel(r.args[0])});
        auto* geometry = reinterpret_cast<Geometry*>(r.ecx);
        for (int i = 0; i < geometry->count; ++i)
            Record({reinterpret_cast<const uint32_t*>(r.args[0])[i]});
        return 0u;
    });
    s.At({0x0048ED70}, 0, [](Regs& r) {
        Record({0xED70, r.args[0]});
        for (int i = 0; i < 4; ++i)
            Record({reinterpret_cast<const uint32_t*>(r.args[1])[i]});
        return g_lightResult;
    });
    s.At({0x0048CB00}, 0, [](Regs& r) { Record({0xCB00, HeapRel(r.args[0])}); return 0u; });
    using AddStructureFn = void (CStruct::*)(uint32_t, CStruct*);
    s.At({0x00478670, AddressOf(static_cast<AddStructureFn>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0], HeapRel(r.args[1])});
        return 0u;
    });
}

// A script array of 32-bit values.
struct TestArray {
    CArray array;
    uint32_t elements[4];
    TestArray(Script::ESymbolType type, std::initializer_list<uint32_t> values)
    {
        std::memset(&array, 0, sizeof(array));
        array.m_type = type;
        array.m_size = static_cast<uint32_t>(values.size());
        int i = 0;
        for (uint32_t value : values)
            elements[i++] = value;
        if (array.m_size == 1)
            array.m_single = elements[0];
        else
            array.m_elements = elements;
    }
    CArray* Get() { return &array; }
};

// A stand-in hash table (Lst::HashTable's layout) and the scenes' list.
struct Item {
    uint32_t key;
    void* value;
    Item* next;
};
struct Table {
    int bits;
    Item* buckets;
    int size;
    int iterBucket;
    Item* iterItem;
    uint32_t flag;
};
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene;
    uint32_t unknown8;
    SceneNode* next;
};

void TestEngineCommandsPolys()
{
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x00700500, 16, {}}, {0x006F3614, 16, {}}, {0x007CC979, 1, {}},
                                {0x007D1800, 4, {}}, {0x00700F20, 4, {}},  {0x006FA7EC, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    // the scenes: two ordinary ones, a sky, a second sky; their sectors (some of a light group, one without geometry)
    static uint8_t scenes[4][0x100];
    static uint8_t sectors[8][0x20];
    static Item items[4][4];
    static Item chains[4][2];
    static Table tables[4];
    static SceneNode nodes[5];
    std::memset(scenes, 0, sizeof(scenes));
    std::memset(sectors, 0, sizeof(sectors));
    std::memset(items, 0, sizeof(items));
    std::memset(chains, 0, sizeof(chains));
    for (int i = 0; i < 8; ++i) {
        *reinterpret_cast<void**>(sectors[i] + 0x14) = i == 5 ? nullptr : &g_geometry[i % 4];
        *reinterpret_cast<uint32_t*>(sectors[i] + 0xC) = i % 3 == 0 ? 0x16A0u : 0x16A1u;
    }
    // scene 0: sectors 0, 1 (chained) and 2; scene 1: 3 and 5 (no geometry) then an empty chain item that ends it,
    // 4 never reached; scene 2 (sky): 6; scene 3 (sky): 7
    items[0][0] = {1, sectors[0], &chains[0][0]};
    chains[0][0] = {2, sectors[1], nullptr};
    items[0][2] = {3, sectors[2], nullptr};
    items[1][1] = {4, sectors[3], nullptr};
    items[1][2] = {5, sectors[5], &chains[1][0]};
    chains[1][0] = {6, nullptr, nullptr};
    items[1][3] = {7, sectors[4], nullptr};
    items[2][3] = {8, nullptr, &chains[2][0]};
    chains[2][0] = {9, sectors[6], nullptr};
    items[3][0] = {10, sectors[7], nullptr};
    for (int i = 0; i < 4; ++i) {
        tables[i] = {2, items[i], 0, 0, nullptr, 0};
        *reinterpret_cast<Table**>(scenes[i] + 0xD0) = &tables[i];
    }
    scenes[2][0x8D] = 1;
    scenes[3][0x8D] = 1;
    // the list: head (scene -1), 0, sky 2, 1, sky 3
    uint8_t* order[4] = {scenes[0], scenes[2], scenes[1], scenes[3]};
    nodes[0] = {0, reinterpret_cast<uint8_t*>(-1), 0, &nodes[1]};
    for (int i = 0; i < 4; ++i)
        nodes[i + 1] = {0, order[i], 0, i == 3 ? &nodes[0] : &nodes[i + 2]};
    // the geometry: colours across the range, positions giving different hashes (negative, large, fractions)
    for (int g = 0; g < 4; ++g) {
        Geometry& geometry = g_geometry[g];
        geometry.count = g == 3 ? 0 : 6 - g;
        for (int v = 0; v < 6; ++v) {
            float k = static_cast<float>(g * 6 + v);
            geometry.positions[v] = {k * 13.37f - 40.0f, 999.94f - k * 7.31f, k * 1.37f + 0.11f, 1.0f};
            uint32_t seed = 0x9E3779B9u * static_cast<uint32_t>(g * 6 + v + 1);
            geometry.colours[v] = (seed ^ (seed >> 13)) | (v == 0 ? 0x00FFFFFFu : 0) ;
            if (v == 1)
                geometry.colours[v] &= 0xFF000000u;
        }
        geometry.positions[0] = {-3.0e9f, 2.5e8f, -0.05f, 1.0f};
    }
    // a vertex whose hash is 0 and channels of 10: NudgeVC 10% scales them by 0.9f, 8.99999976 - 9 as a float (red and
    // green are rounded to floats, blue isn't); channels that CompressVC towards 1000 by 0.13158% truncates
    // differently with the factor rounded once or twice
    g_geometry[0].positions[2] = {0.0f, 5.0f, 0.0f, 1.0f};
    g_geometry[0].colours[2] = 0x7F0A0A0A;
    g_geometry[1].colours[2] = 0x2088C8F0;
    std::memcpy(g_geometrySaved, g_geometry, sizeof(g_geometry));

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_heapUsed = 0;
        g_allocFails = false;
        std::memset(reinterpret_cast<void*>(0x00700500), 0, 16);
        std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
        GameVariable<uint32_t>(0x00700F20) = 0x7E7D1C00;
        GameVariable<SceneNode*>(0x006FA7EC) = &nodes[1]; // (the first scene; the list is a ring through nodes[0], scene -1)
        std::memcpy(g_geometry, g_geometrySaved, sizeof(g_geometry));
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };
        Params none;

        // SpawnPoly: per-corner colours; one colour; neither; the just values; texture or not
        {
            Script::CVector pos = {0, 1.0f, 2.0f, 3.0f};
            Script::CVector v1 = {0, -4.0f, 5.5f, 6.25f};
            Script::CPair startDims = {0, 10.0f, 20.0f};
            Script::CPair endDims = {0, 30.0f, 40.0f};
            TestArray c0(Script::ESYMBOLTYPE_INTEGER, {0x10, 0x20, 0x130, 0x40});
            TestArray c1(Script::ESYMBOLTYPE_INTEGER, {0x11, 0x21, 0x31, 0x41});
            TestArray c2(Script::ESYMBOLTYPE_INTEGER, {0x12, 0x22, 0x32, 0x42});
            TestArray c3(Script::ESYMBOLTYPE_INTEGER, {0x13, 0x23, 0x33, 0xFFFFFF43});
            TestArray e0(Script::ESYMBOLTYPE_INTEGER, {0x90, 0xA0, 0xB0, 0xC0});
            TestArray e1(Script::ESYMBOLTYPE_INTEGER, {0x91, 0xA1, 0xB1, 0xC1});
            TestArray e2(Script::ESYMBOLTYPE_INTEGER, {0x92, 0xA2, 0xB2, 0xC2});
            TestArray e3(Script::ESYMBOLTYPE_INTEGER, {0x93, 0xA3, 0xB3, 0xC3});
            TestArray starts(Script::ESYMBOLTYPE_ARRAY, {reinterpret_cast<uint32_t>(c0.Get()), reinterpret_cast<uint32_t>(c1.Get()),
                                                         reinterpret_cast<uint32_t>(c2.Get()), reinterpret_cast<uint32_t>(c3.Get())});
            TestArray ends(Script::ESYMBOLTYPE_ARRAY, {reinterpret_cast<uint32_t>(e0.Get()), reinterpret_cast<uint32_t>(e1.Get()),
                                                       reinterpret_cast<uint32_t>(e2.Get()), reinterpret_cast<uint32_t>(e3.Get())});
            const uint32_t justs[][2] = {{0x4B358AEB, 0x76A08D5B}, {0x85981897, 0xE126E035}, {0xBF0F14DB, 0xBF0F14DB},
                                         {0x1234, 0x5678}};
            for (int i = 0; i < 4; ++i) {
                TestArray just(Script::ESYMBOLTYPE_NAME, {justs[i][0], justs[i][1]});
                Params a;
                a.Vector(0x7F261953, &pos).Pair(0x0B1FE8B6, &startDims).Pair(0x1A640930, &endDims)
                    .Float(0xC218CF77, 2.5f + i).Array(0x8B60022F, just.Get());
                if (i == 0)
                    a.Array(0xF78EABF0, starts.Get()).Array(0x4D5A57A8, ends.Get()).Vector(0xC2B31CF0, &v1).Flag(0x9C367E9B)
                        .Checksum(0xDD6BB3D5, 0xB1E0D).Checksum(0x7D99F28D, 0x7E47);
                else if (i == 1)
                    a.Array(0xECFCADE7, c3.Get()).Array(0xE836F013, e1.Get()).Vector(0x70A38CFD, &v1)
                        .Array(0x4D5A57A8, ends.Get());
                else if (i == 2)
                    a.Array(0xE836F013, e2.Get()).Checksum(0x7D99F28D, 0);
                call(0x00447330, CFuncs::CFunc_SpawnPoly, a);
            }
        }
        // CompressVC: all light groups, one light group, nothing to do (target and percent 0 -> factor 1, not 0),
        // a NaN target, a percent above 100
        {
            Params a; a.Float(0xB990D003, 128.0f).Float(0x9E497FC6, 40.0f);
            call(0x00447980, CFuncs::CFunc_CompressVC, a);
            Params b; b.Float(0xB990D003, 30.5f).Float(0x9E497FC6, 75.3f).Checksum(0x7E4813A7, 0x16A0);
            call(0x00447980, CFuncs::CFunc_CompressVC, b);
            Params c; c.Float(0xB990D003, 0.0f).Float(0x9E497FC6, 100.0f);
            call(0x00447980, CFuncs::CFunc_CompressVC, c);
            Params d; d.Float(0xB990D003, std::numeric_limits<float>::quiet_NaN()).Float(0x9E497FC6, 10.0f);
            call(0x00447980, CFuncs::CFunc_CompressVC, d);
            Params e; e.Float(0xB990D003, 200.0f).Float(0x9E497FC6, 350.0f);
            call(0x00447980, CFuncs::CFunc_CompressVC, e);
            Params f; f.Float(0xB990D003, 99.0f).Float(0x9E497FC6, 0.001f).Checksum(0x7E4813A7, 0x16A1);
            call(0x00447980, CFuncs::CFunc_CompressVC, f);
            Params g; g.Float(0xB990D003, 1000.0f).Float(0x9E497FC6, 0.13158f);
            call(0x00447980, CFuncs::CFunc_CompressVC, g);
            // straight: target 0 with factor 0 (nothing), target 0 with a factor
            for (float factor : {0.0f, 0.5f}) {
                if (original)
                    Original<void(__cdecl*)(uint8_t*, float, float, uint32_t)>(0x00447710)(scenes[0], 0.0f, factor, 0);
                else
                    CFuncs::CompressSceneVertexColours(scenes[0], 0.0f, factor, 0);
            }
        }
        // NudgeVC: small and large amounts, negative
        for (float percent : {10.0f, 100.0f, 250.0f, -40.0f}) {
            Params a; a.Float(0x9E497FC6, percent);
            call(0x00447D90, CFuncs::CFunc_NudgeVC, a);
        }
        // SetLightDirection: a vector; heading and pitch; one of them; nothing
        for (uint32_t result : {0x101u, 0x200u}) {
            g_lightResult = result;
            Script::CVector direction = {0, 0.25f, -0.5f, 0.75f};
            Params a; a.Integer(0x7F8C98FE, 2).Vector(0xC1B52E4C, &direction).Float(0xFD4BC03E, 90.0f).Float(0xD8604126, 10.0f);
            call(0x00447E00, CFuncs::CFunc_SetLightDirection, a);
            const float angles[][2] = {{30.0f, 45.0f}, {-123.4f, 77.7f}, {359.9f, -0.01f}, {0.0f, 0.0f}, {1000.5f, 270.0f}};
            for (const float* angle : angles) {
                Params b; b.Integer(0x7F8C98FE, 1).Float(0xFD4BC03E, angle[0]).Float(0xD8604126, angle[1]);
                call(0x00447E00, CFuncs::CFunc_SetLightDirection, b);
            }
            Params c; c.Integer(0x7F8C98FE, 3).Float(0xFD4BC03E, 12.0f);
            call(0x00447E00, CFuncs::CFunc_SetLightDirection, c);
            Params d; d.Integer(0x7F8C98FE, 0).Float(0xD8604126, 12.0f);
            call(0x00447E00, CFuncs::CFunc_SetLightDirection, d);
        }
        // GetMetrics, then with the allocation failing
        call(0x00447F30, CFuncs::CFunc_GetMetrics, none);
        g_allocFails = true;
        call(0x00447F30, CFuncs::CFunc_GetMetrics, none);
        g_allocFails = false;
        // the details freed: from the heap (no pools), then into a stand-in pool (the second of the pair)
        if (original)
            Original<void(__cdecl*)(void*)>(0x004472D0)(g_heap + 0x40);
        else
            CFuncs::FreeSpawnedPolyDetails(g_heap + 0x40);
        static uint8_t poolItems[4][0xB8];
        alignas(4) static uint8_t poolStorage[sizeof(Mem::Pool)];
        auto& pool = *reinterpret_cast<Mem::Pool*>(poolStorage);
        std::memset(&pool, 0, sizeof(pool));
        pool.m_start = poolItems[0];
        pool.m_end = poolItems[4];
        pool.m_count = 4;
        pool.m_itemSize = 0xB8;
        pool.m_used = 3;
        pool.m_freeList = poolItems[3];
        GameVariable<Mem::Pool*>(0x00700500 + 12) = &pool;
        if (original)
            Original<void(__cdecl*)(void*)>(0x004472D0)(poolItems[1]);
        else
            CFuncs::FreeSpawnedPolyDetails(poolItems[1]);
        Record({static_cast<uint32_t>(pool.m_used), pool.m_freeList == poolItems[1] ? 1u : 0u,
                *reinterpret_cast<uint32_t*>(poolItems[1]) == reinterpret_cast<uint32_t>(poolItems[3]) ? 1u : 0u});
        // (the iterators' state left in the tables)
        for (const Table& table : tables)
            Record({static_cast<uint32_t>(table.iterBucket), table.iterItem ? 1u : 0u});
        traces[run] = g_trace;
        for (uint32_t& word : traces[run])
            word = HeapRel(word);
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 500, "engine commands (polys): the original reached its calls");
    Compare(traces[0], traces[1], "engine commands (polys, 0x004472D0-0x00447F30)");
}

} // namespace

SELFTEST(Early, TestEngineCommandsPolys, RemoveStandIns);
