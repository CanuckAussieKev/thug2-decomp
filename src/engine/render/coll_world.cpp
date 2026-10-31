// The world's collision: the movable objects' list and the line queries over the scenes' grids and the movables.

#include <cstdint>

#include "engine/render/coll_world.h"

#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_line.h"
#include "engine/render/coll_obj.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/sys/startup_init.h"

namespace Engine3D {

// The movable objects' list (its head).
MemSys::ListNode g_movables;

namespace {

// Where a query's distance to beat starts: past the line's end, or (for the farthest) before its start.
constexpr float kNearestStart = 1.01f;
constexpr float kFarthestStart = -0.01f;

// The scenes' collision grids the line passes near (null-terminated; null: none) and the scenes themselves in the
// same order (0x0048C980 / 0x0048C0B0).
CCollGrid** GridsAlong(const Maths::Seg* line)
{
    return GetCollGridsOnLine(line);
}
uint32_t* GridScenes() { return Engine3D::Engine::GetCollisionScenes(); }

// What every query starts with: the line's box (grown by 0.5 each way), whether the cache holds the unchanged box,
// and the line's direction.
struct LineQuery {
    Maths::BBox box;
    Maths::Vec4 direction;
    bool useCache;

    LineQuery(const Maths::Seg* line, CHitMeshCache* cache) : box(line->start)
    {
        box.AppendSpot(line->end);
        useCache = cache && cache->m_box.Holds(box);
        Maths::Vec4 grown = box.max;
        grown.x += 0.5f;
        grown.y += 0.5f;
        grown.z += 0.5f;
        box.AppendSpot(grown);
        grown = box.min;
        grown.x -= 0.5f;
        grown.y -= 0.5f;
        grown.z -= 0.5f;
        box.AppendSpot(grown);
        direction = {line->end.x - line->start.x, line->end.y - line->start.y, line->end.z - line->start.z,
                     line->end.w - line->start.w};
    }
};

// A cache entry's box (what the cache keeps for each object).
const Maths::BBox* EntryBox(const CHitMeshCache::TableRow& entry) { return static_cast<const Maths::BBox*>(entry.geometry); }

bool MayTouch(CHitMesh* object, const Maths::BBox* box) { return object->Call<bool>(13, box); }
bool CollideWithLine(CHitMesh* object, const Maths::Seg* line, Maths::Vec4* direction, CollisionHit* hit,
                     Maths::BBox* box)
{
    return object->Call<bool>(14, line, direction, hit, box);
}
GameObjects::CGameEntity* Owner(CHitMesh* object) { return object->Call<GameObjects::CGameEntity*>(10); }

// A movable object tested: its owner noted on the hit while it is; the owner if it was hit, else `owner`.
GameObjects::CGameEntity* TestMovable(CHitMesh* object, const Maths::Seg* line, LineQuery& query, CollisionHit* hit,
                                   GameObjects::CGameEntity* owner)
{
    hit->owner = reinterpret_cast<uint32_t>(Owner(object));
    if (CollideWithLine(object, line, &query.direction, hit, &query.box))
        owner = Owner(object);
    hit->owner = 0;
    return owner;
}

} // namespace

// 0x004177B0
void RegisterMovable(CHitMesh* object)
{
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->vtable = kNodeVtable;
        node->data = object;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    node->prev = g_movables.prev;
    node->next = &g_movables;
    g_movables.prev->next = node;
    g_movables.prev = node;
}

// 0x00417800 (the list's end is its head, whose data is -1)
void UnregisterMovable(CHitMesh* object)
{
    MemSys::ListNode* node = g_movables.next;
    if (node->data == reinterpret_cast<void*>(-1))
        return;
    while (node) {
        MemSys::ListNode* next = node->next;
        MemSys::ListNode* following = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        if (node->data == object)
            reinterpret_cast<void*(__thiscall*)(MemSys::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
    }
}

// 0x004106C0
bool CollideWithStatics(const Maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback, CHitMeshCache* cache)
{
    hit->callback = callback;
    LineQuery query(line, cache);
    hit->distance = g_wantFarthest ? kFarthestStart : kNearestStart;
    bool any = false;
    if (query.useCache) {
        for (int i = 0; i < cache->m_statics; ++i) {
            const CHitMeshCache::TableRow& entry = cache->m_entries[i];
            if (query.box.Intersects(*EntryBox(entry)) &&
                CollideWithLine(entry.object, line, &query.direction, hit, &query.box))
                any = true;
        }
        return any;
    }
    CCollGrid** grids = GridsAlong(line);
    if (!grids)
        return false;
    uint32_t* scenes = GridScenes();
    hit->scene = 0;
    for (; *grids; ++grids, ++scenes) {
        int count;
        for (CHitMesh** objects = (*grids)->ObjectsAlongLine(line, &count); *objects; ++objects) {
            if (MayTouch(*objects, &query.box) &&
                CollideWithLine(*objects, line, &query.direction, hit, &query.box)) {
                any = true;
                hit->scene = *scenes;
            }
        }
    }
    return any;
}

// 0x00410930
bool CollideWithStaticsFarthest(const Maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                CHitMeshCache* cache)
{
    g_wantFarthest = true;
    bool any = CollideWithStatics(line, hit, callback, cache);
    g_wantFarthest = false;
    return any;
}

// 0x004118C0
GameObjects::CGameEntity* CollideWithMovables(const Maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                           CHitMeshCache* cache)
{
    hit->callback = callback;
    GameObjects::CGameEntity* owner = nullptr;
    if (!hit->hit)
        hit->distance = g_wantFarthest ? kFarthestStart : kNearestStart;
    LineQuery query(line, cache);
    if (query.useCache) {
        const CHitMeshCache::TableRow* entries = &cache->m_entries[cache->m_statics];
        for (int i = 0; i < cache->m_movables; ++i) {
            if (EntryBox(entries[i]) && query.box.Intersects(*EntryBox(entries[i])))
                owner = TestMovable(entries[i].object, line, query, hit, owner);
        }
        return owner;
    }
    for (MemSys::ListNode* node = g_movables.next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next) {
        auto* object = static_cast<CHitMesh*>(node->data);
        if (object && !(object->m_flagBits & 6) && MayTouch(object, &query.box))
            owner = TestMovable(object, line, query, hit, owner);
    }
    return owner;
}

// 0x00411AE0
GameObjects::CGameEntity* CollideWithMovablesFarthest(const Maths::Seg* line, CollisionHit* hit,
                                                   CollisionHitCallback callback, CHitMeshCache* cache)
{
    g_wantFarthest = true;
    GameObjects::CGameEntity* owner = CollideWithMovables(line, hit, callback, cache);
    g_wantFarthest = false;
    return owner;
}

} // namespace Nx

namespace Engine3D {

// 0x00640880 (a static initialiser): the movables' list, an empty head.
void ConstructMovables() { reinterpret_cast<CollObjListNodes*>(&g_movables)->ConstructHead(); }

} // namespace Nx
