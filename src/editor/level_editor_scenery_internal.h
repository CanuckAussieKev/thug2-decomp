#pragma once

// NOT ORIGINAL - what level_editor_scenery.cpp's pieces share (the scene walks, the tracks and rails, the placement
// maths and the helpers more than one piece uses). They were the one file's anonymous namespaces; here they are in
// `internal`, with a using-directive, so every piece sees them as it saw them there.

#include "editor/level_editor_scenery.h"
#include <cstdint>
#include <memory>
#include <span>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "engine/memory/arena_alloc.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shared_coll.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "engine/objects/rail_manager.h"
#include "engine/render/coll_obj.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "engine/gfx/engine_parts.h"

namespace frontend::level_editor {

namespace internal {

constexpr int kMaxNodeGuard = 256; // a scene list longer than this is taken as broken

constexpr int kMaxSectorTableBits = 20; // the largest sector table walked

constexpr int kSectorFlagMovable = 0x40; // a sector flag; a sector that has it gets kSectorFlagMoved after a move (as WorldCell::place_at_absolute_point does)

constexpr int kSectorFlagMoved = 0x20;

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor

namespace frontend::level_editor::scenery {

namespace internal {

constexpr float kMaxSectorExtent = 4000.0F; // (larger: the ground, a street, a building block - not moved)

// The scenes (0x006FA7EC's list: nodes (memory::ListNode) {scene +0, priority, next +8}, ended by a node whose scene is -1), skies left
// out (+0x8D: drawn in the sky pass).
// A node of the scene list (memory::ListNode): its scene, and whether it is the list's head.
using SceneNode = memory::ListNode;

render::WorldScene* scene_of(const SceneNode* node);

bool is_end(const SceneNode* node);

template <typename F>
void for_each_scene(F fn)
{
    auto* node = (render::engine_parts().scene_list().next());
    for (int guard = 0; node && !is_end(node) && guard < kMaxNodeGuard; ++guard) {
        SceneNode* next = node->next();
        if (scene_of(node) && !scene_of(node)->is_sky()) {
            fn(scene_of(node));
        }
        node = next;
    }
}

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

gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector);

uint32_t& flags_of(render::WorldCell* sector);

bool plain_geom(render::Geom* geom);

bool geom_box(render::Geom* geom, V3* min, V3* max);

const char* meshes_movable(render::Geom* geom);

// What we've done to a sector: the box, turns and position when first touched, the placement since, and the engine's
// state as we set it (checked again before each use: a level reloaded at the same addresses starts afresh).
struct Track {
    uint32_t checksum;
    render::WorldScene* scene;
    render::WorldCell* sector;
    render::Geom* geom;
    V3 min0, max0;
    int quarters0;
    V3 position0;
    Placement placement;
    int quarters;
    V3 position;
    V3 collision_moved_; // (how far its own collision has been moved since: position - position0 on sixteenths)
    bool moved;
    std::shared_ptr<scale_tool::Original>
        original; // (once scaled: its vertices and collision then - level_editor_scale.h)
};

std::map<uint32_t, Track>& tracks_state();

// NOT ORIGINAL grouping - level_editor_scenery.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_scenery_state()).
struct LevelEditorSceneryState {
    std::vector<uint32_t> selection_;
    V3 pivot_{};
    float half_extent_y_{}; // (the selection's bottom is this far below the pivot - quarter turns keep it)
    std::vector<render::WorldScene*> dirty_scenes_;
};

LevelEditorSceneryState& level_editor_scenery_state();

std::unordered_map<uint32_t, uint32_t>& shadow_of();

bool same_v3(V3 a, V3 b);

Track* track_of(uint32_t checksum);

bool current_box(uint32_t checksum, render::Geom* geom, OrientedBox* box);

void mark_dirty(render::WorldScene* scene);

void apply_track(Track* track);

const char* get_name_of(uint32_t checksum);

std::string caption(uint32_t checksum);

std::unordered_map<uint32_t, uint32_t>& clusters_state();

uint32_t cluster_of(uint32_t checksum);

// --- grind rails ---
//
// The level's rails: the rail manager of modules::Skate (0x007CE478, +0x24) - filled from the node array's rail_nodes and
// ClimbingNodes by 0x0057E960 (from cfunc_parse_node_array 0x005A34E0): +0x40 its nodes, +0x48 their count, +0x50 set
// when its rails ride an object (positions in the object's space, 0x0057CB60 - never the level's).
// A node (objects::GrindPoint, 0x0057D090): the box of its segment (to the next node; its own point when it has none -
// 0x0057C260), its position, the next and previous nodes, flags (4 active, 0x10 a climbing node), its node's name.
// Skaters find rails by testing every node's box (0x0057E2D0): moving one is its position and the boxes of its segment
// and the one before.
using RailNode = objects::GrindPoint;

// What we've done to a rail node: where it was when first touched, the placement since, where we put it.
struct RailTrack {
    RailNode* node;
    bool active0; // (flag 4 when first touched: some rails start off)
    V3 pos0;
    Placement placement;
    V3 pos;
};

std::map<uint32_t, RailTrack>& rail_tracks();

// NOT ORIGINAL grouping - the scenery's rails and the ones dropped (were globals). Made on first use, never destroyed
// (scenery_rails()).
struct SceneryRails {
    std::vector<uint32_t> rails_; // (the selection's rail nodes, by name)
    std::vector<uint32_t> dropped_rails_; // (Ctrl+click / the Inspector's x: not carried until another group is selected)
};

SceneryRails& scenery_rails();

RailNode* find_rail(uint32_t name);

RailTrack* rail_track_of(uint32_t name);

void apply_rails(const std::vector<uint32_t>& names);

void compute_pivot();

void select_rails();

Placement placement_of(uint32_t checksum, render::Geom* geom);

bool geom_see_through(render::Geom* geom);

float geom_level_share(uint32_t checksum, render::Geom* geom);

bool same_placement(const Placement& a, const Placement& b);

std::vector<uint32_t> group_of(uint32_t checksum);

// NOT ORIGINAL grouping - the group hovered and the groups by piece (were globals). Made on first use, never destroyed
// (scenery_hover()).
struct SceneryHover {
    // The hovered sector's group (what a click would select), kept while the same sector is hovered.
    uint32_t hover_group_of_{};
    std::vector<uint32_t> hover_group_;
};

SceneryHover& scenery_hover();

std::unordered_map<uint32_t, std::pair<render::Geom*, float>>& level_shares();

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor::scenery
