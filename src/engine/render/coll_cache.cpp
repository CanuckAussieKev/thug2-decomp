// Collision caches.

#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_obj.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <array>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/render/coll_world.h"
#include "engine/core/checked_index.h"
#include "engine/render/collision_state.h"
#include "engine/core/no_destroy.h"

namespace render {

// The collision caches there can be.
constexpr std::size_t kCacheSlotCount = 8;
constexpr int kCacheSlotIndexLimit = 8; // the same count, for the loops over the slots

// The checksums shape_type_from_checksum tells apart.
constexpr uint32_t kBoxShapeAlias1 = 0xDD42AA66U; // text unknown (a box shape)
constexpr uint32_t kBoxShapeAlias2 = 0x2BA71070U; // text unknown (a box shape)
constexpr uint32_t kBoundingBox = 0xE460ABDEU;    // BoundingBox
constexpr uint32_t kGeometry = 0x6AADF154U;       // Geometry (a sector)

namespace {

// NOT ORIGINAL grouping - the collision caches and how many are in use. Made on first use, never destroyed
// (cache_slots()).
struct CacheSlots {
    std::array<HitMeshCache*, kCacheSlotCount> caches_{};
    uint32_t caches_in_use_{};
};

CacheSlots& cache_slots()
{
    static core::NoDestroy<CacheSlots> state;
    return state.get();
}

std::array<HitMeshCache*, kCacheSlotCount>& caches() { return cache_slots().caches_; }

uint32_t& caches_in_use() { return cache_slots().caches_in_use_; }

// The movable collision objects (the first node of their list: 0x00691984).
memory::ListNode* first_movable() { return collision_state().movables().next(); }

// The collision grid lists the line from the box's min to its max passes through (null-terminated; null: none),
// and one list's collision objects near the line (null-terminated). 0x0048C980 / 0x004188C0.
std::span<CollGrid* const> grid_lists_along(maths::Seg* line) { return get_coll_grids_on_line(line); }
std::span<HitMesh* const> objects_along(CollGrid* list, maths::Seg* line)
{
    return list->objects_along_line(line, nullptr);
}

void get_clear(HitMeshCache* cache)
{
    cache->box().min = {.x = kEmptyBoxExtent, .y = kEmptyBoxExtent, .z = kEmptyBoxExtent, .w = 1.0F};
    cache->box().max = {.x = -kEmptyBoxExtent, .y = -kEmptyBoxExtent, .z = -kEmptyBoxExtent, .w = 1.0F};
    cache->set_num(0);
    cache->set_statics(0);
    cache->set_movables(0);
}

} // namespace

// 0x0040AB50
HitMeshCache::HitMeshCache()
{
    register_coll_cache(this);
    get_clear(this);
}

// 0x0040AB40
HitMeshCache::~HitMeshCache() { unregister_coll_cache(this); }

// 0x0040ABA0
void HitMeshCache::tick_frame(const maths::BBox& box)
{
    get_clear(this);
    maths::Seg line(box.min, box.max);
    const std::span<CollGrid* const> lists = grid_lists_along(&line);
    if (lists.empty()) {
        return;
    }
    box_ = box;
    // The box the objects are tested against: grown by half a unit each way.
    maths::BBox grown;
    grown.append_spot({.x = box.max.x + kQueryBoxMargin, .y = box.max.y + kQueryBoxMargin, .z = box.max.z + kQueryBoxMargin, .w = box.max.w});
    grown.append_spot({.x = box.min.x - kQueryBoxMargin, .y = box.min.y - kQueryBoxMargin, .z = box.min.z - kQueryBoxMargin, .w = box.min.w});
    for (CollGrid* const list : lists) {
        for (HitMesh* const object : objects_along(list, &line)) {
            if (!object->inside_bounds(&grown)) {
                continue;
            }
            core::at(entries_, num_).bounds = object->get_world_bounds();
            core::at(entries_, num_).object = object;
            ++num_;
            ++statics_;
        }
    }
    for (const memory::ListNode* node = first_movable(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next()) {
        auto* object = static_cast<HitMesh*>(node->data());
        if (!object || ((object->flag_bits() & kUnlistedFlags) != 0) || !object->inside_bounds(&grown)) {
            continue;
        }
        core::at(entries_, num_).bounds = object->get_local_bounds();
        core::at(entries_, num_).object = object;
        ++num_;
        ++movables_;
    }
}

// 0x0040A910
void HitMeshCache::remove_static(HitMesh* object)
{
    for (int i = 0; i < statics_; ++i) {
        if (core::at(entries_, i).object != object) {
            continue;
        }
        for (int k = i + 1; k < num_; ++k) {
            core::at(entries_, k - 1) = core::at(entries_, k);
        }
        --statics_;
        --num_;
        return;
    }
}

// 0x0040A990
void HitMeshCache::remove_movable(HitMesh* object)
{
    for (int i = statics_; i < num_; ++i) {
        if (core::at(entries_, i).object != object) {
            continue;
        }
        --num_;
        core::at(entries_, i) = core::at(entries_, num_);
        --movables_;
        return;
    }
}

// 0x0040AA00
void register_coll_cache(HitMeshCache* cache)
{
    for (int i = 0; i < kCacheSlotIndexLimit; ++i) {
        if ((caches_in_use() & (1U << i)) == 0U) {
            core::at(caches(), i) = cache;
            caches_in_use() |= 1U << i;
            return;
        }
    }
}

// 0x0040AA40
void unregister_coll_cache(HitMeshCache* cache)
{
    for (int i = 0; i < kCacheSlotIndexLimit; ++i) {
        if (((caches_in_use() & (1U << i)) != 0U) && core::at(caches(), i) == cache) {
            caches_in_use() &= ~(1U << i);
            return;
        }
    }
}

// 0x0040AA80
void remove_movable_from_caches(HitMesh* object)
{
    for (int i = 0; i < kCacheSlotIndexLimit; ++i) {
        if ((caches_in_use() & (1U << i)) != 0U) {
            core::at(caches(), i)->remove_movable(object);
        }
    }
}

// 0x0040AAC0
void remove_from_caches(HitMesh* object)
{
    for (int i = 0; i < kCacheSlotIndexLimit; ++i) {
        if ((caches_in_use() & (1U << i)) != 0U) {
            core::at(caches(), i)->remove_static(object);
            core::at(caches(), i)->remove_movable(object);
        }
    }
}

// 0x0040ADD0
int shape_type_from_checksum(uint32_t shape)
{
    switch (shape) {
    case kBoxShapeAlias1:
    case kBoxShapeAlias2:
    case kBoundingBox: // box
        return 1;
    case kGeometry: // sector
        return 4;
    default:
        return 0;
    }
}

} // namespace render
