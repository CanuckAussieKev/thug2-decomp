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

namespace render {

// The movable objects' list (its head).
memory::ListNode g_movables;

namespace {

// Where a query's distance to beat starts: past the line's end, or (for the farthest) before its start.
constexpr float kNearestStart = 1.01f;
constexpr float kFarthestStart = -0.01f;

// The scenes' collision grids the line passes near (null-terminated; null: none) and the scenes themselves in the
// same order (0x0048C980 / 0x0048C0B0).
CollGrid** grids_along(const maths::Seg* line) { return get_coll_grids_on_line(line); }
uint32_t* grid_scenes() { return render::engine::get_collision_scenes(); }

// What every query starts with: the line's box (grown by 0.5 each way), whether the cache holds the unchanged box,
// and the line's direction.
struct LineQuery {
    maths::BBox box;
    maths::Vec4 direction{};
    bool use_cache_;

    LineQuery(const maths::Seg* line, HitMeshCache* cache) : box(line->start)
    {
        box.append_spot(line->end);
        // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): after calls that may see it - the original's order
        use_cache_ = cache && cache->box_.get_holds(box);
        // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
        maths::Vec4 grown = box.max;
        grown.x += 0.5f;
        grown.y += 0.5f;
        grown.z += 0.5f;
        box.append_spot(grown);
        grown = box.min;
        grown.x -= 0.5f;
        grown.y -= 0.5f;
        grown.z -= 0.5f;
        box.append_spot(grown);
        direction = {line->end.x - line->start.x, line->end.y - line->start.y, line->end.z - line->start.z,
                     line->end.w - line->start.w};
    }
};

// A cache entry's box (what the cache keeps for each object).
const maths::BBox* entry_box(const HitMeshCache::TableRow& entry) { return entry.bounds; }

bool may_touch(HitMesh* object, const maths::BBox* box) { return object->inside_bounds(box); }
bool collide_with_line(HitMesh* object, const maths::Seg* line, maths::Vec4* direction, CollisionHit* hit,
                       maths::BBox* box)
{
    return object->collide_with_line(line, direction, hit, box);
}
objects::GameEntity* get_owner(HitMesh* object) { return reinterpret_cast<objects::GameEntity*>(object->get_owner()); }

// A movable object tested: its owner noted on the hit while it is; the owner if it was hit, else `owner`.
objects::GameEntity* test_movable(HitMesh* object, const maths::Seg* line, LineQuery& query, CollisionHit* hit,
                                  objects::GameEntity* owner)
{
    hit->owner = reinterpret_cast<uint32_t>(get_owner(object));
    if (collide_with_line(object, line, &query.direction, hit, &query.box))
        owner = get_owner(object);
    hit->owner = 0;
    return owner;
}

} // namespace

// 0x004177B0
void register_movable(HitMesh* object)
{
    memory::ListNode* node = memory::ListNode::create(object);
    node->prev = g_movables.prev;
    node->next = &g_movables;
    g_movables.prev->next = node;
    g_movables.prev = node;
}

// 0x00417800 (the list's end is its head, whose data is -1)
void unregister_movable(HitMesh* object)
{
    memory::ListNode* node = g_movables.next;
    if (node->data == reinterpret_cast<void*>(-1))
        return;
    while (node) {
        memory::ListNode* next = node->next;
        memory::ListNode* following = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        if (node->data == object)
            memory::ListNode::destroy(node);
        node = following;
    }
}

// 0x004106C0
bool collide_with_statics(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback, HitMeshCache* cache)
{
    hit->callback = callback;
    LineQuery query(line, cache);
    hit->distance = g_want_farthest ? kFarthestStart : kNearestStart;
    bool any = false;
    if (query.use_cache_) {
        for (int i = 0; i < cache->statics_; ++i) {
            const HitMeshCache::TableRow& entry = cache->entries_[i];
            if (query.box.intersects(*entry_box(entry)) &&
                collide_with_line(entry.object, line, &query.direction, hit, &query.box))
                any = true;
        }
        return any;
    }
    CollGrid** grids = grids_along(line);
    if (!grids)
        return false;
    const uint32_t* scenes = grid_scenes();
    hit->scene = 0;
    for (; *grids; ++grids, ++scenes) {
        int count = 0;
        for (HitMesh* const* objects = (*grids)->objects_along_line(line, &count); *objects; ++objects) {
            if (may_touch(*objects, &query.box) &&
                collide_with_line(*objects, line, &query.direction, hit, &query.box)) {
                any = true;
                hit->scene = *scenes;
            }
        }
    }
    return any;
}

// 0x00410930
bool collide_with_statics_farthest(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                   HitMeshCache* cache)
{
    g_want_farthest = true;
    const bool any = collide_with_statics(line, hit, callback, cache);
    g_want_farthest = false;
    return any;
}

// 0x004118C0
objects::GameEntity* collide_with_movables(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                           HitMeshCache* cache)
{
    hit->callback = callback;
    objects::GameEntity* owner = nullptr;
    if (!hit->hit)
        hit->distance = g_want_farthest ? kFarthestStart : kNearestStart;
    LineQuery query(line, cache);
    if (query.use_cache_) {
        const HitMeshCache::TableRow* entries = &cache->entries_[cache->statics_];
        for (int i = 0; i < cache->movables_; ++i) {
            if (entry_box(entries[i]) && query.box.intersects(*entry_box(entries[i])))
                owner = test_movable(entries[i].object, line, query, hit, owner);
        }
        return owner;
    }
    for (const memory::ListNode* node = g_movables.next; node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next) {
        auto* object = static_cast<HitMesh*>(node->data);
        if (object && !(object->flag_bits_ & 6) && may_touch(object, &query.box))
            owner = test_movable(object, line, query, hit, owner);
    }
    return owner;
}

// 0x00411AE0
objects::GameEntity* collide_with_movables_farthest(const maths::Seg* line, CollisionHit* hit,
                                                    CollisionHitCallback callback, HitMeshCache* cache)
{
    g_want_farthest = true;
    objects::GameEntity* owner = collide_with_movables(line, hit, callback, cache);
    g_want_farthest = false;
    return owner;
}

} // namespace render

namespace render {

// 0x00640880 (a static initialiser): the movables' list, an empty head.
void construct_movables() { g_movables.make_head(); }

} // namespace render
