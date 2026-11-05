#pragma once

// NOT ORIGINAL - what level_editor_scenery.cpp's pieces share (the scene walks, the tracks and rails, the placement
// maths and the helpers more than one piece uses). They were the one file's anonymous namespaces; here they are in
// `internal`, with a using-directive, so every piece sees them as it saw them there.

#include "editor/level_editor_scenery.h"
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shared_coll.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>

namespace frontend::level_editor {

namespace internal {

constexpr int kMaxSectorTableBits = 20; // the largest sector table walked

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor

namespace frontend::level_editor::scenery {

namespace internal {

constexpr float kMaxSectorExtent = 4000.0F; // (larger: the ground, a street, a building block - not moved)

// A scene's sector table (its sectors_ key map: 1 << bits buckets, each an item with more chained from it).
template <typename F>
void for_each_sector(render::WorldScene* scene, F fn)
{
    const lists::KeyMap* table = scene->sectors();
    if (!table || table->bits() > kMaxSectorTableBits) {
        return;
    }
    const lists::KeyMap::Item* buckets = table->buckets();
    if (!buckets) {
        return;
    }
    const uint32_t count = 1U << table->bits();
    const std::span<const lists::KeyMap::Item> bucket_run(buckets, count);
    for (uint32_t i = 0; i < count; ++i) {
        for (const lists::KeyMap::Item* entry = &core::at(bucket_run, i); entry; entry = entry->next) {
            if (entry->value) {
                fn(entry->key, static_cast<render::WorldCell*>(entry->value));
            }
        }
    }
}

render::WorldCell* lookup_sector(render::WorldScene* scene, uint32_t checksum);

render::WorldCell* find_sector(uint32_t checksum, render::WorldScene** scene_out);

render::Geom* geom_of(render::WorldCell* sector);

bool plain_geom(render::Geom* geom);

bool geom_box(render::Geom* geom, V3* min, V3* max);

std::unordered_map<uint32_t, uint32_t>& shadow_of();

const char* get_name_of(uint32_t checksum);

std::string caption(uint32_t checksum);

uint32_t cluster_of(uint32_t checksum);

Placement placement_of(uint32_t checksum, render::Geom* geom);

bool geom_see_through(render::Geom* geom);

float geom_level_share(uint32_t checksum, render::Geom* geom);

bool same_placement(const Placement& a, const Placement& b);

std::vector<uint32_t> group_of(uint32_t checksum);

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor::scenery
