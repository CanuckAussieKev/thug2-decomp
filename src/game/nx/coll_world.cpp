// The world's collision: the movable objects' list and the line queries over the scenes' grids and the movables.

#include "game/nx/coll_world.h"

#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/engine.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

void** const kNodeVtable = reinterpret_cast<void**>(0x00645AA0); // Lst::Node<CCollObj>

Mem::ListNode& s_movables = GameVariable<Mem::ListNode>(0x00691978);
// Set while a caller wants the farthest hit rather than the nearest (ReportHit reads it).
bool& s_wantFarthest = GameVariable<bool>(0x00691910);

// Where a query's distance to beat starts: past the line's end, or (for the farthest) before its start.
constexpr float kNearestStart = 1.01f;
constexpr float kFarthestStart = -0.01f;

// The scenes' collision grids the line passes near (null-terminated; null: none) and the scenes themselves in the
// same order (0x0048C980 / 0x0048C0B0).
CCollGrid** GridsAlong(const Mth::Line* line)
{
    return GetCollGridsOnLine(line);
}
uint32_t* GridScenes() { return Nx::Engine::GetCollisionScenes(); }

// What every query starts with: the line's box (grown by 0.5 each way), whether the cache holds the unchanged box,
// and the line's direction.
struct LineQuery {
    Mth::BBox box;
    Mth::Vector direction;
    bool useCache;

    LineQuery(const Mth::Line* line, CCollCache* cache) : box(line->start)
    {
        box.AddPoint(line->end);
        useCache = cache && cache->m_box.Contains(box);
        Mth::Vector grown = box.max;
        grown.x += 0.5f;
        grown.y += 0.5f;
        grown.z += 0.5f;
        box.AddPoint(grown);
        grown = box.min;
        grown.x -= 0.5f;
        grown.y -= 0.5f;
        grown.z -= 0.5f;
        box.AddPoint(grown);
        direction = {line->end.x - line->start.x, line->end.y - line->start.y, line->end.z - line->start.z,
                     line->end.w - line->start.w};
    }
};

// A cache entry's box (what the cache keeps for each object).
const Mth::BBox* EntryBox(const CCollCache::Entry& entry) { return static_cast<const Mth::BBox*>(entry.geometry); }

bool MayTouch(CCollObj* object, const Mth::BBox* box) { return object->Call<bool>(13, box); }
bool CollideWithLine(CCollObj* object, const Mth::Line* line, Mth::Vector* direction, CollisionHit* hit,
                     Mth::BBox* box)
{
    return object->Call<bool>(14, line, direction, hit, box);
}
Obj::CCompositeObject* Owner(CCollObj* object) { return object->Call<Obj::CCompositeObject*>(10); }

// A movable object tested: its owner noted on the hit while it is; the owner if it was hit, else `owner`.
Obj::CCompositeObject* TestMovable(CCollObj* object, const Mth::Line* line, LineQuery& query, CollisionHit* hit,
                                   Obj::CCompositeObject* owner)
{
    hit->owner = reinterpret_cast<uint32_t>(Owner(object));
    if (CollideWithLine(object, line, &query.direction, hit, &query.box))
        owner = Owner(object);
    hit->owner = 0;
    return owner;
}

} // namespace

// 0x004177B0
void RegisterMovable(CCollObj* object)
{
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->vtable = kNodeVtable;
        node->data = object;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    node->prev = s_movables.prev;
    node->next = &s_movables;
    s_movables.prev->next = node;
    s_movables.prev = node;
}

// 0x00417800 (the list's end is its head, whose data is -1)
void UnregisterMovable(CCollObj* object)
{
    Mem::ListNode* node = s_movables.next;
    if (node->data == reinterpret_cast<void*>(-1))
        return;
    while (node) {
        Mem::ListNode* next = node->next;
        Mem::ListNode* following = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        if (node->data == object)
            reinterpret_cast<void*(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
    }
}

// 0x004106C0
bool CollideWithStatics(const Mth::Line* line, CollisionHit* hit, CollisionHitCallback callback, CCollCache* cache)
{
    hit->callback = callback;
    LineQuery query(line, cache);
    hit->distance = s_wantFarthest ? kFarthestStart : kNearestStart;
    bool any = false;
    if (query.useCache) {
        for (int i = 0; i < cache->m_statics; ++i) {
            const CCollCache::Entry& entry = cache->m_entries[i];
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
        for (CCollObj** objects = (*grids)->ObjectsAlongLine(line, &count); *objects; ++objects) {
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
bool CollideWithStaticsFarthest(const Mth::Line* line, CollisionHit* hit, CollisionHitCallback callback,
                                CCollCache* cache)
{
    s_wantFarthest = true;
    bool any = CollideWithStatics(line, hit, callback, cache);
    s_wantFarthest = false;
    return any;
}

// 0x004118C0
Obj::CCompositeObject* CollideWithMovables(const Mth::Line* line, CollisionHit* hit, CollisionHitCallback callback,
                                           CCollCache* cache)
{
    hit->callback = callback;
    Obj::CCompositeObject* owner = nullptr;
    if (!hit->hit)
        hit->distance = s_wantFarthest ? kFarthestStart : kNearestStart;
    LineQuery query(line, cache);
    if (query.useCache) {
        const CCollCache::Entry* entries = &cache->m_entries[cache->m_statics];
        for (int i = 0; i < cache->m_movables; ++i) {
            if (EntryBox(entries[i]) && query.box.Intersects(*EntryBox(entries[i])))
                owner = TestMovable(entries[i].object, line, query, hit, owner);
        }
        return owner;
    }
    for (Mem::ListNode* node = s_movables.next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next) {
        auto* object = static_cast<CCollObj*>(node->data);
        if (object && !(object->m_flags & 6) && MayTouch(object, &query.box))
            owner = TestMovable(object, line, query, hit, owner);
    }
    return owner;
}

// 0x00411AE0
Obj::CCompositeObject* CollideWithMovablesFarthest(const Mth::Line* line, CollisionHit* hit,
                                                   CollisionHitCallback callback, CCollCache* cache)
{
    s_wantFarthest = true;
    Obj::CCompositeObject* owner = CollideWithMovables(line, hit, callback, cache);
    s_wantFarthest = false;
    return owner;
}

} // namespace Nx

REPLACE(0x004177B0, Nx::RegisterMovable);
REPLACE(0x00417800, Nx::UnregisterMovable);
REPLACE(0x004106C0, Nx::CollideWithStatics);
REPLACE(0x00410930, Nx::CollideWithStaticsFarthest);
REPLACE(0x004118C0, Nx::CollideWithMovables);
REPLACE(0x00411AE0, Nx::CollideWithMovablesFarthest);
