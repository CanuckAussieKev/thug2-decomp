// Self-tests for AN-16: the world's line queries, the movable objects' list and the collision feeler
// (src/game/nx/coll_world.cpp, feeler.cpp) - the original and ours on identical stand-in collision objects (a fake
// vtable answering from a script), scenes' grids, caches and owners; every call out, the hit records, the feeler
// and the results compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_line.h"
#include "game/nx/coll_world.h"
#include "game/nx/feeler.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Vector;
using Nx::CCollCache;
using Nx::CFeeler;
using Nx::CollisionHit;

constexpr int kObjects = 12; // 0-5 static, 6-11 movable
constexpr int kOwners = 4;

struct FakeObject {
    void** vtable;
    uint32_t checksum;
    uint16_t flags;
    uint8_t terrain;
    uint8_t pad;
    uint32_t geometry;
};

struct WorldArena {
    alignas(16) uint8_t feeler[0xB0];
    CollisionHit hit;
    FakeObject objects[kObjects];
    uint8_t owners[kOwners][0x20];
    Mth::BBox boxes[kObjects];
    uint32_t grids[4];
    uint32_t scenes[4];
    uint32_t lists[3][kObjects + 1];
    uint8_t heap[0x400];
    uint8_t manager[0x200];
};
static_assert(sizeof(CCollCache) == 0xFCC, "cache size");
WorldArena g_wa;
alignas(16) uint8_t g_waCache[0xFD0];
bool g_waOriginal;
uint32_t g_waUsed;

// The script each stand-in object answers from (per query).
struct ObjectScript {
    bool touches;     // slot 13
    bool hits;        // slot 14: offers a hit...
    float distance;   // ... at this distance
    bool triangles;   // slot 1
    uint32_t owner;   // slot 10
};
ObjectScript g_script[kObjects];
bool g_gridsNone;
int g_gridCount;

template <typename F>
F WaFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_waOriginal, address));
}
uint32_t P(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
// An address in the arena as an offset (the same in both runs anyway); other words as they are.
uint32_t Where(uint32_t address)
{
    if (address >= P(&g_wa) && address < P(&g_wa) + sizeof(g_wa))
        return 0xA0000000u + (address - P(&g_wa));
    if (address >= P(g_waCache) && address < P(g_waCache) + sizeof(g_waCache))
        return 0xCA000000u + (address - P(g_waCache));
    return address;
}
int Index(const void* object) { return static_cast<int>((P(object) - P(g_wa.objects)) / sizeof(FakeObject)); }
void Words(const void* p, int n)
{
    const auto* w = static_cast<const uint32_t*>(p);
    for (int i = 0; i < n; ++i)
        g_trace.push_back(Where(w[i]));
}
// A hit record, its callback as whether there is one (the original's relay and ours are different code).
void RecordHit(const CollisionHit* hit)
{
    const auto* w = reinterpret_cast<const uint32_t*>(hit);
    for (int k = 0; k < 0x50 / 4; ++k) {
        uint32_t word = w[k];
        if (k == 0x3C / 4)
            word = word != 0;
        g_trace.push_back(Where(word));
    }
}

// The fake objects' vtable (thiscall emulated with fastcall).
bool __fastcall ObjTriangles(FakeObject* self, void*)
{
    Record({0x0B100001u, static_cast<uint32_t>(Index(self))});
    return g_script[Index(self)].triangles;
}
uint32_t __fastcall ObjOwner(FakeObject* self, void*)
{
    Record({0x0B10000Au, static_cast<uint32_t>(Index(self))});
    return g_script[Index(self)].owner;
}
bool __fastcall ObjTouches(FakeObject* self, void*, const Mth::BBox* box)
{
    Record({0x0B10000Du, static_cast<uint32_t>(Index(self))});
    Words(box, 8);
    return g_script[Index(self)].touches;
}
bool __fastcall ObjCollide(FakeObject* self, void*, const Mth::Line* line, const Vector* direction, CollisionHit* hit,
                           const Mth::BBox* box)
{
    const ObjectScript& script = g_script[Index(self)];
    Record({0x0B10000Eu, static_cast<uint32_t>(Index(self)), Where(P(hit)), hit->owner, hit->callback != nullptr,
            Bits(hit->distance), hit->ignoreFlags, hit->requireFlags, hit->hit});
    Words(line, 8);
    Words(direction, 4);
    Words(box, 8);
    if (!script.hits)
        return false;
    bool farthest = GameVariable<bool>(0x00691910);
    bool better = farthest ? script.distance > hit->distance : script.distance < hit->distance;
    hit->hit = true;
    hit->object = reinterpret_cast<Nx::CCollObj*>(self);
    hit->objectChecksum = self->checksum;
    hit->face = static_cast<uint32_t>(Index(self)) * 3;
    hit->faceFlags = static_cast<uint16_t>(0x100 + Index(self));
    hit->point = {script.distance * 10.0f, 1.0f, 2.0f, 1.0f};
    hit->normal = {0.0f, 1.0f, -script.distance, 0.0f};
    float previous = hit->distance;
    hit->distance = script.distance;
    if (hit->callback)
        hit->callback(hit);
    if (!better)
        hit->distance = previous;
    return better;
}
void* g_objectVtable[22];

// The feeler's callback: what it's given, and the owner being tested then.
void __cdecl FeelerCallback(CFeeler* feeler)
{
    Record({0xFEE1CA11u, Where(P(feeler)), WaFn<uint32_t(__thiscall*)(CFeeler*)>(0x00417140)(feeler)});
}

uint32_t WaBump(uint32_t size)
{
    uint32_t at = P(g_wa.heap) + g_waUsed;
    g_waUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0, size);
    return at;
}

float Rand(Lcg& random, float range) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range; }

// Every query and feeler function: lines with random object scripts (touching, hitting at random distances,
// owners), the static objects through the scenes' grids (none, some) or a cache holding the line's box or not, the
// movable ones through the list or the cache (flags 2 / 4 left out, entries without a box), nearest and farthest,
// with and without a callback, movables wanted or not; the hit object's smart pointer cleared (the object gone);
// the movables' list registered and unregistered; the feeler's constructors and setters on dirty memory.
void TestAnimFeelerQueries()
{
    GameMemory memory({{0x00691910, 1}, {0x00691974, 0x18}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA110C00u, r.args[0]});
        return WaBump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, Where(r.args[0])});
        return 0u;
    });
    s.At({0x0048C980}, 0, [](Regs& r) {
        Record({0x48C98000u});
        Words(reinterpret_cast<void*>(r.args[0]), 8);
        if (g_gridsNone)
            return 0u;
        for (int g = 0; g < 4; ++g)
            g_wa.grids[g] = g < g_gridCount ? 0x6D1D0000u + g : 0;
        return P(g_wa.grids);
    });
    s.At({0x0048C0B0}, 0, [](Regs&) {
        Record({0x48C0B000u});
        return P(g_wa.scenes);
    });
    s.At({0x004188C0, AddressOf(&Nx::CCollGrid::ObjectsAlongLine)}, 8, [](Regs& r) {
        uint32_t grid = r.ecx & 0xF;
        Record({0x4188C000u, r.ecx, r.args[1] != 0});
        Words(reinterpret_cast<void*>(r.args[0]), 8);
        if (r.args[1])
            *reinterpret_cast<int*>(r.args[1]) = 7;
        return P(g_wa.lists[grid % 3]);
    });

    auto runOnce = [&](bool original) {
        g_waOriginal = original;
        g_trace.clear();
        std::memset(&g_wa, 0xB5, sizeof(g_wa));
        std::memset(g_waCache, 0xB6, sizeof(g_waCache));
        g_waUsed = 0;
        memory.Fill(0x4106C0);
        GameVariable<void*>(0x007D1800) = g_wa.manager;
        GameVariable<bool>(0x00691910) = false;
        if (!original)
            UseOurCollisionVtables();
        // the movables' list: empty
        auto& head = GameVariable<Mem::ListNode>(0x00691978);
        head.vtable = reinterpret_cast<void**>(0x00645D34);
        head.data = reinterpret_cast<void*>(-1);
        head.priority = 0;
        head.next = &head;
        head.prev = &head;
        GameVariable<CCollCache*>(0x00691974) = nullptr;
        Lcg random(0x4172F0);
        for (int i = 0; i < kObjects; ++i) {
            FakeObject& o = g_wa.objects[i];
            o.vtable = g_objectVtable;
            o.checksum = 0xC0110000u + i;
            o.flags = static_cast<uint16_t>(i == 8 ? 2 : i == 10 ? 4 : i == 7 ? 0x101 : 0);
            o.terrain = 0;
            o.geometry = 0;
            g_wa.boxes[i].min = {Rand(random, 50), Rand(random, 50), Rand(random, 50), 1};
            g_wa.boxes[i].max = {g_wa.boxes[i].min.x + 30, g_wa.boxes[i].min.y + 30, g_wa.boxes[i].min.z + 30, 1};
        }
        for (int o = 0; o < kOwners; ++o) {
            std::memset(g_wa.owners[o], 0, sizeof(g_wa.owners[o]));
            *reinterpret_cast<uint32_t*>(g_wa.owners[o] + 0x14) = 0x1D000000u + o;
        }
        for (int g = 0; g < 4; ++g)
            g_wa.scenes[g] = 0x5CE00000u + g;
        // the grids' lists: 0: statics 0, 1, 2; 1: 3, 4; 2: 5, 0 (an object in two grids)
        const int lists[3][4] = {{0, 1, 2, -1}, {3, 4, -1, -1}, {5, 0, -1, -1}};
        for (int l = 0; l < 3; ++l) {
            int n = 0;
            for (int k = 0; k < 4 && lists[l][k] >= 0; ++k)
                g_wa.lists[l][n++] = P(&g_wa.objects[lists[l][k]]);
            g_wa.lists[l][n] = 0;
        }
        // the movables registered (6 .. 11, 9 twice), 9 unregistered (both nodes), 3 (never registered) too
        auto registerMovable = WaFn<void(__cdecl*)(void*)>(0x004177B0);
        auto unregisterMovable = WaFn<void(__cdecl*)(void*)>(0x00417800);
        unregisterMovable(&g_wa.objects[6]); // (an empty list)
        for (int i : {6, 9, 7, 8, 9, 10, 11})
            registerMovable(&g_wa.objects[i]);
        unregisterMovable(&g_wa.objects[9]);
        unregisterMovable(&g_wa.objects[3]);
        registerMovable(&g_wa.objects[9]);
        unregisterMovable(&g_wa.objects[6]); // (the first)
        registerMovable(&g_wa.objects[6]);
        for (Mem::ListNode* node = head.next; node != &head && g_trace.size() < 100000; node = node->next)
            Record({Where(P(node)), static_cast<uint32_t>(Index(node->data))});
        // the cache: statics 0, 2, 5 (5 with a far box), movables 6, 7, 9, 11 (11 without a box)
        auto* cache = reinterpret_cast<CCollCache*>(g_waCache);
        cache->m_box.min = {-100, -100, -100, 1};
        cache->m_box.max = {100, 100, 100, 1};
        const int entries[] = {0, 2, 5, 6, 7, 9, 11};
        for (int e = 0; e < 7; ++e) {
            cache->m_entries[e].object = reinterpret_cast<Nx::CCollObj*>(&g_wa.objects[entries[e]]);
            cache->m_entries[e].geometry = entries[e] == 11 ? nullptr : &g_wa.boxes[entries[e]];
        }
        g_wa.boxes[5].min = {500, 500, 500, 1};
        g_wa.boxes[5].max = {600, 600, 600, 1};
        cache->m_statics = 3;
        cache->m_movables = 4;
        cache->m_count = 7;

        auto* feeler = reinterpret_cast<CFeeler*>(g_wa.feeler);
        Record({Where(P(WaFn<CFeeler*(__thiscall*)(CFeeler*)>(0x00417760)(feeler)))});
        Words(feeler, 0xB0 / 4);
        std::memset(feeler, 0xC3, sizeof(g_wa.feeler));
        Vector a{1, 2, 3, 4}, b{5, 6, 7, 8};
        Record({Where(P(WaFn<CFeeler*(__thiscall*)(CFeeler*, const Vector*, const Vector*)>(0x00417250)(feeler, &a,
                                                                                                        &b)))});
        Words(feeler, 0xB0 / 4);
        WaFn<void(__thiscall*)(CFeeler*, uint32_t, uint32_t)>(0x004170F0)(feeler, 0xFFFF1234u, 0xABCD0050u);
        Words(feeler, 0xB0 / 4);
        WaFn<void(__thiscall*)(CFeeler*, uint32_t, uint32_t, uint32_t, int)>(0x00417150)(feeler, 1, 2, 3, 4);

        auto getCollision = WaFn<int(__thiscall*)(CFeeler*, bool, bool)>(0x004172F0);
        auto queries = [](int which) {
            static const uint32_t addresses[] = {0x004106C0, 0x00410930, 0x004118C0, 0x00411AE0};
            return WaFn<uint32_t(__cdecl*)(const Mth::Line*, CollisionHit*, void*, CCollCache*)>(addresses[which]);
        };
        for (int q = 0; q < 400; ++q) {
            for (int i = 0; i < kObjects; ++i) {
                ObjectScript& script = g_script[i];
                script.touches = random.Below(4) != 0;
                script.hits = random.Below(3) == 0;
                script.distance = static_cast<float>(random.Below(1000)) / 999.0f;
                script.triangles = random.Below(2) != 0;
                uint32_t owner = random.Below(kOwners + 1);
                script.owner = owner == kOwners ? 0 : P(g_wa.owners[owner]);
            }
            g_gridsNone = random.Below(5) == 0;
            g_gridCount = static_cast<int>(random.Below(4));
            Vector start{Rand(random, 80), Rand(random, 80), Rand(random, 80), 1};
            Vector end{Rand(random, 80), Rand(random, 80), Rand(random, 80), random.Below(2) ? 1.0f : 1.5f};
            if (random.Below(6) == 0)
                end = {start.x + 300, start.y, start.z, 1}; // (out of the cache's box)
            uint32_t cacheChoice = random.Below(3);
            bool movables = random.Below(3) != 0, farthest = random.Below(3) == 0;
            if (q < 300) {
                CFeeler* f = feeler;
                f->m_callback = random.Below(2) ? &FeelerCallback : nullptr;
                f->mp_cache = cacheChoice == 1 ? cache : nullptr;
                GameVariable<CCollCache*>(0x00691974) = cacheChoice == 2 ? cache : nullptr;
                WaFn<void(__thiscall*)(CFeeler*, uint32_t, uint32_t)>(0x004170F0)(f, random.Below(0x40),
                                                                                  random.Below(4) << 4);
                int result;
                if (q % 4 == 3) {
                    result = WaFn<int(__thiscall*)(CFeeler*, const Vector*, const Vector*, bool)>(0x00417710)(
                        f, &start, &end, movables);
                } else {
                    if (q % 3 == 0) {
                        WaFn<void(__thiscall*)(CFeeler*, const Vector*)>(0x00417190)(f, &start);
                        WaFn<void(__thiscall*)(CFeeler*, const Vector*)>(0x004171B0)(f, &end);
                    } else {
                        WaFn<void(__thiscall*)(CFeeler*, const Vector*, const Vector*)>(0x004171D0)(f, &start, &end);
                    }
                    result = getCollision(f, movables, farthest);
                }
                Record({0x5E5017u, static_cast<uint32_t>(result)});
                RecordHit(&f->m_hit);
                Words(&f->m_distance, (0xB0 - 0x74) / 4);
                Record({static_cast<uint32_t>(WaFn<bool(__thiscall*)(CFeeler*)>(0x00417120)(f)),
                        WaFn<uint32_t(__thiscall*)(CFeeler*)>(0x00417140)(f),
                        Where(WaFn<uint32_t(__thiscall*)(CFeeler*)>(0x00417240)(f))});
                if (q % 5 == 1 && f->m_object.mp_object) // the object gone: its smart pointers cleared
                    GameFunction<void(__thiscall*)(void*)>(0x00463820)(f->m_object.mp_object);
                Record({static_cast<uint32_t>(WaFn<bool(__thiscall*)(CFeeler*)>(0x00417210)(f))});
                RecordHit(&f->m_hit);
                Words(&f->m_object, 4);
                if (q % 7 == 0) { // the relay itself
                    f->m_callback = &FeelerCallback;
                    CollisionHit relayed{};
                    relayed.callbackData = P(f);
                    WaFn<void(__cdecl*)(CollisionHit*)>(0x00417110)(&relayed);
                }
            } else {
                // the queries themselves, on a record that may already have a hit
                Mth::Line line;
                line.start = start;
                line.end = end;
                CollisionHit& hit = g_wa.hit;
                std::memset(&hit, 0, sizeof(hit));
                hit.hit = random.Below(2) != 0;
                hit.distance = 0.5f;
                hit.callbackData = P(feeler);
                feeler->m_callback = &FeelerCallback;
                void* callback = random.Below(2) ? Code(g_waOriginal, 0x00417110) : nullptr;
                int which = static_cast<int>(random.Below(4));
                uint32_t result = queries(which)(&line, &hit, callback, cacheChoice ? cache : nullptr);
                Record({0x9E5017u, static_cast<uint32_t>(which), which < 2 ? result & 0xFF : Where(result)});
                RecordHit(&hit);
            }
            Record({GameVariable<uint8_t>(0x00691910)});
        }
        EndRunPatches();
        for (int i = 0; i < kObjects; ++i)
            Record({g_wa.objects[i].flags});
        for (Mem::ListNode* node = head.next; node != &head && g_trace.size() < 1000000; node = node->next)
            Record({static_cast<uint32_t>(Index(node->data))});
        Words(g_wa.heap, sizeof(g_wa.heap) / 4);
        return g_trace;
    };
    for (int slot = 0; slot < 22; ++slot)
        g_objectVtable[slot] = nullptr;
    g_objectVtable[1] = reinterpret_cast<void*>(&ObjTriangles);
    g_objectVtable[10] = reinterpret_cast<void*>(&ObjOwner);
    g_objectVtable[13] = reinterpret_cast<void*>(&ObjTouches);
    g_objectVtable[14] = reinterpret_cast<void*>(&ObjCollide);
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    Log("feeler: %u trace words", static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx world line queries and the feeler");
}
SELFTEST(Early, TestAnimFeelerQueries, TestStandIns::RemoveStandIns);

} // namespace
