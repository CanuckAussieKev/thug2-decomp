// NOT ORIGINAL - the level editor's scenery (ours, added at the user's request). See level_editor_scenery.h.

#include "editor/level_editor_scenery.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "editor/level_editor.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_ground.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shadows.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "gpu/vertex_buffers.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_scene_list.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "engine/objects/rail_manager.h"
#include "engine/render/coll_obj.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"

namespace frontend::level_editor {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

V3 get_quarter_turn(V3 v, int quarters)
{
    switch (quarters & 3) {
    case 1:
        return {.x = v.z, .y = v.y, .z = -v.x};
    case 2:
        return {.x = -v.x, .y = v.y, .z = -v.z};
    case 3:
        return {.x = -v.z, .y = v.y, .z = v.x};
    default:
        return v;
    }
}

V3 get_apply(const Placement& placement, V3 x)
{
    return get_quarter_turn(per_axis(x, placement.scale), placement.quarters) + placement.offset;
}

void move_by(Placement* placement, V3 delta) { placement->offset = placement->offset + delta; }

Placement compose(const Placement& outer, const Placement& inner)
{
    // outer(inner(x)) = Q_o S_o (Q_i S_i x + o_i) + o_o = Q_o Q_i (S_o' S_i) x + (Q_o S_o o_i + o_o), S_o' = S_o as
    // seen through Q_i (S_o Q_i = Q_i S_o': odd turns swap its x and z).
    return {.quarters = (inner.quarters + outer.quarters) & 3,
            .offset = get_quarter_turn(per_axis(inner.offset, outer.scale), outer.quarters) + outer.offset,
            .scale = per_axis(turned_scale(outer.scale, inner.quarters), inner.scale)};
}

void turn_about(Placement* placement, V3 pivot, int quarters)
{
    // x'' = R (x' - pivot) + pivot, x' = Q x + offset: x'' = R Q x + R (offset - pivot) + pivot.
    placement->quarters = (placement->quarters + quarters) & 3;
    placement->offset = get_quarter_turn(placement->offset - pivot, quarters) + pivot;
}

void engine_state(int quarters0, V3 position0, const Placement& placement, int* quarters, V3* position)
{
    // Loaded vertex v was at Q0 v + p0; now at Q s (Q0 v + p0) + offset = s Q Q0 v + (s Q p0 + offset).
    *quarters = (quarters0 + placement.quarters) & 3;
    *position = get_apply(placement, position0);
}

OrientedBox placed_box(V3 min, V3 max, const Placement& placement)
{
    OrientedBox box{};
    box.origin = placement.offset;
    core::at(box.axes, 0) = get_quarter_turn({.x = placement.scale.x, .y = 0.0f, .z = 0.0f}, placement.quarters);
    core::at(box.axes, 1) = {.x = 0.0f, .y = placement.scale.y, .z = 0.0f};
    core::at(box.axes, 2) = get_quarter_turn({.x = 0.0f, .y = 0.0f, .z = placement.scale.z}, placement.quarters);
    box.min = min;
    box.max = max;
    return box;
}

bool sector_size_movable(V3 min, V3 max, float limit)
{
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN)
        return false;
    return max.x - min.x <= limit && max.y - min.y <= limit && max.z - min.z <= limit;
}

void grow_box(V3* min, V3* max, bool have, V3 bounds_low, V3 bounds_high)
{
    if (!have) {
        *min = bounds_low;
        *max = bounds_high;
        return;
    }
    *min = {.x = std::min(min->x, bounds_low.x), .y = std::min(min->y, bounds_low.y), .z = std::min(min->z, bounds_low.z)};
    *max = {.x = std::max(max->x, bounds_high.x), .y = std::max(max->y, bounds_high.y), .z = std::max(max->z, bounds_high.z)};
}

void bounds_of(const OrientedBox& box, V3* min, V3* max)
{
    std::array<V3, 8> corners{};
    box_corners(box.origin, box.axes, box.min, box.max, corners);
    for (int i = 0; i < 8; ++i)
        grow_box(min, max, i != 0, core::at(corners, i), core::at(corners, i));
}

bool piece_fits(V3 min, V3 max, V3 group_min, V3 group_max)
{
    const V3 size = max - min;
    const V3 group = group_max - group_min;
    const double volume = static_cast<double>(size.x) * size.y * size.z;
    const double group_volume = static_cast<double>(group.x) * group.y * group.z;
    const float across = std::max(group.x, group.z) * 1.25f;
    return volume <= group_volume && size.x <= across && size.z <= across;
}

bool piece_touches(V3 min, V3 max, V3 bounds_low, V3 bounds_high, float margin)
{
    return min.x <= bounds_high.x + margin && max.x >= bounds_low.x - margin && min.z <= bounds_high.z + margin &&
           max.z >= bounds_low.z - margin && min.y <= bounds_high.y + margin && max.y > bounds_low.y + 5.0f;
}

void segment_box(V3 a, V3 b, V3* min, V3* max)
{
    *min = {.x = std::min(a.x, b.x), .y = std::min(a.y, b.y), .z = std::min(a.z, b.z)};
    *max = {.x = std::max(a.x, b.x), .y = std::max(a.y, b.y), .z = std::max(a.z, b.z)};
}

uint32_t get_pulse(uint32_t low, uint32_t high, double seconds)
{
    const double phase = std::fmod(seconds, 1.25) / 1.25;
    const double mix = 0.5 - 0.5 * std::cos(phase * 2.0 * 3.14159265358979);
    uint32_t colour = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const double a = (low >> shift) & 0xFF;
        const double b = (high >> shift) & 0xFF;
        // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
        colour |= static_cast<uint32_t>(a + (b - a) * mix + 0.5) << shift;
    }
    return colour;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace scenery {
namespace {

constexpr float kMaxSectorExtent = 4000.0f; // (larger: the ground, a street, a building block - not moved)
constexpr uint32_t kCluster = 0x1A3A966Bu;  // "Cluster" (the node's TrickObject Cluster=)

// The scenes (0x006FA7EC's list: nodes (memory::ListNode) {scene +0, priority, next +8}, ended by a node whose scene is -1), skies left
// out (+0x8D: drawn in the sky pass).
// A node of the scene list (memory::ListNode): its scene, and whether it is the list's head.
using SceneNode = memory::ListNode;
render::WorldScene* scene_of(const SceneNode* node) { return static_cast<render::WorldScene*>(node->data); }
bool is_end(const SceneNode* node) { return node->is_head(); }

template <typename F>
void for_each_scene(F fn)
{
    auto* node = (render::engine_parts().scene_list().next);
    for (int guard = 0; node && !is_end(node) && guard < 256; ++guard) {
        SceneNode* next = node->next;
        if (scene_of(node) && !scene_of(node)->is_sky_)
            fn(scene_of(node));
        node = next;
    }
}

// A scene's sector table (its sectors_ key map: 1 << bits buckets, each an item with more chained from it).
template <typename F>
void for_each_sector(render::WorldScene* scene, F fn)
{
    const lists::KeyMap* table = scene->sectors_;
    if (!table || table->bits_ > 20)
        return;
    const lists::KeyMap::Item* buckets = table->buckets_;
    if (!buckets)
        return;
    const uint32_t count = 1u << table->bits_;
    const std::span<const lists::KeyMap::Item> bucket_run(buckets, count);
    for (uint32_t i = 0; i < count; ++i) {
        for (const lists::KeyMap::Item* entry = &core::at(bucket_run, i); entry; entry = entry->next) {
            if (entry->value)
                fn(entry->key, static_cast<render::WorldCell*>(entry->value));
        }
    }
}

render::WorldCell* lookup_sector(render::WorldScene* scene, uint32_t checksum)
{
    return static_cast<render::WorldCell*>(scene->look_up_sector(checksum));
}

// The sector of that checksum in the first (non-sky) scene having one.
render::WorldCell* find_sector(uint32_t checksum, render::WorldScene** scene_out)
{
    render::WorldCell* found = nullptr;
    for_each_scene([&](render::WorldScene* scene) {
        if (found)
            return;
        if (render::WorldCell* sector = lookup_sector(scene, checksum)) {
            found = sector;
            if (scene_out)
                *scene_out = scene;
        }
    });
    return found;
}

// (On PC every sector's geometry is a PcGeom - as PcSector reads it.)
render::PcGeom* geom_of(render::WorldCell* sector) { return static_cast<render::PcGeom*>(sector->geom_handle_); }
gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector) { return sector->hit_body_; }
uint32_t& flags_of(render::WorldCell* sector) { return sector->flag_bits_; }
int quarters_of(render::WorldCell* sector) { return sector->rot_y_; }

// The PC geometry of level scenery (not an instance), active.
bool plain_geom(render::PcGeom* geom) { return render::is_pc_geom(geom) && !geom->instance; }

// Its box as loaded.
bool geom_box(render::PcGeom* geom, V3* min, V3* max)
{
    const maths::BBox& box = geom->bounds;
    *min = {.x = box.min.x, .y = box.min.y, .z = box.min.z};
    *max = {.x = box.max.x, .y = box.max.y, .z = box.max.z};
    return min->x <= max->x && min->y <= max->y && min->z <= max->z;
}

// The geometry's world position (0x004B46A0: the first mesh's; 0 when never moved).
V3 geom_position(render::PcGeom* geom)
{
    const maths::Vec4* v = geom->get_absolute_point();
    return {.x = v->x, .y = v->y, .z = v->z};
}

// The meshes' vertices can be moved: each vertex buffer has its own CPU copy (data) holding exactly the mesh's
// vertices (vertex_count_ of stride bytes) - what MeshRef::change_place walks and uploads.
const char* meshes_movable(render::PcGeom* geom)
{
    render::MeshRef* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    if (!meshes || count == 0)
        return "no meshes";
    if (count > 4096)
        return "odd mesh count";
    for (uint32_t i = 0; i < count; ++i) {
        const render::MeshRef* mesh = core::at(std::span<render::MeshRef* const>(meshes, count), i);
        if (!mesh)
            return "no mesh";
        const uint32_t buffers = mesh->vertex_buffer_count_;
        const uint32_t vertices = mesh->vertex_count_;
        const uint32_t stride = mesh->stride;
        if (buffers > std::size(mesh->vertex_buffers_))
            return "odd vertex buffers";
        for (uint32_t b = 0; b < buffers; ++b) {
            const render::VertexBuffer* buffer = core::at(mesh->vertex_buffers_, b);
            if (!buffer || !buffer->data)
                return "no vertex copy";
            if (buffer->size != vertices * stride)
                return "shared vertex buffer";
        }
    }
    return nullptr;
}

// What we've done to a sector: the box, turns and position when first touched, the placement since, and the engine's
// state as we set it (checked again before each use: a level reloaded at the same addresses starts afresh).
struct Track {
    uint32_t checksum;
    render::WorldScene* scene;
    render::WorldCell* sector;
    render::PcGeom* geom;
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
std::map<uint32_t, Track>& tracks_state()
{
    static std::map<uint32_t, Track> value;
    return value;
}
// NOT ORIGINAL grouping - level_editor_scenery.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_scenery_state()).
struct LevelEditorSceneryState {
    std::vector<uint32_t> selection_{};
    V3 pivot_{};
    float half_extent_y_{}; // (the selection's bottom is this far below the pivot - quarter turns keep it)
    std::vector<render::WorldScene*> dirty_scenes_{};
};

LevelEditorSceneryState& level_editor_scenery_state()
{
    static core::NoDestroy<LevelEditorSceneryState> state;
    return state.get();
}

// The shadows groups have taken (level_editor_shadows.h): each with the member whose name it carries (0: by place).
std::unordered_map<uint32_t, uint32_t>& shadow_of()
{
    static std::unordered_map<uint32_t, uint32_t> value;
    return value;
}

bool same_v3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The track of a sector (made when first touched); null when the sector can't be found or has no plain geometry.
Track* track_of(uint32_t checksum)
{
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(checksum, &scene);
    if (!sector)
        return nullptr;
    render::PcGeom* geom = geom_of(sector);
    if (!plain_geom(geom))
        return nullptr;
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end()) {
        Track& track = it->second;
        if (track.sector == sector && track.geom == geom && track.scene == scene &&
            track.quarters == quarters_of(sector) && same_v3(track.position, geom_position(geom)))
            return &track;
        if (track.sector == sector && track.geom == geom && track.scene == scene)
            shared_collision::rebase(checksum); // (moved by something else: its shared collision's base is now too)
        tracks_state().erase(it); // (another level, or moved by something else: start again from where it is)
    }
    Track track = {};
    track.checksum = checksum;
    track.scene = scene;
    track.sector = sector;
    track.geom = geom;
    if (!geom_box(geom, &track.min0, &track.max0))
        return nullptr;
    track.quarters0 = track.quarters = quarters_of(sector);
    track.position0 = track.position = geom_position(geom);
    track.placement = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
    return &tracks_state().emplace(checksum, track).first->second;
}

// A sector's box now, without making a track for it.
bool current_box(uint32_t checksum, render::PcGeom* geom, OrientedBox* box)
{
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end() && it->second.geom == geom) {
        *box = placed_box(it->second.min0, it->second.max0, it->second.placement);
        return true;
    }
    V3 min{}, max{};
    if (!geom_box(geom, &min, &max))
        return false;
    *box = placed_box(min, max, {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}});
    return true;
}

void mark_dirty(render::WorldScene* scene)
{
    if (std::ranges::find(level_editor_scenery_state().dirty_scenes_, scene) == level_editor_scenery_state().dirty_scenes_.end())
        level_editor_scenery_state().dirty_scenes_.push_back(scene);
}

// The engine brought to the track's placement: the turn (WorldCell::spin_around_up 0x0049E760, about the geometry's
// world position as it is), then the position (the geometry's place_at_absolute_point, vfunc +0x48 through 0x0048DEC0; the
// collision's, vfunc +0x14 - handed its own origin (vfunc +0x1C) plus the change: it translates by the difference),
// the sector flagged for the super sectors' update as WorldCell::place_at_absolute_point (0x0049E6C0) does.
// A scale (level_editor_scale.h): the first time, the sector's vertices and collision are kept as they are; from then
// on, after the engine's turn and move, they're written again from that copy, scaled.
void apply_track(Track* track)
{
    // (first, while the piece is still where the track started: the level's shared collision that is its -
    // level_editor_shared_coll.h)
    shared_collision::ensure(track->checksum, track->scene, track->sector, track->geom);
    if (!track->original && !is_unit_scale(track->placement.scale)) {
        track->original = scale_tool::get_capture(track->sector, track->geom, track->quarters, track->position);
        if (!track->original) {
            get_log("level editor: sector %p can't be scaled (its meshes can't be copied)",
                    static_cast<void*>(track->sector));
            track->placement.scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f};
        }
    }
    int quarters = 0;
    V3 position{};
    engine_state(track->quarters0, track->position0, track->placement, &quarters, &position);
    if (quarters != track->quarters) {
        track->sector->spin_around_up(quarters);
        track->quarters = quarters;
    }
    if (!same_v3(position, track->position)) {
        const maths::Vec4 to = {.x = position.x, .y = position.y, .z = position.z, .w = 1.0f};
        track->geom->place_at_absolute_point(&to);
        // The collision by whole sixteenths of a unit from where the track started: its BSP tree's splits are
        // sixteenths and each Translate (0x00415D80) rounds them towards zero, so fractional steps (every frame of a
        // drag) would let the tree drift away from its faces; on sixteenths every step is exact and moving back gives
        // the level's tree again (as level_editor_walls.h keeps walls). The collision is then within 1/32 unit of the
        // meshes.
        const V3 collision_moved = walls::on_grid(position - track->position0);
        gsl::owner<render::HitMesh*> coll = coll_of(track->sector);
        if (coll && !same_v3(collision_moved, track->collision_moved_)) {
            render::HitMesh* object = coll;
            const maths::Vec4* origin = object->get_absolute_point();
            const V3 change = collision_moved - track->collision_moved_;
            const maths::Vec4 moved = {.x = origin->x + change.x, .y = origin->y + change.y, .z = origin->z + change.z, .w = origin->w};
            object->place_at_absolute_point(&moved);
        }
        track->collision_moved_ = collision_moved;
        track->position = position;
    }
    if (track->original && !scale_tool::reassemble(*track->original, track->sector, track->geom, track->quarters,
                                                   track->position, track->placement.scale, track->quarters0))
        track->original.reset(); // (not the sector copied any more: left as the engine has it)
    shared_collision::get_place(track->checksum, track->placement);
    if (flags_of(track->sector) & 0x40)
        flags_of(track->sector) |= 0x20;
    track->moved = true;
    mark_dirty(track->scene);
}

const char* get_name_of(uint32_t checksum)
{
    if (const char* name = script::kept_qb_name(checksum))
        return name;
    return script::look_up_hash_name(checksum);
}

std::string caption(uint32_t checksum)
{
    std::array<char, 96> text{};
    const char* name = get_name_of(checksum);
    if (name)
        text::format_to(text, "%s", name);
    else
        text::format_to(text, "%08X", checksum);
    return text.data();
}

// A node's TrickObject Cluster= (0: none), by its name (0x0045C1B0) - kept, as looked up (the level doesn't change
// while the editor is on; Reset forgets them).
std::unordered_map<uint32_t, uint32_t>& clusters_state()
{
    static std::unordered_map<uint32_t, uint32_t> value;
    return value;
}
uint32_t cluster_of(uint32_t checksum)
{
    // (a cluster the editor set - level_editor_clusters.h: its group follows it)
    uint32_t edited = 0;
    if (clusters::get_override(checksum, &edited))
        return edited;
    auto it = clusters_state().find(checksum);
    if (it != clusters_state().end())
        return it->second;
    auto* node = level_nodes::fetch_vertex(checksum, false);
    uint32_t cluster = 0;
    if (node)
        node->read_hash_value(kCluster, &cluster, 0);
    clusters_state().emplace(checksum, cluster);
    return cluster;
}

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

// The level's rail nodes (empty when there are none, or the manager is on an object).
std::span<RailNode> rail_nodes()
{
    const modules::Skate* skate = modules::Skate::sole();
    const objects::GrindRailHub* manager = skate ? skate->rail_manager().get() : nullptr;
    if (!manager || manager->on_object_)
        return {};
    if (!manager->nodes_ || manager->num_ <= 0 || manager->num_ >= 100000)
        return {};
    return {manager->nodes_, static_cast<size_t>(manager->num_)};
}

V3 rail_pos(const RailNode* node) { return {.x = node->at_point_.x, .y = node->at_point_.y, .z = node->at_point_.z}; }

// What we've done to a rail node: where it was when first touched, the placement since, where we put it.
struct RailTrack {
    RailNode* node;
    bool active0; // (flag 4 when first touched: some rails start off)
    V3 pos0;
    Placement placement;
    V3 pos;
};
std::map<uint32_t, RailTrack>& rail_tracks()
{
    static std::map<uint32_t, RailTrack> value;
    return value;
}
// NOT ORIGINAL grouping - the scenery's rails and the ones dropped (were globals). Made on first use, never destroyed
// (scenery_rails()).
struct SceneryRails {
    std::vector<uint32_t> rails_{}; // (the selection's rail nodes, by name)
    std::vector<uint32_t> dropped_rails_{}; // (Ctrl+click / the Inspector's x: not carried until another group is selected)
};

SceneryRails& scenery_rails()
{
    static core::NoDestroy<SceneryRails> state;
    return state.get();
}

RailNode* find_rail(uint32_t name)
{
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (nodes.empty())
        return nullptr;
    // (a ladder's point too - its moves are tracked as a rail's: level_editor_chains.h)
    for (int i = 0; i < count; ++i) {
        if (core::at(nodes, i).label_ == name)
            return &core::at(nodes, i);
    }
    return nullptr;
}

RailTrack* rail_track_of(uint32_t name)
{
    RailNode* node = find_rail(name);
    if (!node)
        return nullptr;
    auto it = rail_tracks().find(name);
    if (it != rail_tracks().end()) {
        if (it->second.node == node && same_v3(it->second.pos, rail_pos(node)))
            return &it->second;
        rail_tracks().erase(it);
    }
    const RailTrack track = {
        .node = node, .active0 = (node->flag_bits_ & 4) != 0, .pos0 = rail_pos(node), .placement = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}}, .pos = rail_pos(node)};
    return &rail_tracks().emplace(name, track).first->second;
}

uint32_t& stored_rail_generation()
{
    static uint32_t rail_generation{1};
    return rail_generation;
}

// The rail nodes `names` placed where their tracks say, then the boxes of their segments and of those ending at
// them.
void apply_rails(const std::vector<uint32_t>& names)
{
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (nodes.empty())
        return;
    ++stored_rail_generation(); // (the node layer reads the places again)
    std::vector<RailNode*> moved;
    for (const uint32_t name : names) {
        auto it = rail_tracks().find(name);
        if (it == rail_tracks().end())
            continue;
        RailTrack& track = it->second;
        const V3 pos = get_apply(track.placement, track.pos0);
        track.node->at_point_.x = pos.x;
        track.node->at_point_.y = pos.y;
        track.node->at_point_.z = pos.z;
        track.pos = pos;
        moved.push_back(track.node);
    }
    auto is_moved = [&](const RailNode* node) { return node && std::ranges::find(moved, node) != moved.end(); };
    for (int i = 0; i < count; ++i) {
        RailNode* node = &core::at(nodes, i);
        if (!is_moved(node) && !is_moved(node->succ_))
            continue;
        V3 min{}, max{};
        segment_box(rail_pos(node), node->succ_ ? rail_pos(node->succ_) : rail_pos(node), &min, &max);
        node->lower_limit_ = {.x = min.x, .y = min.y, .z = min.z, .w = 1.0f};
        node->upper_limit_ = {.x = max.x, .y = max.y, .z = max.z, .w = 1.0f};
    }
}

void compute_pivot()
{
    V3 min = {}, max = {};
    bool have = false;
    // (its shadows left out while it has anything else: a shadow lies wider than its caster, on the floor below)
    const bool casters =
        std::ranges::any_of(level_editor_scenery_state().selection_, [](uint32_t checksum) { return !shadow_of().count(checksum); });
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (casters && shadow_of().count(checksum))
            continue;
        OrientedBox box{};
        if (!cuboid(checksum, &box))
            continue;
        V3 a{}, b{};
        bounds_of(box, &a, &b);
        grow_box(&min, &max, have, a, b);
        have = true;
    }
    level_editor_scenery_state().pivot_ = have ? (min + max) * 0.5f : V3{.x = 0.0f, .y = 0.0f, .z = 0.0f};
    level_editor_scenery_state().half_extent_y_ = have ? (max.y - min.y) * 0.5f : 0.0f;
}

// The selection's rails: rail nodes of the selected sectors' clusters (the fire truck's TRG_FireTruck01..25 are in
// Fire_Tender), and ones lying within a selected sector's box (both ends, within 10 units).
void select_rails()
{
    scenery_rails().rails_.clear();
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (level_editor_scenery_state().selection_.empty() || nodes.empty())
        return;
    std::vector<uint32_t> clusters;
    std::vector<std::pair<V3, V3>> boxes;
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (is_clone(checksum))
            continue; // (a copy has no rails of its own: the source's stay with the source)
        clusters.push_back(checksum);
        if (const uint32_t cluster = cluster_of(checksum))
            clusters.push_back(cluster);
        OrientedBox box{};
        V3 min{}, max{};
        if (!shadow_of().count(checksum) && cuboid(checksum, &box)) { // (not by a shadow's box: rails lying over it)
            bounds_of(box, &min, &max);
            boxes.emplace_back(min - V3{.x = 10.0f, .y = 10.0f, .z = 10.0f}, max + V3{.x = 10.0f, .y = 10.0f, .z = 10.0f});
        }
    }
    auto within = [&](V3 p) {
        for (const auto& box : boxes) {
            if (p.x >= box.first.x && p.x <= box.second.x && p.y >= box.first.y && p.y <= box.second.y &&
                p.z >= box.first.z && p.z <= box.second.z)
                return true;
        }
        return false;
    };
    auto listed = [&](uint32_t c) { return c && std::ranges::find(clusters, c) != clusters.end(); };
    for (int i = 0; i < count; ++i) {
        const RailNode& node = core::at(nodes, i);
        if (node.flag_bits_ & 0x10)
            continue;
        bool in = listed(node.label_) || listed(cluster_of(node.label_)) ||
                  (within(rail_pos(&node)) && (!node.succ_ || within(rail_pos(node.succ_))));
        if (in && std::ranges::find(scenery_rails().dropped_rails_, node.label_) != scenery_rails().dropped_rails_.end())
            in = false;
        if (in && std::ranges::find(scenery_rails().rails_, node.label_) == scenery_rails().rails_.end())
            scenery_rails().rails_.push_back(node.label_);
    }
}

// Where the editor has put a sector (none yet: where the level has it).
Placement placement_of(uint32_t checksum, render::PcGeom* geom)
{
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end() && it->second.geom == geom)
        return it->second.placement;
    return {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
}

bool geom_see_through(render::PcGeom* geom); // (below)
float geom_level_share(uint32_t checksum, render::PcGeom* geom);

bool same_placement(const Placement& a, const Placement& b)
{
    return a.quarters == b.quarters && std::fabs(a.scale.x - b.scale.x) < 1e-4f &&
           std::fabs(a.scale.y - b.scale.y) < 1e-4f && std::fabs(a.scale.z - b.scale.z) < 1e-4f &&
           std::fabs(a.offset.x - b.offset.x) < 0.5f && std::fabs(a.offset.y - b.offset.y) < 0.5f &&
           std::fabs(a.offset.z - b.offset.z) < 0.5f;
}

// The group a click on a sector selects: the sector, the rest of its cluster, and the movable pieces built onto them
// (piece_fits / piece_touches, three rounds - each with its cluster). Decided on the level's own layout (the boxes as
// loaded) and only among pieces the editor has moved together (the same placement): a group moved into something
// doesn't take it in, and a piece moved away from its group no longer brings the group along. Hidden pieces aren't
// taken, nor structure (Picking::is_structure by size - a floor, a wall) unless the clicked piece is structure itself:
// a level's own clusters can join a small piece to the floor it lies on (TR's SkWare_Main_12, 360 x 60 x 924, and the
// 3000 x 99 x 2431 floor SkWare_Main_9), and a click on the small piece mustn't take the floor. Then the group's shadows
// (level_editor_shadows.h).
std::vector<uint32_t> group_of(uint32_t checksum)
{
    std::vector<uint32_t> group;
    render::WorldScene* scene = nullptr;
    render::WorldCell* clicked = find_sector(checksum, &scene);
    if (why_not_movable(checksum) || !clicked)
        return group;
    Placement placement = placement_of(checksum, geom_of(clicked));
    struct Piece {
        uint32_t checksum;
        V3 min, max;
        bool in;
        bool see_through_;
        float level_share_; // (Picking::get_level_share: structure's low-and-flat rule)
    };
    std::vector<Piece> pieces; // (the scene's shown sectors small enough to move, moved as the clicked one)
    for_each_sector(scene, [&](uint32_t other, render::WorldCell* sector) {
        render::PcGeom* geom = geom_of(sector);
        V3 min{}, max{};
        if (!plain_geom(geom) || !geom->active || !geom_box(geom, &min, &max) ||
            !sector_size_movable(min, max, kMaxSectorExtent) || !same_placement(placement_of(other, geom), placement))
            return;
        pieces.push_back({.checksum = other, .min = min, .max = max, .in = false, .see_through_ = geom_see_through(geom), .level_share_ = geom_level_share(other, geom)});
    });
    // (by size only: a camera nowhere near)
    const V3 nowhere = {.x = 1.0e9f, .y = 1.0e9f, .z = 1.0e9f};
    bool clicked_structure = false;
    for (const Piece& piece : pieces) {
        if (piece.checksum == checksum)
            clicked_structure = picking::is_structure(piece.min, piece.max, nowhere, piece.level_share_);
    }
    auto take = [&](Piece& piece) {
        if (piece.in || why_not_movable(piece.checksum))
            return false;
        if (piece.checksum != checksum && !clicked_structure &&
            picking::is_structure(piece.min, piece.max, nowhere, piece.level_share_))
            return false;
        piece.in = true;
        group.push_back(piece.checksum);
        return true;
    };
    const uint32_t cluster = cluster_of(checksum);
    for (Piece& piece : pieces) {
        if (piece.checksum == checksum || (cluster && cluster_of(piece.checksum) == cluster))
            take(piece);
    }
    if (group.empty())
        return group;
    V3 group_min = {}, group_max = {};
    bool have = false;
    for (const Piece& piece : pieces) {
        if (piece.in) {
            grow_box(&group_min, &group_max, have, piece.min, piece.max);
            have = true;
        }
    }
    // (only a free-standing thing grows: not a floor, a wall or a whole building - what stands on or against those isn't
    // part of them)
    const V3 group_size = group_max - group_min;
    const bool grows = std::min({group_size.x, group_size.y, group_size.z}) >= 20.0f &&
                       std::max({group_size.x, group_size.y, group_size.z}) <= 1500.0f;
    for (int round = 0; grows && round < 3; ++round) {
        std::vector<uint32_t> joined;
        for (const Piece& piece : pieces) {
            if (piece.in || !piece_fits(piece.min, piece.max, group_min, group_max))
                continue;
            for (const Piece& member : pieces) {
                if (member.in && piece_touches(piece.min, piece.max, member.min, member.max, 2.0f)) {
                    joined.push_back(piece.checksum);
                    break;
                }
            }
        }
        if (joined.empty())
            break;
        for (const uint32_t joining : joined) {
            const uint32_t joining_cluster = cluster_of(joining);
            for (Piece& piece : pieces) {
                if (piece.checksum == joining || (joining_cluster && cluster_of(piece.checksum) == joining_cluster &&
                                                  piece_fits(piece.min, piece.max, group_min, group_max))) {
                    if (take(piece) && debug_enabled())
                        get_log("level editor:   joins: %s (%.0f x %.0f x %.0f)", caption(piece.checksum).c_str(),
                                piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z);
                }
            }
        }
    }
    // Its shadows (level_editor_shadows.h): by name, then - for shadow-named pieces nobody owns by name - by place,
    // under one of its pieces. Not by place for a floor or wall clicked by itself.
    auto source_of = [](uint32_t of) {
        for (const Replicate& clone : get_clones()) {
            if (clone.checksum == of)
                return clone.source;
        }
        return of;
    };
    for (Piece& piece : pieces) {
        if (piece.in || why_not_movable(piece.checksum))
            continue;
        const uint32_t source = source_of(piece.checksum);
        const char* name = get_name_of(source);
        if (!name || !shadows::shadow_name(name))
            continue;
        uint32_t owner = 0;
        for (const std::string& candidate : shadows::owner_names(name)) {
            const uint32_t crc = checksums::hash_from_text(candidate.c_str());
            if (crc != source && lookup_sector(scene, crc)) {
                owner = crc;
                break;
            }
        }
        uint32_t caster = 0;
        const char* how = nullptr;
        if (owner) {
            // (a copy's shadow: a copy of its owner, and by it - copies of one piece share names)
            const bool clone = source != piece.checksum;
            for (const Piece& member : pieces) {
                if (member.in && source_of(member.checksum) == owner && (member.checksum != owner) == clone &&
                    (!clone || shadows::get_beside(piece.min, piece.max, member.min, member.max))) {
                    caster = member.checksum;
                    how = "by name";
                    break;
                }
            }
        } else if (!clicked_structure && piece.see_through_) {
            // (under one of the group's pieces - not a shadow taken here - and nearer to it than to anything else
            // standing there)
            std::vector<std::pair<V3, V3>> others;
            for (const Piece& other : pieces) {
                if (!other.in && !other.see_through_ && shadows::stands_by(other.min, piece.max) &&
                    !picking::is_structure(other.min, other.max, nowhere, other.level_share_))
                    others.emplace_back(other.min, other.max);
            }
            for (const Piece& member : pieces) {
                if (member.in && !shadow_of().count(member.checksum) &&
                    shadows::falls_under(piece.min, piece.max, member.min, member.max) &&
                    shadows::nearest_to_group(piece.min, piece.max, member.min, member.max, others)) {
                    how = "by place";
                    caster = member.checksum;
                    break;
                }
            }
        }
        if (!how)
            continue;
        piece.in = true;
        group.push_back(piece.checksum);
        shadow_of()[piece.checksum] = caster;
        if (debug_enabled())
            get_log("level editor:   shadow %s: %s (%.0f x %.0f x %.0f)%s%s", how, caption(piece.checksum).c_str(),
                    piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z,
                    caster ? " of " : "", caster ? caption(caster).c_str() : "");
    }
    return group;
}

// NOT ORIGINAL grouping - the group hovered and the groups by piece (were globals). Made on first use, never destroyed
// (scenery_hover()).
struct SceneryHover {
    // The hovered sector's group (what a click would select), kept while the same sector is hovered.
    uint32_t hover_group_of_{};
    std::vector<uint32_t> hover_group_{};
};

SceneryHover& scenery_hover()
{
    static core::NoDestroy<SceneryHover> state;
    return state.get();
}

// The feeler (as ground_below in level_editor.cpp): constructor 0x00417760, cast 0x00417710 (start, end, movables).
// Its collision data from +0x24: the normal, the point (+0x34), the collision object hit (+0x54) and its checksum
// (+0x58: the object's +4, written by 0x004131F0).
bool cast_static(V3 from, V3 to, V3* point, uint32_t* checksum)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const auto& hit = ray->hit_.point;
    *point = {.x = hit[0], .y = hit[1], .z = hit[2]};
    *checksum = ray->hit_.object_checksum_;
    return ray->hit_.object != nullptr;
}

// The ray against a geometry's meshes as drawn (their vertex copies are where the sector is now: moving it moves
// them): each mesh's first index buffer as a triangle strip; a billboard (turned to the camera when drawn) by its
// sphere.
bool ray_meshes(render::PcGeom* geom, V3 origin, V3 dir, float max_t, float* t)
{
    auto* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    bool found = false;
    float best = max_t;
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        const render::MeshRef* mesh = core::at(std::span<render::MeshRef* const>(meshes, count), i);
        if (!mesh)
            continue;
        float hit = 0.0f;
        if (mesh->billboard || (mesh->flags & 0x10)) {
            if (ray_sphere(origin, dir, {.x = core::at(mesh->centre, 0), .y = core::at(mesh->centre, 1), .z = core::at(mesh->centre, 2)}, mesh->radius, &hit) &&
                hit < best) {
                best = hit;
                found = true;
            }
            continue;
        }
        const render::VertexBuffer* vertices = mesh->vertex_buffer_count_ ? core::at(mesh->vertex_buffers_, 0) : nullptr;
        const render::IdxStore* indices = core::at(mesh->index_buffers_, 0);
        if (!vertices || !vertices->data || !indices || !indices->indices || mesh->stride < 12)
            continue;
        if (picking::ray_strip(origin, dir, vertices->data, mesh->stride, mesh->vertex_count_, indices->indices,
                               core::at(mesh->index_counts_, 0), best, &hit)) {
            best = hit;
            found = true;
        }
    }
    if (found)
        *t = best;
    return found;
}

// Whether a geometry's meshes are all see-through (Picking::material_see_through: the material's +0x20 - the first
// pass's flags, 0x40 the scene's transparent sort - and +0x80, its blend mode; the scene loader 0x004D3390 fills both).
bool geom_see_through(render::PcGeom* geom)
{
    auto* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    if (!meshes || count == 0 || count > 4096)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        const render::MaterialRef* material = core::at(std::span<render::MeshRef* const>(meshes, count), i) ? core::at(std::span<render::MeshRef* const>(meshes, count), i)->material : nullptr;
        if (!material || !picking::material_see_through(core::at(material->pass_flags_, 0), core::at(material->blend_modes_, 0)))
            return false;
    }
    return true;
}

// The level part of a geometry's upward-facing surface (its meshes' first index buffers as strips, as ray_meshes reads
// them; billboards left out), measured once a piece while the editor runs (moves and quarter turns don't change it).
std::unordered_map<uint32_t, std::pair<render::PcGeom*, float>>& level_shares()
{
    static std::unordered_map<uint32_t, std::pair<render::PcGeom*, float>> value;
    return value;
}

float geom_level_share(uint32_t checksum, render::PcGeom* geom)
{
    auto it = level_shares().find(checksum);
    if (it != level_shares().end() && it->second.first == geom)
        return it->second.second;
    picking::TopArea area;
    auto* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        const render::MeshRef* mesh = core::at(std::span<render::MeshRef* const>(meshes, count), i);
        if (!mesh || mesh->billboard || (mesh->flags & 0x10))
            continue;
        const render::VertexBuffer* vertices = mesh->vertex_buffer_count_ ? core::at(mesh->vertex_buffers_, 0) : nullptr;
        const render::IdxStore* indices = core::at(mesh->index_buffers_, 0);
        if (!vertices || !vertices->data || !indices || !indices->indices || mesh->stride < 12)
            continue;
        picking::add_top_strip(&area, vertices->data, mesh->stride, mesh->vertex_count_, indices->indices,
                               core::at(mesh->index_counts_, 0));
    }
    const float share = picking::get_level_share(area);
    level_shares()[checksum] = {geom, share};
    return share;
}

} // namespace

bool get_see_through(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    return sector && plain_geom(geom_of(sector)) && geom_see_through(geom_of(sector));
}

float get_level_share(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    render::PcGeom* geom = sector ? geom_of(sector) : nullptr;
    return plain_geom(geom) ? geom_level_share(checksum, geom) : 1.0f;
}

bool get_passed(uint32_t checksum, V3 camera)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    render::PcGeom* geom = sector ? geom_of(sector) : nullptr;
    if (!plain_geom(geom))
        return false;
    OrientedBox box{};
    V3 min{}, max{};
    if (current_box(checksum, geom, &box)) {
        bounds_of(box, &min, &max);
        if (picking::is_structure(min, max, camera, geom_level_share(checksum, geom)))
            return true;
    }
    return geom_see_through(geom);
}

bool get_pick(V3 origin, V3 unit_dir, float max_t, Hit* hit, std::vector<Layer>* layers)
{
    *hit = Hit{};
    std::vector<picking::Met> met;
    // (without `layers`: nothing past the first occluding piece that isn't locked can matter - a plain click stops at
    // it, and Alt takes it or something nearer)
    float stop = max_t;
    auto note = [&](uint32_t checksum, render::PcGeom* geom, float t, bool collision) {
        if (view::hidden_sector(checksum)) // (H: level_editor_view.h - not shown, not met)
            return;
        const bool locked = picking::latched(picking::kScenery, checksum);
        const bool occludes = collision || !(plain_geom(geom) && geom_see_through(geom));
        met.push_back({.id = checksum, .t = t, .passed = get_passed(checksum, origin), .occludes = occludes, .locked = locked});
        if (occludes && !locked && !layers && t < stop)
            stop = t;
    };
    // The collision ray, past collision that has no geometry to show (invisible walls), at most a few dozen faces.
    V3 from = origin;
    for (int i = 0; i < 32; ++i) {
        V3 point{};
        uint32_t checksum = 0;
        if (!cast_static(from, origin + unit_dir * max_t, &point, &checksum))
            break;
        const float t = get_dot(point - origin, unit_dir);
        if (t >= stop)
            break;
        render::WorldCell* sector = find_sector(checksum, nullptr);
        if (sector && geom_of(sector))
            note(checksum, geom_of(sector), t, true);
        from = point + unit_dir * 1.0f;
    }
    // Every shown piece by its triangles (the box only to skip the rest - a decal's box can fill a room): pieces without
    // collision, and the drawn parts of pieces with it that their collision doesn't cover.
    for_each_scene([&](render::WorldScene* scene) {
        for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
            render::PcGeom* geom = geom_of(sector);
            if (!plain_geom(geom) || !geom->active)
                return;
            OrientedBox box{};
            if (!current_box(checksum, geom, &box))
                return;
            float t = 0.0f;
            bool inside = false;
            {
                V3 min{}, max{};
                bounds_of(box, &min, &max);
                inside = origin.x >= min.x && origin.x <= max.x && origin.y >= min.y && origin.y <= max.y &&
                         origin.z >= min.z && origin.z <= max.z;
            }
            if (!inside && (!ray_box(origin, unit_dir, box.origin, box.axes, box.min, box.max, &t) || t >= stop))
                return;
            if (ray_meshes(geom, origin, unit_dir, stop, &t))
                note(checksum, geom, t, false);
        });
    });
    const picking::Resolved r = picking::settle_up(met);
    hit->checksum = r.take;
    hit->t = r.take_t_;
    hit->passed = r.passed;
    hit->passed_t_ = r.passed_t_;
    hit->blocked = r.blocked;
    hit->block_t_ = r.block_t_;
    for (const picking::Met& m : met) {
        if (!m.locked && !m.passed && (!hit->nearest || m.t < hit->nearest_t_)) {
            hit->nearest = m.id;
            hit->nearest_t_ = m.t;
        }
    }
    if (layers) {
        // (each piece once, at its nearest; not locked)
        for (const picking::Met& m : met) {
            if (m.locked)
                continue;
            auto it = std::ranges::find_if(*layers, [&](const Layer& l) { return l.checksum == m.id; });
            if (it == layers->end())
                layers->push_back({.checksum = m.id, .t = m.t, .passed = m.passed});
            else if (m.t < it->t)
                it->t = m.t;
        }
        std::ranges::stable_sort(*layers, [](const Layer& a, const Layer& b) { return a.t < b.t; });
    }
    return hit->checksum || hit->passed || hit->nearest;
}

bool cuboid(uint32_t checksum, OrientedBox* box)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    if (!sector || !plain_geom(geom_of(sector)))
        return false;
    return current_box(checksum, geom_of(sector), box);
}

const char* why_not_movable(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    if (!sector)
        return "not found";
    if (ground::is_piece(checksum))
        return "generated ground piece: edit with the Ground Tool"; // (UX9)
    render::PcGeom* geom = geom_of(sector);
    if (!geom)
        return "collision only";
    if (!render::is_pc_geom(geom))
        return "odd geometry";
    if (geom->instance)
        return "an instance";
    OrientedBox box{};
    if (!current_box(checksum, geom, &box))
        return "no box";
    if (!sector_size_movable(box.min, box.max, kMaxSectorExtent))
        return "too big";
    return meshes_movable(geom);
}

std::string describe_hover(uint32_t checksum)
{
    std::string text = "scenery " + caption(checksum);
    OrientedBox box{};
    if (cuboid(checksum, &box)) {
        std::array<char, 64> size{};
        const V3 size3 = box_size(box); // (scaled: level_editor_scale.h)
        text::format_to(size, "  %.0f x %.0f x %.0f", size3.x, size3.y, size3.z);
        text += size.data();
    }
    if (const uint32_t cluster = cluster_of(checksum)) {
        const char* name = get_name_of(cluster);
        std::array<char, 64> line{};
        text::format_to(line, "  cluster %s", name ? name : "");
        text += name ? line.data() : "  (in a cluster)";
    }
    if (const char* why = why_not_movable(checksum))
        text += std::string("  (can't move: ") + why + ")";
    return text;
}

std::string describe_selection()
{
    if (level_editor_scenery_state().selection_.empty())
        return "-";
    std::string text = "scenery " + caption(core::at(level_editor_scenery_state().selection_, 0));
    std::array<char, 160> more{};
    int quarters = 0;
    V3 scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f};
    auto it = tracks_state().find(core::at(level_editor_scenery_state().selection_, 0));
    if (it != tracks_state().end()) {
        quarters = it->second.placement.quarters;
        scale = it->second.placement.scale;
    }
    text::format_to(more, "  at (%.0f, %.0f, %.0f)  turned %d", level_editor_scenery_state().pivot_.x, level_editor_scenery_state().pivot_.y, level_editor_scenery_state().pivot_.z, quarters * 90);
    if (!is_unit_scale(scale)) {
        const size_t used = std::strlen(more.data());
        text::format_to(std::span(more).subspan(used), "  scaled %s", scale_tool::describe(scale).c_str());
    }
    if (level_editor_scenery_state().selection_.size() > 1)
        text += " + " + std::to_string(level_editor_scenery_state().selection_.size() - 1) + " more";
    if (!scenery_rails().rails_.empty())
        text += ", " + std::to_string(scenery_rails().rails_.size()) + " rail points";
    return text + more.data();
}

bool has_selection() { return !level_editor_scenery_state().selection_.empty(); }
const std::vector<uint32_t>& get_selection() { return level_editor_scenery_state().selection_; }

bool get_selected(uint32_t checksum) { return std::ranges::find(level_editor_scenery_state().selection_, checksum) != level_editor_scenery_state().selection_.end(); }

void clear_selection()
{
    level_editor_scenery_state().selection_.clear();
    scenery_rails().rails_.clear();
    scenery_rails().dropped_rails_.clear();
}

bool is_shadow(uint32_t checksum, uint32_t* caster)
{
    auto it = shadow_of().find(checksum);
    if (it == shadow_of().end())
        return false;
    if (caster)
        *caster = it->second;
    return true;
}

const std::vector<uint32_t>& selected_rail_names() { return scenery_rails().rails_; }

bool carries_rail(uint32_t name) { return std::ranges::find(scenery_rails().rails_, name) != scenery_rails().rails_.end(); }

void drop_rail(uint32_t name)
{
    if (!carries_rail(name))
        return;
    scenery_rails().dropped_rails_.push_back(name);
    scenery_rails().rails_.erase(std::ranges::find(scenery_rails().rails_, name));
}

void keep_only(uint32_t checksum)
{
    scenery_rails().dropped_rails_.clear();
    set_selection({checksum});
}

const std::vector<uint32_t>& hover_group(uint32_t checksum)
{
    if (checksum != scenery_hover().hover_group_of_) {
        scenery_hover().hover_group_of_ = checksum;
        scenery_hover().hover_group_ = checksum ? group_of(checksum) : std::vector<uint32_t>{};
    }
    return scenery_hover().hover_group_;
}

namespace {
// [features] debug_editor: how the first selected sector's meshes are drawn (whether the material lets a colour
// through).
void log_meshes()
{
    if (!debug_enabled() || level_editor_scenery_state().selection_.empty())
        return;
    const render::PcGeom* geom = sector_geometry(core::at(level_editor_scenery_state().selection_, 0));
    render::MeshRef* const* meshes = geom ? geom->meshes : nullptr;
    const uint32_t count = geom ? geom->mesh_count_ : 0;
    for (uint32_t i = 0; meshes && i < count && i < 4; ++i) {
        const render::MeshRef* mesh = core::at(std::span<render::MeshRef* const>(meshes, count), i);
        const render::MaterialRef* material = mesh->material;
        const auto& colour = mesh->color;
        get_log("level editor:   mesh %u: flags %08X, colour (%.2f, %.2f, %.2f), fvf %08X, material %p: passes %u, "
                "pass flags %08X %08X, colour 0 %08X %08X %08X; renderer 786AB8 %d",
                i, mesh->flags, core::at(colour, 0), core::at(colour, 1), core::at(colour, 2), mesh->fvf, static_cast<const void*>(material),
                material ? material->pass_count_ & 0xFFFFFFF : 0, material ? core::at(material->pass_flags_, 0) : 0,
                material ? core::at(material->pass_flags_, 1) : 0,
                material ? std::bit_cast<uint32_t>(core::at(core::at(material->pass_color_, 0), 0)) : 0,
                material ? std::bit_cast<uint32_t>(core::at(core::at(material->pass_color_, 0), 1)) : 0,
                material ? std::bit_cast<uint32_t>(core::at(core::at(material->pass_color_, 0), 2)) : 0,
                static_cast<uint8_t>(render::renderer().shaders_available()));
    }
}
} // namespace

void select_group(uint32_t checksum)
{
    level_editor_scenery_state().selection_ = group_of(checksum);
    scenery_rails().dropped_rails_.clear();
    compute_pivot();
    select_rails();
    log_meshes();
    if (debug_enabled()) {
        get_log(
            "level editor: selected scenery %s, %u sector%s (cluster %08X), %u rail node%s, pivot (%.1f, %.1f, %.1f)",
            caption(checksum).c_str(), static_cast<unsigned>(level_editor_scenery_state().selection_.size()), level_editor_scenery_state().selection_.size() == 1 ? "" : "s",
            cluster_of(checksum), static_cast<unsigned>(scenery_rails().rails_.size()), scenery_rails().rails_.size() == 1 ? "" : "s", level_editor_scenery_state().pivot_.x,
            level_editor_scenery_state().pivot_.y, level_editor_scenery_state().pivot_.z);
        for (const uint32_t member : level_editor_scenery_state().selection_)
            get_log("level editor:   %s%s", caption(member).c_str(), is_shadow(member) ? " (shadow)" : "");
    }
}

void flip_over(uint32_t checksum)
{
    auto it = std::ranges::find(level_editor_scenery_state().selection_, checksum);
    if (it != level_editor_scenery_state().selection_.end())
        level_editor_scenery_state().selection_.erase(it);
    else if (!why_not_movable(checksum))
        level_editor_scenery_state().selection_.push_back(checksum);
    compute_pivot();
    select_rails();
}

V3 get_pivot() { return level_editor_scenery_state().pivot_; }

float get_bottom() { return level_editor_scenery_state().pivot_.y - level_editor_scenery_state().half_extent_y_; }

void set_selection(const std::vector<uint32_t>& sectors)
{
    level_editor_scenery_state().selection_.clear();
    for (const uint32_t checksum : sectors) {
        if (!why_not_movable(checksum))
            level_editor_scenery_state().selection_.push_back(checksum);
    }
    compute_pivot();
    select_rails();
}

void move_to(V3 pivot)
{
    const V3 delta = pivot - level_editor_scenery_state().pivot_;
    if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f)
        return;
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            move_by(&track->placement, delta);
            apply_track(track);
        }
    }
    for (const uint32_t name : scenery_rails().rails_) {
        if (RailTrack* track = rail_track_of(name))
            move_by(&track->placement, delta);
    }
    apply_rails(scenery_rails().rails_);
    level_editor_scenery_state().pivot_ = pivot;
    scenery_hover().hover_group_of_ = 0;
}

void get_turn(int quarters)
{
    if (!(quarters & 3))
        return;
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            turn_about(&track->placement, level_editor_scenery_state().pivot_, quarters);
            apply_track(track);
        }
    }
    log_meshes();
    for (const uint32_t name : scenery_rails().rails_) {
        if (RailTrack* track = rail_track_of(name))
            turn_about(&track->placement, level_editor_scenery_state().pivot_, quarters);
    }
    apply_rails(scenery_rails().rails_);
    scenery_hover().hover_group_of_ = 0;
}

// --- scaling (level_editor_scale.h) ---

V3 selection_scale()
{
    if (level_editor_scenery_state().selection_.empty())
        return {.x = 1.0f, .y = 1.0f, .z = 1.0f};
    render::WorldCell* sector = find_sector(core::at(level_editor_scenery_state().selection_, 0), nullptr);
    return sector ? placement_of(core::at(level_editor_scenery_state().selection_, 0), geom_of(sector)).scale : V3{.x = 1.0f, .y = 1.0f, .z = 1.0f};
}

int selection_quarters()
{
    if (level_editor_scenery_state().selection_.empty())
        return 0;
    render::WorldCell* sector = find_sector(core::at(level_editor_scenery_state().selection_, 0), nullptr);
    return sector ? placement_of(core::at(level_editor_scenery_state().selection_, 0), geom_of(sector)).quarters : 0;
}

V3 scale_selection(V3 factor, bool uniform)
{
    const V3 unchanged = {.x = 1.0f, .y = 1.0f, .z = 1.0f};
    std::vector<V3> scales, limits; // (along the world's axes)
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            if (!track->original) // (kept now, as apply_track would: a piece that can't be copied stops the lot)
                track->original = scale_tool::get_capture(track->sector, track->geom, track->quarters, track->position);
            if (!track->original) {
                get_log("level editor: %s can't be scaled (its meshes can't be copied)", caption(checksum).c_str());
                return unchanged;
            }
            const V3 world = turned_scale(track->placement.scale, track->placement.quarters);
            scales.push_back(world);
            limits.push_back(scale_tool::get_limit(track->sector, world));
        }
    }
    factor = scale_tool::allowed_factor(factor, scales, limits, uniform);
    if (is_unit_scale(factor) || scales.empty())
        return unchanged;
    // About the middle of the selection's bottom: it keeps standing where it stands.
    const V3 anchor = {.x = level_editor_scenery_state().pivot_.x, .y = get_bottom(), .z = level_editor_scenery_state().pivot_.z};
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            scale_tool::scale_about(&track->placement, anchor, factor);
            apply_track(track);
        }
    }
    for (const uint32_t name : scenery_rails().rails_) {
        if (RailTrack* track = rail_track_of(name))
            scale_tool::scale_about(&track->placement, anchor, factor);
    }
    apply_rails(scenery_rails().rails_);
    level_editor_scenery_state().half_extent_y_ *= factor.y;
    level_editor_scenery_state().pivot_ = {.x = anchor.x, .y = anchor.y + level_editor_scenery_state().half_extent_y_, .z = anchor.z};
    scenery_hover().hover_group_of_ = 0;
    return factor;
}

std::vector<std::pair<V3, V3>> selected_rails()
{
    std::vector<std::pair<V3, V3>> segments;
    for (const uint32_t name : scenery_rails().rails_) {
        const RailNode* node = find_rail(name);
        if (node && node->succ_)
            segments.emplace_back(rail_pos(node), rail_pos(node->succ_));
    }
    return segments;
}

int selected_rail_count() { return static_cast<int>(scenery_rails().rails_.size()); }

// --- grind rail points one by one ---

bool get_rail_point(uint32_t name, V3* pos, bool climbing)
{
    const RailNode* node = find_rail(name);
    if (!node || ((node->flag_bits_ & 0x10) && !climbing))
        return false;
    *pos = rail_pos(node);
    return true;
}

bool rail_origin(uint32_t name, V3* pos)
{
    const RailTrack* track = rail_track_of(name);
    if (!track)
        return false;
    *pos = track->pos0;
    return true;
}

bool set_rail_point(uint32_t name, V3 pos)
{
    RailTrack* track = rail_track_of(name);
    if (!track)
        return false;
    // (the quarter turns a sector move gave it don't matter for one point: a plain move from where the level has it -
    // Home's place gives an offset of exactly 0)
    track->placement = {.quarters = 0, .offset = pos - track->pos0};
    apply_rails({name});
    return true;
}

std::vector<uint32_t> rail_of(uint32_t name)
{
    std::vector<uint32_t> names;
    const RailNode* node = find_rail(name);
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (!node || nodes.empty())
        return names;
    names.push_back(name);
    // (back to its first point, then along to its last - a closed rail stops where it started)
    const RailNode* first = node;
    for (int steps = 0; first->last_value_ && first->last_value_ != node && steps < count; ++steps)
        first = first->last_value_;
    int steps = 0;
    for (const RailNode* at = first; at && steps < count; at = at->succ_, ++steps) {
        // (a rail's points, or a ladder's: not the other kind)
        if (at != node && (at->flag_bits_ & 0x10) == (node->flag_bits_ & 0x10) &&
            std::ranges::find(names, at->label_) == names.end())
            names.push_back(at->label_);
        if (at->succ_ == first)
            break;
    }
    return names;
}

uint32_t rail_generation() { return stored_rail_generation(); }

void rails_rebuilt()
{
    // (the tracks keep their nodes by address: found again by name in the new array - places and flags were copied)
    for (auto it = rail_tracks().begin(); it != rail_tracks().end();) {
        if (RailNode* node = find_rail(it->first)) {
            it->second.node = node;
            ++it;
        } else {
            it = rail_tracks().erase(it);
        }
    }
    ++stored_rail_generation();
}

void for_each_rail_point(void (*each)(void* context, uint32_t name, V3 pos), void* context)
{
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (nodes.empty())
        return;
    for (int i = 0; i < count; ++i)
        each(context, core::at(nodes, i).label_, rail_pos(&core::at(nodes, i)));
}

// --- the selection's state (undo / redo, reset, hiding) ---

namespace {

// WorldCell::change_live (0x0049E9B0, thiscall, RET 4: the geometry's active flag - vfunc +0x34 - and the collision's
// "off" flag 4) / is_operative (0x0049E9E0).
void change_live(render::WorldCell* sector, bool active) { sector->change_live(active); }
bool is_operative(render::WorldCell* sector) { return sector->is_operative(); }

} // namespace

State save_state()
{
    State state;
    state.selection = level_editor_scenery_state().selection_;
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        render::WorldCell* sector = find_sector(checksum, nullptr);
        if (!sector)
            continue;
        state.sectors.push_back({.checksum = checksum, .placement = placement_of(checksum, geom_of(sector)), .hidden = !is_operative(sector)});
    }
    for (const uint32_t name : scenery_rails().rails_) {
        if (const RailTrack* track = rail_track_of(name))
            state.rails.push_back({.name = name, .placement = track->placement, .hidden = track->active0 && !(track->node->flag_bits_ & 4)});
    }
    return state;
}

void apply_states(const std::vector<SectorState>& sectors, const std::vector<RailState>& rails)
{
    for (const SectorState& saved : sectors) {
        Track* track = track_of(saved.checksum);
        if (!track)
            continue;
        if (track->placement.quarters != saved.placement.quarters ||
            !same_v3(track->placement.offset, saved.placement.offset) ||
            !same_v3(track->placement.scale, saved.placement.scale)) {
            track->placement = saved.placement;
            apply_track(track);
        }
        if (is_operative(track->sector) == saved.hidden) {
            if (saved.hidden) // (its shared collision taken first, to go off with it)
                shared_collision::ensure(track->checksum, track->scene, track->sector, track->geom);
            change_live(track->sector, !saved.hidden);
            mark_dirty(track->scene);
        }
        shared_collision::change_live(track->checksum, !saved.hidden);
    }
    std::vector<uint32_t> names;
    for (const RailState& saved : rails) {
        if (RailTrack* track = rail_track_of(saved.name)) {
            track->placement = saved.placement;
            const bool active = track->active0 && !saved.hidden;
            track->node->flag_bits_ = active ? track->node->flag_bits_ | 4u : track->node->flag_bits_ & ~4u;
            names.push_back(saved.name);
        }
    }
    apply_rails(names);
    compute_pivot();
    scenery_hover().hover_group_of_ = 0;
    finalise();
}

void restore_state(const State& state)
{
    apply_states(state.sectors, state.rails);
    scenery_rails().rails_.clear();
    for (const RailState& saved : state.rails)
        scenery_rails().rails_.push_back(saved.name);
    level_editor_scenery_state().selection_ = state.selection;
    compute_pivot();
}

std::vector<SectorState> changed_sectors()
{
    std::vector<SectorState> changed;
    for (const auto& entry : tracks_state()) {
        const Track& track = entry.second;
        if (is_clone(entry.first) || find_sector(entry.first, nullptr) != track.sector)
            continue;
        const bool hidden = !is_operative(track.sector);
        const Placement& p = track.placement;
        if (hidden || p.quarters || !is_unit_scale(p.scale) || p.offset.x != 0.0f || p.offset.y != 0.0f ||
            p.offset.z != 0.0f)
            changed.push_back({.checksum = entry.first, .placement = p, .hidden = hidden});
    }
    return changed;
}

std::vector<RailState> changed_rails()
{
    std::vector<RailState> changed;
    for (const auto& entry : rail_tracks()) {
        const RailTrack& track = entry.second;
        if (find_rail(entry.first) != track.node)
            continue;
        const bool hidden = track.active0 && !(track.node->flag_bits_ & 4);
        const Placement& p = track.placement;
        if (hidden || p.quarters || !is_unit_scale(p.scale) || p.offset.x != 0.0f || p.offset.y != 0.0f ||
            p.offset.z != 0.0f)
            changed.push_back({.name = entry.first, .placement = p, .hidden = hidden});
    }
    return changed;
}

render::WorldScene* main_scene()
{
    render::WorldScene* found = nullptr;
    for_each_scene([&](render::WorldScene* scene) {
        if (!found)
            found = scene;
    });
    return found;
}

bool scene_live(render::WorldScene* scene)
{
    bool live = false;
    for_each_scene([&](render::WorldScene* s) { live |= s == scene; });
    return live && scene;
}

uint32_t world_scene_key(render::WorldScene* scene) { return scene ? scene->checksum_ : 0; }

// --- copies of sectors ---
//
// WorldScene::duplicate_scene_sector (0x0049DF30, thiscall, RET 0x14: the source's checksum, the scene to put it in, instance,
// add-to-collision, the new checksum - 0: a new one; returns the new checksum) as the park editor makes its pieces
// (0x00594730): not an instance (its own meshes and vertex buffers, added to the scene's render lists - so it moves
// as any level sector), flagged 4 to be added to the super sectors (Commit). The copy is made of the source as it is
// now (moved or not); its box (+0x14 / +0x24, not copied for a full clone) is set to the source's box where it is.

std::vector<Replicate>& get_clones()
{
    static std::vector<Replicate> clones;
    return clones;
}

bool is_clone(uint32_t checksum)
{
    const auto& clones = get_clones();
    return std::ranges::any_of(clones, [&](const Replicate& c) { return c.checksum == checksum; });
}

uint32_t get_duplicate(uint32_t source)
{
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(source, &scene);
    if (!sector || why_not_movable(source))
        return 0;
    OrientedBox box{};
    V3 min{}, max{};
    if (!cuboid(source, &box))
        return 0;
    bounds_of(box, &min, &max);
    const Placement source_placement = placement_of(source, geom_of(sector));
    // (the source's shared collision gathered before the copy is there to tie with it)
    auto shared_faces = shared_collision::prepare_copy(source);
    const uint32_t made = scene->clone_sector_named(source, scene, 0, true, 0);
    render::WorldCell* copy = made ? lookup_sector(scene, made) : nullptr;
    render::PcGeom* geom = copy ? geom_of(copy) : nullptr;
    if (!render::is_pc_geom(geom) || geom->instance) {
        get_log("level editor: copy of %s: %s", caption(source).c_str(), made ? "not a plain sector" : "not made");
        return 0;
    }
    geom->bounds.min.x = min.x, geom->bounds.min.y = min.y, geom->bounds.min.z = min.z;
    geom->bounds.max.x = max.x, geom->bounds.max.y = max.y, geom->bounds.max.z = max.z;
    // (the collision's flags are copied with it: a source whose collision is switched off by itself - level_editor_walls.h
    // - gives a copy with collision on, as a layout makes copies again)
    if (gsl::owner<render::HitMesh*> coll = coll_of(copy); coll && is_operative(copy))
        coll->flag_bits_ &= static_cast<uint16_t>(~4);
    shared_collision::finish_copy(shared_faces, made, scene, copy, geom);
    mark_dirty(scene);
    get_clones().push_back({.checksum = made, .source = source, .source_at_copy_ = source_placement});
    std::array<char, 96> name{};
    text::format_to(name, "%s copy", caption(source).c_str());
    script::note_qb_name(made, name.data());
    if (debug_enabled())
        get_log("level editor: copy %08X of %s, box (%.0f, %.0f, %.0f)-(%.0f, %.0f, %.0f), meshes %s", made,
                caption(source).c_str(), min.x, min.y, min.z, max.x, max.y, max.z,
                meshes_movable(geom) ? meshes_movable(geom) : "movable");
    return made;
}

bool placement_now(uint32_t checksum, Placement* placement)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    if (!sector)
        return false;
    *placement = placement_of(checksum, geom_of(sector));
    return true;
}

bool get_hidden(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    return sector && !is_operative(sector);
}

void add_group(uint32_t checksum)
{
    for (const uint32_t member : group_of(checksum)) {
        if (!get_selected(member))
            level_editor_scenery_state().selection_.push_back(member);
    }
    compute_pivot();
    select_rails();
}

std::vector<uint32_t> movable_pieces()
{
    std::vector<uint32_t> pieces;
    render::WorldScene* scene = main_scene();
    if (!scene)
        return pieces;
    for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
        render::PcGeom* geom = geom_of(sector);
        if (plain_geom(geom) && geom->active && !is_clone(checksum) && !why_not_movable(checksum))
            pieces.push_back(checksum);
    });
    return pieces;
}

void reset_selection()
{
    State state = save_state();
    for (SectorState& saved : state.sectors) {
        saved.placement = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
        saved.hidden = false;
    }
    for (RailState& saved : state.rails) {
        saved.placement = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
        saved.hidden = false;
    }
    restore_state(state);
}

void hide_selection()
{
    State state = save_state();
    for (SectorState& saved : state.sectors)
        saved.hidden = true;
    for (RailState& saved : state.rails)
        saved.hidden = true;
    restore_state(state);
    clear_selection();
}

bool selection_moved(Placement* placement)
{
    if (level_editor_scenery_state().selection_.empty())
        return false;
    render::WorldCell* sector = find_sector(core::at(level_editor_scenery_state().selection_, 0), nullptr);
    if (!sector)
        return false;
    *placement = placement_of(core::at(level_editor_scenery_state().selection_, 0), geom_of(sector));
    return placement->quarters != 0 || !is_unit_scale(placement->scale) || placement->offset.x != 0.0f ||
           placement->offset.y != 0.0f || placement->offset.z != 0.0f;
}

void get_reset()
{
    clear_selection();
    clusters_state().clear();
    shadow_of().clear();
    scenery_hover().hover_group_of_ = 0;
    scenery_hover().hover_group_.clear();
    level_shares().clear();
}

render::PcGeom* sector_geometry(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    return sector ? geom_of(sector) : nullptr;
}

render::WorldCell* sector_of(uint32_t checksum) { return find_sector(checksum, nullptr); }

void for_each_level_sector(void (*each)(void* context, uint32_t checksum, render::WorldCell* sector), void* context)
{
    for_each_scene([&](render::WorldScene* scene) {
        for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) { each(context, checksum, sector); });
    });
}

// --- highlighting ---

bool tintable(void* geom) { return render::is_pc_geom(geom); }

namespace {

// How a geometry's colour was before we tinted it: an instance's (its flags' bit 4, its colour), or each mesh's (the
// mesh's flags' bit 4, its colour) - what PcGeom::change_tint (0x004B48B0) writes.
struct Saved {
    TintOwner owner;
    render::PcGeom* geom;
    render::Singleton* instance;
    uint32_t instance_flag_;
    std::array<uint8_t, 4> instance_colour_;
    struct MeshColour {
        uint32_t flag; // bit 4
        std::array<float, 3> color;
    };
    std::vector<MeshColour> meshes;
    bool used;
};
std::vector<Saved>& stored_tinted()
{
    static core::NoDestroy<std::vector<Saved>> tinted;
    return tinted.get();
}

Saved save_colour(TintOwner owner, render::PcGeom* geom)
{
    Saved saved = {.owner = owner, .geom = geom, .instance = geom->instance, .instance_flag_ = 0, .instance_colour_ = {}, .meshes = {}, .used = true};
    if (saved.instance) {
        saved.instance_flag_ = saved.instance->flags & 4;
        std::memcpy(saved.instance_colour_.data(), saved.instance->color.data(), 4);
        return saved;
    }
    render::MeshRef* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        const render::MeshRef* mesh = core::at(std::span<render::MeshRef* const>(meshes, count), i);
        saved.meshes.push_back({.flag = mesh->flags & 4, .color = {core::at(mesh->color, 0), core::at(mesh->color, 1), core::at(mesh->color, 2)}});
    }
    return saved;
}

void restore_colour(const Saved& saved)
{
    if (saved.instance) {
        if (saved.geom->instance != saved.instance)
            return;
        saved.instance->flags = (saved.instance->flags & ~4u) | saved.instance_flag_;
        std::memcpy(saved.instance->color.data(), saved.instance_colour_.data(), 4);
        return;
    }
    render::MeshRef* const* meshes = saved.geom->meshes;
    const uint32_t count = saved.geom->mesh_count_;
    if (!meshes || count != saved.meshes.size())
        return;
    for (uint32_t i = 0; i < count; ++i) {
        render::MeshRef* mesh = core::at(std::span<render::MeshRef* const>(meshes, count), i);
        mesh->flags = (mesh->flags & ~4u) | core::at(saved.meshes, i).flag;
        std::memcpy(mesh->color.data(), core::at(saved.meshes, i).color.data(), sizeof(mesh->color));
    }
}

} // namespace

void get_tint(TintOwner owner, void* geom, uint32_t rgba)
{
    if (!tintable(geom))
        return;
    auto* g = static_cast<render::PcGeom*>(geom);
    auto it = std::ranges::find_if(stored_tinted(), [&](const Saved& s) { return s.geom == g; });
    if (it == stored_tinted().end())
        stored_tinted().push_back(save_colour(owner, g));
    else
        it->used = true;
    static_cast<render::Geom*>(geom)->change_tint(rgba);
}

void end_tints(void* (*resolve)(TintOwner), bool all)
{
    for (auto it = stored_tinted().begin(); it != stored_tinted().end();) {
        if (all || !it->used) {
            if (resolve(it->owner) == it->geom) // (still there: not a dead object's)
                restore_colour(*it);
            it = stored_tinted().erase(it);
        } else {
            it->used = false;
            ++it;
        }
    }
}

void finalise()
{
    if (level_editor_scenery_state().dirty_scenes_.empty())
        return;
    // As the park editor does (0x00592090): the scene's UpdateSuperSectors (thiscall, a list of sectors to add - none)
    // with the bottom-up heap current.
    memory::Hub* manager = memory::Hub::get_pull();
    for (render::WorldScene* scene : level_editor_scenery_state().dirty_scenes_) {
        bool live = false;
        for_each_scene([&](render::WorldScene* s) { live |= s == scene; });
        if (!live || !scene->coll_grid_)
            continue;
        if (manager)
            manager->enter_heap_scope(manager->bottom_up_heap_);
        scene->get_update_super_sectors(nullptr);
        if (manager)
            manager->leave_heap_scope();
        shared_collision::finalise(scene); // (the pieces' shared collision: level_editor_shared_coll.h)
    }
    if (debug_enabled())
        get_log("level editor: super sectors updated in %u scene%s", static_cast<unsigned>(level_editor_scenery_state().dirty_scenes_.size()),
                level_editor_scenery_state().dirty_scenes_.size() == 1 ? "" : "s");
    level_editor_scenery_state().dirty_scenes_.clear();
}

void note_collision_moved(uint32_t checksum)
{
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(checksum, &scene);
    if (!sector)
        return;
    if (flags_of(sector) & 0x40)
        flags_of(sector) |= 0x20; // (as apply_track / WorldCell::place_at_absolute_point 0x0049E6C0)
    mark_dirty(scene);
}

int get_moved_count()
{
    int count = 0;
    for (const auto& entry : tracks_state())
        count += entry.second.moved ? 1 : 0;
    return count;
}

void log_census()
{
    // (every node of the scene list, skies too: what the walk sees)
    auto* node = (render::engine_parts().scene_list().next);
    for (int i = 0; node && i < 16; ++i, node = node->next) {
        const bool end = is_end(node);
        const render::WorldScene* scene = end ? nullptr : scene_of(node);
        const lists::KeyMap* table = scene ? scene->sectors_ : nullptr;
        get_log("level editor: scene list node %d %p: scene %p%s, sky byte %d, name %08X, sector table %p (bits %u)", i,
                static_cast<void*>(node), static_cast<void*>(scene_of(node)), end ? " (end)" : "",
                scene ? static_cast<int>(scene->is_sky_) : -1, scene ? scene->checksum_ : 0,
                static_cast<const void*>(table), table ? static_cast<unsigned>(table->bits_) : 0u);
        if (end)
            break;
    }
    for_each_scene([](render::WorldScene* scene) {
        int sectors = 0, with_geometry = 0, with_collision = 0, movable = 0, see_through = 0;
        for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
            ++sectors;
            with_geometry += geom_of(sector) ? 1 : 0;
            with_collision += coll_of(sector) ? 1 : 0;
            movable += why_not_movable(checksum) ? 0 : 1;
            see_through += geom_of(sector) && plain_geom(geom_of(sector)) && geom_see_through(geom_of(sector)) ? 1 : 0;
        });
        const uint32_t name = scene->checksum_;
        get_log("level editor: scene %08X (%s): %d sectors, %d with geometry, %d with collision, %d movable, %d "
                "see-through",
                name, get_name_of(name) ? get_name_of(name) : "?", sectors, with_geometry, with_collision, movable,
                see_through);
    });
}

} // namespace scenery
} // namespace frontend::level_editor
