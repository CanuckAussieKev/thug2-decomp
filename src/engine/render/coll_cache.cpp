// Collision caches.

#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_obj.h"

#include <cstdint>
#include <new>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/render/coll_world.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

std::array<HitMeshCache*, 8> g_caches;

std::array<HitMeshCache*, 8>& caches() { return g_caches; }

uint32_t g_caches_in_use;

uint32_t& caches_in_use() { return g_caches_in_use; }

// The movable collision objects (the first node of their list: 0x00691984).
memory::ListNode* first_movable() { return g_movables.next; }

// The collision grid lists the line from the box's min to its max passes through (null-terminated; null: none),
// and one list's collision objects near the line (null-terminated). 0x0048C980 / 0x004188C0.
std::span<CollGrid* const> grid_lists_along(maths::Seg* line) { return get_coll_grids_on_line(line); }
std::span<HitMesh* const> objects_along(CollGrid* list, maths::Seg* line)
{
    return list->objects_along_line(line, nullptr);
}

void get_clear(HitMeshCache* cache)
{
    cache->box_.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    cache->box_.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    cache->num_ = 0;
    cache->statics_ = 0;
    cache->movables_ = 0;
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
    if (lists.empty())
        return;
    box_ = box;
    // The box the objects are tested against: grown by half a unit each way.
    maths::BBox grown;
    grown.append_spot({box.max.x + 0.5f, box.max.y + 0.5f, box.max.z + 0.5f, box.max.w});
    grown.append_spot({box.min.x - 0.5f, box.min.y - 0.5f, box.min.z - 0.5f, box.min.w});
    for (CollGrid* const list : lists) {
        for (HitMesh* const object : objects_along(list, &line)) {
            if (!object->inside_bounds(&grown))
                continue;
            core::at(entries_, num_).bounds = object->get_world_bounds();
            core::at(entries_, num_).object = object;
            ++num_;
            ++statics_;
        }
    }
    for (const memory::ListNode* node = first_movable(); node->data != memory::ListNode::head_marker() && node;
         node = node->next) {
        auto* object = static_cast<HitMesh*>(node->data);
        if (!object || (object->flag_bits_ & 6) || !object->inside_bounds(&grown))
            continue;
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
        if (core::at(entries_, i).object != object)
            continue;
        for (int k = i + 1; k < num_; ++k)
            core::at(entries_, k - 1) = core::at(entries_, k);
        --statics_;
        --num_;
        return;
    }
}

// 0x0040A990
void HitMeshCache::remove_movable(HitMesh* object)
{
    for (int i = statics_; i < num_; ++i) {
        if (core::at(entries_, i).object != object)
            continue;
        --num_;
        core::at(entries_, i) = core::at(entries_, num_);
        --movables_;
        return;
    }
}

// 0x0040AA00
void register_coll_cache(HitMeshCache* cache)
{
    for (int i = 0; i < 8; ++i) {
        if (!(caches_in_use() & (1u << i))) {
            core::at(caches(), i) = cache;
            caches_in_use() |= 1u << i;
            return;
        }
    }
}

// 0x0040AA40
void unregister_coll_cache(HitMeshCache* cache)
{
    for (int i = 0; i < 8; ++i) {
        if ((caches_in_use() & (1u << i)) && core::at(caches(), i) == cache) {
            caches_in_use() &= ~(1u << i);
            return;
        }
    }
}

// 0x0040AA80
void remove_movable_from_caches(HitMesh* object)
{
    for (int i = 0; i < 8; ++i) {
        if (caches_in_use() & (1u << i))
            core::at(caches(), i)->remove_movable(object);
    }
}

// 0x0040AAC0
void remove_from_caches(HitMesh* object)
{
    for (int i = 0; i < 8; ++i) {
        if (caches_in_use() & (1u << i)) {
            core::at(caches(), i)->remove_static(object);
            core::at(caches(), i)->remove_movable(object);
        }
    }
}

// 0x0040ADD0
int shape_type_from_checksum(uint32_t shape)
{
    switch (shape) {
    case 0xDD42AA66:
    case 0x2BA71070:
    case 0xE460ABDE: // box
        return 1;
    case 0x6AADF154: // sector
        return 4;
    default:
        return 0;
    }
}

} // namespace render
