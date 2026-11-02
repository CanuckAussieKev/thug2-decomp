// The world's collision: the movable objects' list and the line queries over the scenes' grids and the movables.

#include <array>
#include <cstddef>
#include <span>

#include "engine/render/coll_world.h"

#include "engine/maths/shape_tests.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_line.h"
#include "engine/render/coll_obj.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/sys/startup_init.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"
#include "engine/render/collision_state.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"

namespace render {

CollisionState& collision_state()
{
    static core::NoDestroy<CollisionState> state;
    return state.get();
}

namespace {

// Where a query's distance to beat starts: past the line's end, or (for the farthest) before its start.
constexpr float kNearestStart = 1.01f;
constexpr float kFarthestStart = -0.01f;

// The scenes' collision grids the line passes near (null-terminated; null: none) and the scenes themselves in the
// same order (0x0048C980 / 0x0048C0B0).
std::span<CollGrid* const> grids_along(const maths::Seg* line) { return get_coll_grids_on_line(line); }
const std::array<WorldScene*, 16>& grid_scenes() { return render::engine_parts().get_collision_scenes(); }

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
        direction = {.x = line->end.x - line->start.x, .y = line->end.y - line->start.y, .z = line->end.z - line->start.z,
                     .w = line->end.w - line->start.w};
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
objects::GameEntity* get_owner(const HitMesh* object) { return object->get_owner(); }

// A movable object tested: its owner noted on the hit while it is; the owner if it was hit, else `owner`.
objects::GameEntity* test_movable(HitMesh* object, const maths::Seg* line, LineQuery& query, CollisionHit* hit,
                                  objects::GameEntity* owner)
{
    hit->owner = get_owner(object);
    if (collide_with_line(object, line, &query.direction, hit, &query.box))
        owner = get_owner(object);
    hit->owner = nullptr;
    return owner;
}

} // namespace

// 0x004177B0
void register_movable(HitMesh* object)
{
    memory::ListNode* node = memory::ListNode::create(object);
    node->prev = collision_state().movables_.prev;
    node->next = &collision_state().movables_;
    collision_state().movables_.prev->next = node;
    collision_state().movables_.prev = node;
}

// 0x00417800 (the list's end is its head, whose data is -1)
void unregister_movable(HitMesh* object)
{
    memory::ListNode* node = collision_state().movables_.next;
    if (node->data == memory::ListNode::head_marker())
        return;
    while (node) {
        memory::ListNode* next = node->next;
        memory::ListNode* following = next->data == memory::ListNode::head_marker() ? nullptr : next;
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
    hit->distance = collision_state().want_farthest_ ? kFarthestStart : kNearestStart;
    bool any = false;
    if (query.use_cache_) {
        for (int i = 0; i < cache->statics_; ++i) {
            const HitMeshCache::TableRow& entry = core::at(cache->entries_, i);
            if (query.box.intersects(*entry_box(entry)) &&
                collide_with_line(entry.object, line, &query.direction, hit, &query.box))
                any = true;
        }
        return any;
    }
    const std::span<CollGrid* const> grids = grids_along(line);
    if (grids.empty())
        return false;
    const auto& scenes = grid_scenes();
    hit->scene = nullptr;
    for (size_t scene = 0; scene < grids.size(); ++scene) { // (the scene of the grid walked)
        int count = 0;
        for (HitMesh* const object : core::at(grids, scene)->objects_along_line(line, &count)) {
            if (may_touch(object, &query.box) &&
                collide_with_line(object, line, &query.direction, hit, &query.box)) {
                any = true;
                hit->scene = core::at(scenes, scene);
            }
        }
    }
    return any;
}

// 0x00410930
bool collide_with_statics_farthest(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                   HitMeshCache* cache)
{
    collision_state().want_farthest_ = true;
    const bool any = collide_with_statics(line, hit, callback, cache);
    collision_state().want_farthest_ = false;
    return any;
}

// 0x004118C0
objects::GameEntity* collide_with_movables(const maths::Seg* line, CollisionHit* hit, CollisionHitCallback callback,
                                           HitMeshCache* cache)
{
    hit->callback = callback;
    objects::GameEntity* owner = nullptr;
    if (!hit->hit)
        hit->distance = collision_state().want_farthest_ ? kFarthestStart : kNearestStart;
    LineQuery query(line, cache);
    if (query.use_cache_) {
        const std::span<const HitMeshCache::TableRow> entries =
            std::span<const HitMeshCache::TableRow>(cache->entries_).subspan(static_cast<size_t>(cache->statics_));
        for (int i = 0; i < cache->movables_; ++i) {
            if (entry_box(core::at(entries, i)) && query.box.intersects(*entry_box(core::at(entries, i))))
                owner = test_movable(core::at(entries, i).object, line, query, hit, owner);
        }
        return owner;
    }
    for (const memory::ListNode* node = collision_state().movables_.next; node->data != memory::ListNode::head_marker() && node;
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
    collision_state().want_farthest_ = true;
    objects::GameEntity* owner = collide_with_movables(line, hit, callback, cache);
    collision_state().want_farthest_ = false;
    return owner;
}

} // namespace render

namespace render {

// 0x00640880 (a static initialiser): the movables' list, an empty head.
void construct_movables() { collision_state().movables_.make_head(); }

} // namespace render
