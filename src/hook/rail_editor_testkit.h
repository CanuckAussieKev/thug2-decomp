#pragma once

// The rail editor's self-tests' world (selftests_rail_editor*.cpp): on skater_core_testkit.h's arena - the rail editor
// component, its point and rail pools (small, in the arena), rails of random points, stand-in sectors whose geometry
// records the colours it's told; a separate bump heap for the memory manager's stand-in. The pool pairs and the other
// globals the code touches are set from the variant's start before each run and recorded after it.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/pool.h"
#include "game/ed/rail_editor.h"
#include "hook/game.h"
#include "hook/skater_core_testkit.h"

namespace RailEditorKit {

using namespace SkaterCoreKit;
using Ed::CEditedRail;
using Ed::CEditedRailPoint;
using Ed::CRailEditorComponent;

constexpr uint32_t kEditor = 0x0000;        // the component (0x1168 bytes)
constexpr uint32_t kPointPool = 0x1180;     // its pools (Mem::Pool, 0x5C bytes each)
constexpr uint32_t kRailPool = 0x11E0;
constexpr uint32_t kSparePool = 0x1240;     // a second (empty) point pool, sometimes
constexpr uint32_t kPointItems = 0x12A0;    // 56 points
constexpr int kPointCount = 56;
constexpr uint32_t kRailItems = 0x1C60;     // 24 rails
constexpr int kRailCount = 24;
constexpr uint32_t kSectors = 0x1DE0;       // 48 sectors (0x20 bytes: +4 the name, +0x14 the geometry)
constexpr int kSectorCount = 48;
// (the 48 geometries - +0 a vtable pointer, +8 the collision faces - and their vtable are outside the arena:
// g_geometries / g_geometryVtable below)
constexpr uint32_t kManager = 0x2580;       // the park manager (+0 the generator)
constexpr uint32_t kGenerator = 0x25A0;
constexpr uint32_t kOut = 0x2600;           // outputs (0x100 bytes)
constexpr uint32_t kColours = 0x2700;       // stand-in script arrays
constexpr uint32_t kObjectArea = 0x2800;    // the component's object (+0x4C its position)
constexpr uint32_t kScratch = 0x2A00;       // the rest: per test

// The memory manager's stand-in heap (not in the arena: the pools' blocks are big). Bump allocation from the start
// each run.
alignas(16) inline uint8_t g_heap[0x10000];
alignas(16) inline uint8_t g_geometries[48][0x10];
inline void* g_geometryVtable[64];
inline uint32_t g_heapUsed;
inline void* HeapAllocate(uint32_t size)
{
    size = (size + 15) & ~15u;
    if (g_heapUsed + size > sizeof(g_heap))
        return nullptr;
    void* p = g_heap + g_heapUsed;
    g_heapUsed += size;
    return p;
}

// The globals: both pool pairs, the next rail id, UpdateSuperSectors.
struct Globals {
    Mem::PoolPair points, rails;
    uint32_t nextId;
    bool update;
    static Globals Read()
    {
        return {GameVariable<Mem::PoolPair>(0x007CCD50), GameVariable<Mem::PoolPair>(0x007CCD40),
                GameVariable<uint32_t>(0x0067EFFC), GameVariable<bool>(0x0067F000)};
    }
    void Write() const
    {
        GameVariable<Mem::PoolPair>(0x007CCD50) = points;
        GameVariable<Mem::PoolPair>(0x007CCD40) = rails;
        GameVariable<uint32_t>(0x0067EFFC) = nextId;
        GameVariable<bool>(0x0067F000) = update;
    }
    void Record() const
    {
        for (const Mem::PoolPair* p : {&points, &rails}) {
            SkaterCoreKit::Record(p->current);
            SkaterCoreKit::Record(p->unknown4);
            SkaterCoreKit::Record(reinterpret_cast<uint32_t>(p->pools[0]));
            SkaterCoreKit::Record(reinterpret_cast<uint32_t>(p->pools[1]));
        }
        SkaterCoreKit::Record(nextId);
        SkaterCoreKit::Record(update);
    }
};
inline Globals g_start;

// The geometry's colour call (vtable +0x18).
inline void __fastcall GeometrySetColour(void* geometry, void*, uint32_t rgba)
{
    Record(0xC010C010u);
    Record(reinterpret_cast<uint32_t>(geometry));
    Record(rgba);
}

template <typename T>
T* At(uint32_t offset)
{
    return reinterpret_cast<T*>(ArenaPtr(offset));
}
inline CRailEditorComponent* Editor() { return At<CRailEditorComponent>(kEditor); }
inline Mem::Pool* PointPool() { return At<Mem::Pool>(kPointPool); }
inline Mem::Pool* RailPool() { return At<Mem::Pool>(kRailPool); }

inline void InitPool(Mem::Pool* pool, uint32_t items, int count, int size)
{
    pool->m_start = ArenaPtr(items);
    pool->m_end = ArenaPtr(items + count * size);
    pool->m_count = count;
    pool->m_itemSize = size;
    pool->m_used = 0;
    pool->m_peakUsed = 0;
    pool->m_freeList = ArenaPtr(items);
    for (int i = 0; i < count; ++i)
        *At<void*>(items + i * size) = i < count - 1 ? ArenaPtr(items + (i + 1) * size) : nullptr;
    pool->m_name[0] = '\0'; // (unnamed: the pool code doesn't log for it)
}
// Take an item out of a pool as Pool::Allocate does.
inline void* Take(Mem::Pool* pool)
{
    void* item = pool->m_freeList;
    if (!item)
        return nullptr;
    pool->m_freeList = *static_cast<void**>(item);
    ++pool->m_used;
    if (pool->m_used > pool->m_peakUsed)
        pool->m_peakUsed = pool->m_used;
    return item;
}

inline uint8_t* RandomSector()
{
    return Next() % 3 ? ArenaPtr(kSectors + (Next() % kSectorCount) * 0x20) : nullptr;
}
inline float Coordinate() { return static_cast<float>(static_cast<int>(Next() % 401) - 200) * 0.5f; }

// A random world: up to `maxRails` rails of up to `maxPoints` points, the pools, the sectors, the editor.
inline std::vector<CEditedRail*> BuildWorld(int maxRails, int maxPoints)
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        ArenaAt<float>(i) = RandomFloat(3.0f);
    InitPool(PointPool(), kPointItems, kPointCount, sizeof(CEditedRailPoint));
    InitPool(RailPool(), kRailItems, kRailCount, sizeof(CEditedRail));
    InitPool(At<Mem::Pool>(kSparePool), kPointItems, 0, sizeof(CEditedRailPoint));
    // the sectors
    for (int i = 0; i < kSectorCount; ++i) {
        *At<uint32_t>(kSectors + i * 0x20 + 4) = 0x5EC70000u + i;
        *At<uint8_t*>(kSectors + i * 0x20 + 0x14) = Next() % 5 ? g_geometries[i] : nullptr;
        std::memset(g_geometries[i], 0, sizeof(g_geometries[i]));
        *reinterpret_cast<void***>(g_geometries[i]) = g_geometryVtable;
    }
    // (the vtable's slots ours calls - SetColor 6 - and, aimed at the original dispatchers that the
    // draw test stands in for, the vertex slots 36, 37, 39)
    g_geometryVtable[6] = reinterpret_cast<void*>(&GeometrySetColour);
    g_geometryVtable[36] = reinterpret_cast<void*>(0x0048DF70);
    g_geometryVtable[37] = reinterpret_cast<void*>(0x0048DF80);
    g_geometryVtable[39] = reinterpret_cast<void*>(0x0048DF90);
    *At<uint8_t*>(kManager) = ArenaPtr(kGenerator);
    // the pool pairs (the current slot 0 or 1; the other empty or an empty spare pool)
    g_start = Globals::Read();
    for (Mem::PoolPair* pair : {&g_start.points, &g_start.rails}) {
        pair->current = static_cast<int>(Next() % 2);
        pair->unknown4 = 0;
        pair->pools[0] = pair->pools[1] = nullptr;
    }
    g_start.points.pools[g_start.points.current] = PointPool();
    if (Next() % 4 == 0)
        g_start.points.pools[g_start.points.current ^ 1] = At<Mem::Pool>(kSparePool);
    g_start.rails.pools[g_start.rails.current] = RailPool();
    g_start.nextId = 0x100 + Next() % 50;
    g_start.update = Next() % 3 != 0;
    // the rails
    std::vector<CEditedRail*> rails;
    int count = static_cast<int>(Next() % (maxRails + 1));
    for (int r = 0; r < count; ++r) {
        auto* rail = static_cast<CEditedRail*>(Take(RailPool()));
        rail->m_id = 0x200 + r * 3 + Next() % 3;
        rail->mp_points = nullptr;
        int points = static_cast<int>(Next() % (maxPoints + 1));
        CEditedRailPoint* last = nullptr;
        for (int i = 0; i < points; ++i) {
            auto* p = static_cast<CEditedRailPoint*>(Take(PointPool()));
            if (!p)
                break;
            p->m_pos = {Coordinate(), RandomFloat(20.0f), Coordinate(), 1.0f};
            if (last && Next() % 6 == 0) // (a point on top of the last on the ground plane: ties)
                p->m_pos = {last->m_pos.x, p->m_pos.y, last->m_pos.z, 1.0f};
            p->m_hasPost = RandomBool(50);
            p->m_highlighted = RandomBool(40);
            p->m_height = Next() % 4 ? RandomFloat(30.0f) : -RandomFloat(5.0f);
            p->mp_segment = RandomSector();
            p->mp_post = RandomSector();
            p->mp_next = nullptr;
            p->mp_prev = last;
            if (last)
                last->mp_next = p;
            else
                rail->mp_points = p;
            last = p;
        }
        rails.push_back(rail);
    }
    for (size_t i = 0; i < rails.size(); ++i) {
        rails[i]->mp_next = i + 1 < rails.size() ? rails[i + 1] : nullptr;
        rails[i]->mp_prev = i ? rails[i - 1] : nullptr;
    }
    CRailEditorComponent* editor = Editor();
    editor->mp_rails = rails.empty() ? nullptr : rails[0];
    editor->mp_current = rails.empty() || Next() % 4 == 0 ? nullptr : rails[Next() % rails.size()];
    editor->m_mode = Next() % 3;
    editor->mp_dottedLine = RandomSector();
    editor->m_dottedLineName = 0x0D07;
    editor->m_object = reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kObjectArea));
    return rails;
}

// The stack below the caller cleared (a function's unset locals then read 0 in both runs).
__declspec(noinline) inline void ScrubStack()
{
    volatile uint8_t buffer[0x800];
    for (uint32_t i = 0; i < sizeof(buffer); ++i)
        buffer[i] = 0;
}

// A run: the globals from the start, the heap reset; afterwards the globals and the heap's used part recorded.
template <typename Call>
std::vector<uint32_t> RunWorld(uint32_t seed, Call call)
{
    return RunOnArena(seed, [&](uint8_t*) {
        g_start.Write();
        g_heapUsed = 0;
        std::memset(g_heap, 0xA5, 0x2000);
        uint32_t result = call();
        // (the game's sprintf is stood in for the original; ours uses the DLL's C runtime)
        for (size_t i = 0; i < g_calls.size();) {
            if (g_calls[i] == 0x006234FE)
                g_calls.erase(g_calls.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
        Globals::Read().Record();
        Record(g_heapUsed);
        for (uint32_t i = 0; i < g_heapUsed && i < 0x2000; i += 4)
            Record(*reinterpret_cast<uint32_t*>(g_heap + i));
        return result;
    });
}

} // namespace RailEditorKit
