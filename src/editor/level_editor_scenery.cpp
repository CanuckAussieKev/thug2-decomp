// NOT ORIGINAL - the level editor's scenery (ours, added at the user's request). See level_editor_scenery.h. This
// file holds the placement maths, the scene and sector access, the tracks and rails, the selection and its moves;
// the pieces (scenery_groups / scenery_pick) are level_editor_scenery_<name>.cpp, sharing level_editor_scenery_internal.h.

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
#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "engine/memory/arena_alloc.h"
#include "engine/memory/manager.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/sector.h"
#include "base/maths/vec4.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_scene_list.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "engine/objects/rail_manager.h"
#include "engine/render/coll_obj.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "base/maths/maths.h"
#include "editor/level_editor_scenery_internal.h"

namespace frontend::level_editor {
namespace {

constexpr float kMinVerticalOverlap = 5.0F; // a piece must reach this far above the bounds floor to touch them
constexpr int kColourBits = 32; // a packed colour: four 8-bit channels
constexpr int kBitsPerChannel = 8;

constexpr int kSectorFlagMovable = 0x40; // a sector flag; a sector that has it gets kSectorFlagMoved after a move (as WorldCell::place_at_absolute_point does)
constexpr int kSectorFlagMoved = 0x20;
constexpr std::size_t kCaptionSize = 96;
constexpr int kMaxManagerNodes = 100000; // a manager with more nodes is taken as broken
constexpr float kSelectGrowth = 10.0F;   // a rail selection box is grown by this on each axis
constexpr int kRailNodeClimbFlag = 0x10; // a rail node flag: only used when climbing
constexpr float kScaleTolerance = 1e-4F; // two placements are the same within these
constexpr float kOffsetTolerance = 0.5F;

constexpr int kPassCountMask = 0xFFFFFFF; // the part of the pass count that is a count
constexpr std::size_t kDuplicateNameSize = 96;
constexpr int kCensusSceneLimit = 16; // scenes listed in the census

} // namespace

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
    core::at(box.axes, 0) = get_quarter_turn({.x = placement.scale.x, .y = 0.0F, .z = 0.0F}, placement.quarters);
    core::at(box.axes, 1) = {.x = 0.0F, .y = placement.scale.y, .z = 0.0F};
    core::at(box.axes, 2) = get_quarter_turn({.x = 0.0F, .y = 0.0F, .z = placement.scale.z}, placement.quarters);
    box.min = min;
    box.max = max;
    return box;
}

bool sector_size_movable(V3 min, V3 max, float limit)
{
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) { // (also NaN)
        return false;
    }
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
    std::array<V3, kBoxCornerCount> corners{};
    box_corners(box.origin, box.axes, box.min, box.max, corners);
    for (std::size_t i = 0; i < kBoxCornerCount; ++i) {
        grow_box(min, max, i != 0, core::at(corners, i), core::at(corners, i));
    }
}

bool piece_fits(V3 min, V3 max, V3 group_min, V3 group_max)
{
    const V3 size = max - min;
    const V3 group = group_max - group_min;
    const double volume = static_cast<double>(size.x) * size.y * size.z;
    const double group_volume = static_cast<double>(group.x) * group.y * group.z;
    const float across = std::max(group.x, group.z) * 1.25F;
    return volume <= group_volume && size.x <= across && size.z <= across;
}

bool piece_touches(V3 min, V3 max, V3 bounds_low, V3 bounds_high, float margin)
{
    return min.x <= bounds_high.x + margin && max.x >= bounds_low.x - margin && min.z <= bounds_high.z + margin &&
           max.z >= bounds_low.z - margin && min.y <= bounds_high.y + margin && max.y > bounds_low.y + kMinVerticalOverlap;
}

void segment_box(V3 a, V3 b, V3* min, V3* max)
{
    *min = {.x = std::min(a.x, b.x), .y = std::min(a.y, b.y), .z = std::min(a.z, b.z)};
    *max = {.x = std::max(a.x, b.x), .y = std::max(a.y, b.y), .z = std::max(a.z, b.z)};
}

uint32_t get_pulse(uint32_t low, uint32_t high, double seconds)
{
    const double phase = std::fmod(seconds, 1.25) / 1.25;
    const double mix = 0.5 - (0.5 * std::cos(phase * 2.0 * 3.14159265358979));
    uint32_t colour = 0;
    for (int shift = 0; shift < kColourBits; shift += kBitsPerChannel) {
        const double low_byte = (low >> shift) & 0xFF;
        const double high_byte = (high >> shift) & 0xFF;
        colour |= maths::round_non_negative<uint32_t>(low_byte + ((high_byte - low_byte) * mix)) << shift;
    }
    return colour;
}

namespace scenery {
namespace {

constexpr float kHalf = 0.5F; // half

constexpr uint32_t kCluster = 0x1A3A966BU;  // "Cluster" (the node's TrickObject Cluster=)

} // namespace
namespace internal {

render::WorldScene* scene_of(const SceneNode* node) { return static_cast<render::WorldScene*>(node->data()); }
bool is_end(const SceneNode* node) { return node->is_head(); }

render::WorldCell* lookup_sector(render::WorldScene* scene, uint32_t checksum)
{
    return static_cast<render::WorldCell*>(scene->look_up_sector(checksum));
}

// The sector of that checksum in the first (non-sky) scene having one.
render::WorldCell* find_sector(uint32_t checksum, render::WorldScene** scene_out)
{
    render::WorldCell* found = nullptr;
    for_each_scene([&](render::WorldScene* scene) {
        if (found) {
            return;
        }
        if (render::WorldCell* sector = lookup_sector(scene, checksum)) {
            found = sector;
            if (scene_out) {
                *scene_out = scene;
            }
        }
    });
    return found;
}

// (On PC every sector's geometry is a PcGeom - as PcSector reads it.)
render::Geom* geom_of(render::WorldCell* sector) { return static_cast<render::Geom*>(sector->geom_handle()); }

} // namespace internal
namespace {

gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector) { return sector->hit_body(); }
uint32_t& flags_of(render::WorldCell* sector) { return sector->flag_bits(); }
int quarters_of(render::WorldCell* sector) { return sector->rot_y(); }

} // namespace
namespace internal {

// The PC geometry of level scenery (not an instance), active.
bool plain_geom(render::Geom* geom)
{
    return render::is_pc_geom(geom) && (render::device().geom_instance(geom) == nullptr);
}

// Its box as loaded.
bool geom_box(render::Geom* geom, V3* min, V3* max)
{
    const maths::BBox& box = *geom->fetch_bounds();
    *min = {.x = box.min.x, .y = box.min.y, .z = box.min.z};
    *max = {.x = box.max.x, .y = box.max.y, .z = box.max.z};
    return min->x <= max->x && min->y <= max->y && min->z <= max->z;
}

} // namespace internal
namespace {

// The geometry's world position (0x004B46A0: the first mesh's; 0 when never moved).
V3 geom_position(render::Geom* geom)
{
    const maths::Vec4* v = geom->get_absolute_point();
    return {.x = v->x, .y = v->y, .z = v->z};
}

} // namespace
namespace internal {

// The meshes' vertices can be moved: each vertex buffer has its own CPU copy (data) holding exactly the mesh's
// vertices (vertex_count_ of stride bytes) - what MeshRef::change_place walks and uploads.
const char* meshes_movable(render::Geom* geom)
{
    const GeomMeshes meshes = meshes_of(geom);
    const uint32_t count = meshes.count;
    if (count == 0) {
        return "no meshes";
    }
    if (count > kMeshScanLimit) {
        return "odd mesh count";
    }
    for (uint32_t i = 0; i < count; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        if (!mesh) {
            return "no mesh";
        }
        const uint32_t buffers = mesh->vertex_buffer_count();
        const uint32_t vertices = mesh->vertex_count();
        const uint32_t stride = mesh->stride();
        if (buffers > std::size(mesh->vertex_buffers())) {
            return "odd vertex buffers";
        }
        for (uint32_t buffer_index = 0; buffer_index < buffers; ++buffer_index) {
            const render::VertexBufferData* buffer = core::at(mesh->vertex_buffers(), buffer_index);
            if (!buffer || !buffer->data()) {
                return "no vertex copy";
            }
            if (buffer->size() != vertices * stride) {
                return "shared vertex buffer";
            }
        }
    }
    return nullptr;
}

std::map<uint32_t, Track>& tracks_state()
{
    static std::map<uint32_t, Track> value;
    return value;
}

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

} // namespace internal
namespace {

bool same_v3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The track of a sector (made when first touched); null when the sector can't be found or has no plain geometry.
Track* track_of(uint32_t checksum)
{
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(checksum, &scene);
    if (!sector) {
        return nullptr;
    }
    render::Geom* geom = geom_of(sector);
    if (!plain_geom(geom)) {
        return nullptr;
    }
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end()) {
        Track& track = it->second;
        if (track.sector == sector && track.geom == geom && track.scene == scene &&
            track.quarters == quarters_of(sector) && same_v3(track.position, geom_position(geom))) {
            return &track;
        }
        if (track.sector == sector && track.geom == geom && track.scene == scene) {
            shared_collision::rebase(checksum); // (moved by something else: its shared collision's base is now too)
        }
        tracks_state().erase(it); // (another level, or moved by something else: start again from where it is)
    }
    Track track = {};
    track.checksum = checksum;
    track.scene = scene;
    track.sector = sector;
    track.geom = geom;
    if (!geom_box(geom, &track.min0, &track.max0)) {
        return nullptr;
    }
    track.quarters0 = track.quarters = quarters_of(sector);
    track.position0 = track.position = geom_position(geom);
    track.placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    return &tracks_state().emplace(checksum, track).first->second;
}

} // namespace
namespace internal {

// A sector's box now, without making a track for it.
bool current_box(uint32_t checksum, render::Geom* geom, OrientedBox* box)
{
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end() && it->second.geom == geom) {
        *box = placed_box(it->second.min0, it->second.max0, it->second.placement);
        return true;
    }
    V3 min{};
    V3 max{};
    if (!geom_box(geom, &min, &max)) {
        return false;
    }
    *box = placed_box(min, max, {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}});
    return true;
}

} // namespace internal
namespace {

void mark_dirty(render::WorldScene* scene)
{
    if (std::ranges::find(level_editor_scenery_state().dirty_scenes_, scene) ==
        level_editor_scenery_state().dirty_scenes_.end()) {
        level_editor_scenery_state().dirty_scenes_.push_back(scene);
    }
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
            track->placement.scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
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
        const maths::Vec4 to = {.x = position.x, .y = position.y, .z = position.z, .w = 1.0F};
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
                                                   track->position, track->placement.scale, track->quarters0)) {
        track->original.reset(); // (not the sector copied any more: left as the engine has it)
    }
    shared_collision::get_place(track->checksum, track->placement);
    if ((flags_of(track->sector) & kSectorFlagMovable) != 0U) {
        flags_of(track->sector) |= kSectorFlagMoved;
    }
    track->moved = true;
    mark_dirty(track->scene);
}

} // namespace
namespace internal {

const char* get_name_of(uint32_t checksum)
{
    if (const char* name = script::kept_qb_name(checksum)) {
        return name;
    }
    return script::look_up_hash_name(checksum);
}

std::string caption(uint32_t checksum)
{
    std::array<char, kCaptionSize> text{};
    const char* name = get_name_of(checksum);
    if (name) {
        text::format_to(text, "%s", name);
    } else {
        text::format_to(text, "%08X", checksum);
    }
    return text.data();
}

} // namespace internal
namespace {

// A node's TrickObject Cluster= (0: none), by its name (0x0045C1B0) - kept, as looked up (the level doesn't change
// while the editor is on; Reset forgets them).
std::unordered_map<uint32_t, uint32_t>& clusters_state()
{
    static std::unordered_map<uint32_t, uint32_t> value;
    return value;
}

} // namespace
namespace internal {

uint32_t cluster_of(uint32_t checksum)
{
    // (a cluster the editor set - level_editor_clusters.h: its group follows it)
    uint32_t edited = 0;
    if (clusters::get_override(checksum, &edited)) {
        return edited;
    }
    auto it = clusters_state().find(checksum);
    if (it != clusters_state().end()) {
        return it->second;
    }
    auto* node = level_nodes::fetch_vertex(checksum, false);
    uint32_t cluster = 0;
    if (node) {
        node->read_hash_value(kCluster, &cluster, 0);
    }
    clusters_state().emplace(checksum, cluster);
    return cluster;
}

} // namespace internal
namespace {

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
    if (!manager || manager->on_object()) {
        return {};
    }
    if (!manager->nodes() || manager->num() <= 0 || manager->num() >= kMaxManagerNodes) {
        return {};
    }
    return {manager->nodes(), static_cast<size_t>(manager->num())};
}

V3 rail_pos(const RailNode* node) { return {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z}; }

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

} // namespace
namespace internal {

SceneryRails& scenery_rails()
{
    static core::NoDestroy<SceneryRails> state;
    return state.get();
}

} // namespace internal
namespace {

RailNode* find_rail(uint32_t name)
{
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (nodes.empty()) {
        return nullptr;
    }
    // (a ladder's point too - its moves are tracked as a rail's: level_editor_chains.h)
    for (int i = 0; i < count; ++i) {
        if (core::at(nodes, i).label() == name) {
            return &core::at(nodes, i);
        }
    }
    return nullptr;
}

RailTrack* rail_track_of(uint32_t name)
{
    RailNode* node = find_rail(name);
    if (!node) {
        return nullptr;
    }
    auto it = rail_tracks().find(name);
    if (it != rail_tracks().end()) {
        if (it->second.node == node && same_v3(it->second.pos, rail_pos(node))) {
            return &it->second;
        }
        rail_tracks().erase(it);
    }
    const RailTrack track = {.node = node,
                             .active0 = (node->flag_bits() & 4) != 0,
                             .pos0 = rail_pos(node),
                             .placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}},
                             .pos = rail_pos(node)};
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
    if (nodes.empty()) {
        return;
    }
    ++stored_rail_generation(); // (the node layer reads the places again)
    std::vector<RailNode*> moved;
    for (const uint32_t name : names) {
        auto it = rail_tracks().find(name);
        if (it == rail_tracks().end()) {
            continue;
        }
        RailTrack& track = it->second;
        const V3 pos = get_apply(track.placement, track.pos0);
        track.node->at_point().x = pos.x;
        track.node->at_point().y = pos.y;
        track.node->at_point().z = pos.z;
        track.pos = pos;
        moved.push_back(track.node);
    }
    auto is_moved = [&](const RailNode* node) { return node && std::ranges::find(moved, node) != moved.end(); };
    for (int i = 0; i < count; ++i) {
        RailNode* node = &core::at(nodes, i);
        if (!is_moved(node) && !is_moved(node->succ())) {
            continue;
        }
        V3 min{};
        V3 max{};
        segment_box(rail_pos(node), node->succ() ? rail_pos(node->succ()) : rail_pos(node), &min, &max);
        node->lower_limit() = {.x = min.x, .y = min.y, .z = min.z, .w = 1.0F};
        node->upper_limit() = {.x = max.x, .y = max.y, .z = max.z, .w = 1.0F};
    }
}

void compute_pivot()
{
    V3 min = {};
    V3 max = {};
    bool have = false;
    // (its shadows left out while it has anything else: a shadow lies wider than its caster, on the floor below)
    const bool casters =
        std::ranges::any_of(level_editor_scenery_state().selection_, [](uint32_t checksum) { return !shadow_of().contains(checksum); });
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (casters && shadow_of().contains(checksum)) {
            continue;
        }
        OrientedBox box{};
        if (!cuboid(checksum, &box)) {
            continue;
        }
        V3 point_a{};
        V3 point_b{};
        bounds_of(box, &point_a, &point_b);
        grow_box(&min, &max, have, point_a, point_b);
        have = true;
    }
    level_editor_scenery_state().pivot_ = have ? (min + max) * kHalf : V3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    level_editor_scenery_state().half_extent_y_ = have ? (max.y - min.y) * kHalf : 0.0F;
}

// The selection's rails: rail nodes of the selected sectors' clusters (the fire truck's TRG_FireTruck01..25 are in
// Fire_Tender), and ones lying within a selected sector's box (both ends, within 10 units).
void select_rails()
{
    scenery_rails().rails_.clear();
    const std::span<RailNode> nodes = rail_nodes();
    const int count = static_cast<int>(nodes.size());
    if (level_editor_scenery_state().selection_.empty() || nodes.empty()) {
        return;
    }
    std::vector<uint32_t> clusters;
    std::vector<std::pair<V3, V3>> boxes;
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (is_clone(checksum)) {
            continue; // (a copy has no rails of its own: the source's stay with the source)
        }
        clusters.push_back(checksum);
        if (const uint32_t cluster = cluster_of(checksum)) {
            clusters.push_back(cluster);
        }
        OrientedBox box{};
        V3 min{};
        V3 max{};
        if (!shadow_of().contains(checksum) && cuboid(checksum, &box)) { // (not by a shadow's box: rails lying over it)
            bounds_of(box, &min, &max);
            boxes.emplace_back(min - V3{.x = kSelectGrowth, .y = kSelectGrowth, .z = kSelectGrowth}, max + V3{.x = kSelectGrowth, .y = kSelectGrowth, .z = kSelectGrowth});
        }
    }
    auto within = [&](V3 point) {
        return std::ranges::any_of(boxes, [&](const auto& box) {
            return point.x >= box.first.x && point.x <= box.second.x && point.y >= box.first.y &&
                   point.y <= box.second.y && point.z >= box.first.z && point.z <= box.second.z;
        });
    };
    auto listed = [&](uint32_t colour) { return colour && std::ranges::find(clusters, colour) != clusters.end(); };
    for (int i = 0; i < count; ++i) {
        const RailNode& node = core::at(nodes, i);
        if ((node.flag_bits() & kRailNodeClimbFlag) != 0U) {
            continue;
        }
        bool in = listed(node.label()) || listed(cluster_of(node.label())) ||
                  (within(rail_pos(&node)) && ((node.succ() == nullptr) || within(rail_pos(node.succ()))));
        if (in &&
            std::ranges::find(scenery_rails().dropped_rails_, node.label()) != scenery_rails().dropped_rails_.end()) {
            in = false;
        }
        if (in && std::ranges::find(scenery_rails().rails_, node.label()) == scenery_rails().rails_.end()) {
            scenery_rails().rails_.push_back(node.label());
        }
    }
}

} // namespace
namespace internal {

// Where the editor has put a sector (none yet: where the level has it).
Placement placement_of(uint32_t checksum, render::Geom* geom)
{
    auto it = tracks_state().find(checksum);
    if (it != tracks_state().end() && it->second.geom == geom) {
        return it->second.placement;
    }
    return {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
}

bool same_placement(const Placement& a, const Placement& b)
{
    return a.quarters == b.quarters && std::fabs(a.scale.x - b.scale.x) < kScaleTolerance &&
           std::fabs(a.scale.y - b.scale.y) < kScaleTolerance && std::fabs(a.scale.z - b.scale.z) < kScaleTolerance &&
           std::fabs(a.offset.x - b.offset.x) < kOffsetTolerance && std::fabs(a.offset.y - b.offset.y) < kOffsetTolerance &&
           std::fabs(a.offset.z - b.offset.z) < kOffsetTolerance;
}

} // namespace internal
namespace {

// [features] debug_editor: how the first selected sector's meshes are drawn (whether the material lets a colour
// through).
void log_meshes()
{
    if (!debug_enabled() || level_editor_scenery_state().selection_.empty()) {
        return;
    }
    const render::Geom* geom = sector_geometry(core::at(level_editor_scenery_state().selection_, 0));
    const GeomMeshes meshes = meshes_of(geom); // (none without a geom)
    for (uint32_t i = 0; i < meshes.count && i < 4; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        const render::MaterialRef* material = mesh->material();
        const auto& colour = mesh->color();
        get_log("level editor:   mesh %u: flags %08X, colour (%.2f, %.2f, %.2f), fvf %08X, material %p: passes %u, "
                "pass flags %08X %08X, colour 0 %08X %08X %08X; renderer 786AB8 %d",
                i, mesh->flags(), core::at(colour, 0), core::at(colour, 1), core::at(colour, 2), mesh->fvf(), static_cast<const void*>(material),
                material ? material->pass_count() & kPassCountMask : 0, material ? core::at(material->pass_flags(), 0) : 0,
                material ? core::at(material->pass_flags(), 1) : 0,
                material ? std::bit_cast<uint32_t>(core::at(core::at(material->pass_color(), 0), 0)) : 0,
                material ? std::bit_cast<uint32_t>(core::at(core::at(material->pass_color(), 0), 1)) : 0,
                material ? std::bit_cast<uint32_t>(core::at(core::at(material->pass_color(), 0), 2)) : 0,
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
        for (const uint32_t member : level_editor_scenery_state().selection_) {
            get_log("level editor:   %s%s", caption(member).c_str(), is_shadow(member) ? " (shadow)" : "");
        }
    }
}

void flip_over(uint32_t checksum)
{
    auto it = std::ranges::find(level_editor_scenery_state().selection_, checksum);
    if (it != level_editor_scenery_state().selection_.end()) {
        level_editor_scenery_state().selection_.erase(it);
    } else if (!why_not_movable(checksum)) {
        level_editor_scenery_state().selection_.push_back(checksum);
    }
    compute_pivot();
    select_rails();
}

V3 get_pivot() { return level_editor_scenery_state().pivot_; }

float get_bottom() { return level_editor_scenery_state().pivot_.y - level_editor_scenery_state().half_extent_y_; }

void set_selection(const std::vector<uint32_t>& sectors)
{
    level_editor_scenery_state().selection_.clear();
    for (const uint32_t checksum : sectors) {
        if (!why_not_movable(checksum)) {
            level_editor_scenery_state().selection_.push_back(checksum);
        }
    }
    compute_pivot();
    select_rails();
}

void move_to(V3 pivot)
{
    const V3 delta = pivot - level_editor_scenery_state().pivot_;
    if (delta.x == 0.0F && delta.y == 0.0F && delta.z == 0.0F) {
        return;
    }
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            move_by(&track->placement, delta);
            apply_track(track);
        }
    }
    for (const uint32_t name : scenery_rails().rails_) {
        if (RailTrack* track = rail_track_of(name)) {
            move_by(&track->placement, delta);
        }
    }
    apply_rails(scenery_rails().rails_);
    level_editor_scenery_state().pivot_ = pivot;
    scenery_hover().hover_group_of_ = 0;
}

void get_turn(int quarters)
{
    if ((quarters & 3) == 0) {
        return;
    }
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            turn_about(&track->placement, level_editor_scenery_state().pivot_, quarters);
            apply_track(track);
        }
    }
    log_meshes();
    for (const uint32_t name : scenery_rails().rails_) {
        if (RailTrack* track = rail_track_of(name)) {
            turn_about(&track->placement, level_editor_scenery_state().pivot_, quarters);
        }
    }
    apply_rails(scenery_rails().rails_);
    scenery_hover().hover_group_of_ = 0;
}

// --- scaling (level_editor_scale.h) ---

V3 selection_scale()
{
    if (level_editor_scenery_state().selection_.empty()) {
        return {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    }
    render::WorldCell* sector = find_sector(core::at(level_editor_scenery_state().selection_, 0), nullptr);
    return sector ? placement_of(core::at(level_editor_scenery_state().selection_, 0), geom_of(sector)).scale
                  : V3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
}

int selection_quarters()
{
    if (level_editor_scenery_state().selection_.empty()) {
        return 0;
    }
    render::WorldCell* sector = find_sector(core::at(level_editor_scenery_state().selection_, 0), nullptr);
    return sector ? placement_of(core::at(level_editor_scenery_state().selection_, 0), geom_of(sector)).quarters : 0;
}

V3 scale_selection(V3 factor, bool uniform)
{
    const V3 unchanged = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    std::vector<V3> scales;
    std::vector<V3> limits; // (along the world's axes)
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            if (!track->original) { // (kept now, as apply_track would: a piece that can't be copied stops the lot)
                track->original = scale_tool::get_capture(track->sector, track->geom, track->quarters, track->position);
            }
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
    if (is_unit_scale(factor) || scales.empty()) {
        return unchanged;
    }
    // About the middle of the selection's bottom: it keeps standing where it stands.
    const V3 anchor = {.x = level_editor_scenery_state().pivot_.x, .y = get_bottom(), .z = level_editor_scenery_state().pivot_.z};
    for (const uint32_t checksum : level_editor_scenery_state().selection_) {
        if (Track* track = track_of(checksum)) {
            scale_tool::scale_about(&track->placement, anchor, factor);
            apply_track(track);
        }
    }
    for (const uint32_t name : scenery_rails().rails_) {
        if (RailTrack* track = rail_track_of(name)) {
            scale_tool::scale_about(&track->placement, anchor, factor);
        }
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
        if (node && node->succ()) {
            segments.emplace_back(rail_pos(node), rail_pos(node->succ()));
        }
    }
    return segments;
}

int selected_rail_count() { return static_cast<int>(scenery_rails().rails_.size()); }

// --- grind rail points one by one ---

bool get_rail_point(uint32_t name, V3* pos, bool climbing)
{
    const RailNode* node = find_rail(name);
    if (!node || (((node->flag_bits() & kRailNodeClimbFlag) != 0U) && !climbing)) {
        return false;
    }
    *pos = rail_pos(node);
    return true;
}

bool rail_origin(uint32_t name, V3* pos)
{
    const RailTrack* track = rail_track_of(name);
    if (!track) {
        return false;
    }
    *pos = track->pos0;
    return true;
}

bool set_rail_point(uint32_t name, V3 pos)
{
    RailTrack* track = rail_track_of(name);
    if (!track) {
        return false;
    }
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
    if (!node || nodes.empty()) {
        return names;
    }
    names.push_back(name);
    // (back to its first point, then along to its last - a closed rail stops where it started)
    const RailNode* first = node;
    for (int steps = 0; first->last_value() && first->last_value() != node && steps < count; ++steps) {
        first = first->last_value();
    }
    int steps = 0;
    for (const RailNode* at = first; at && steps < count; at = at->succ(), ++steps) {
        // (a rail's points, or a ladder's: not the other kind)
        if (at != node && (at->flag_bits() & kRailNodeClimbFlag) == (node->flag_bits() & kRailNodeClimbFlag) &&
            std::ranges::find(names, at->label()) == names.end()) {
            names.push_back(at->label());
        }
        if (at->succ() == first) {
            break;
        }
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
    if (nodes.empty()) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        each(context, core::at(nodes, i).label(), rail_pos(&core::at(nodes, i)));
    }
}

namespace {

// WorldCell::change_live (checksums::Name{0x0049E9B0}, thiscall, RET 4: the geometry's active flag - vfunc +0x34 - and the collision's
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
        if (!sector) {
            continue;
        }
        state.sectors.push_back({.checksum = checksum, .placement = placement_of(checksum, geom_of(sector)), .hidden = !is_operative(sector)});
    }
    for (const uint32_t name : scenery_rails().rails_) {
        if (const RailTrack* track = rail_track_of(name)) {
            state.rails.push_back({.name = name,
                                   .placement = track->placement,
                                   .hidden = track->active0 && ((track->node->flag_bits() & 4) == 0U)});
        }
    }
    return state;
}

void apply_states(const std::vector<SectorState>& sectors, const std::vector<RailState>& rails)
{
    for (const SectorState& saved : sectors) {
        Track* track = track_of(saved.checksum);
        if (!track) {
            continue;
        }
        if (track->placement.quarters != saved.placement.quarters ||
            !same_v3(track->placement.offset, saved.placement.offset) ||
            !same_v3(track->placement.scale, saved.placement.scale)) {
            track->placement = saved.placement;
            apply_track(track);
        }
        if (is_operative(track->sector) == saved.hidden) {
            if (saved.hidden) { // (its shared collision taken first, to go off with it)
                shared_collision::ensure(track->checksum, track->scene, track->sector, track->geom);
            }
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
            track->node->flag_bits() = active ? track->node->flag_bits() | 4U : track->node->flag_bits() & ~4U;
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
    for (const RailState& saved : state.rails) {
        scenery_rails().rails_.push_back(saved.name);
    }
    level_editor_scenery_state().selection_ = state.selection;
    compute_pivot();
}

std::vector<SectorState> changed_sectors()
{
    std::vector<SectorState> changed;
    for (const auto& entry : tracks_state()) {
        const Track& track = entry.second;
        if (is_clone(entry.first) || find_sector(entry.first, nullptr) != track.sector) {
            continue;
        }
        const bool hidden = !is_operative(track.sector);
        const Placement& placement = track.placement;
        if (hidden || (placement.quarters != 0) || !is_unit_scale(placement.scale) || placement.offset.x != 0.0F ||
            placement.offset.y != 0.0F || placement.offset.z != 0.0F) {
            changed.push_back({.checksum = entry.first, .placement = placement, .hidden = hidden});
        }
    }
    return changed;
}

std::vector<RailState> changed_rails()
{
    std::vector<RailState> changed;
    for (const auto& entry : rail_tracks()) {
        const RailTrack& track = entry.second;
        if (find_rail(entry.first) != track.node) {
            continue;
        }
        const bool hidden = track.active0 && ((track.node->flag_bits() & 4) == 0U);
        const Placement& placement = track.placement;
        if (hidden || (placement.quarters != 0) || !is_unit_scale(placement.scale) || placement.offset.x != 0.0F ||
            placement.offset.y != 0.0F || placement.offset.z != 0.0F) {
            changed.push_back({.name = entry.first, .placement = placement, .hidden = hidden});
        }
    }
    return changed;
}

render::WorldScene* main_scene()
{
    render::WorldScene* found = nullptr;
    for_each_scene([&](render::WorldScene* scene) {
        if (!found) {
            found = scene;
        }
    });
    return found;
}

bool scene_live(render::WorldScene* scene)
{
    bool live = false;
    for_each_scene([&](render::WorldScene* world_scene) { live |= world_scene == scene; });
    return live && (scene != nullptr);
}

uint32_t world_scene_key(render::WorldScene* scene) { return scene ? scene->checksum() : 0; }

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
    return std::ranges::any_of(clones, [&](const Replicate& replicate) { return replicate.checksum == checksum; });
}

uint32_t get_duplicate(uint32_t source)
{
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(source, &scene);
    if (!sector || why_not_movable(source)) {
        return 0;
    }
    OrientedBox box{};
    V3 min{};
    V3 max{};
    if (!cuboid(source, &box)) {
        return 0;
    }
    bounds_of(box, &min, &max);
    const Placement source_placement = placement_of(source, geom_of(sector));
    // (the source's shared collision gathered before the copy is there to tie with it)
    auto shared_faces = shared_collision::prepare_copy(source);
    const uint32_t made = scene->clone_sector_named(source, scene, 0, true, 0);
    render::WorldCell* copy = (made != 0U) ? lookup_sector(scene, made) : nullptr;
    render::Geom* geom = copy ? geom_of(copy) : nullptr;
    if (!render::is_pc_geom(geom) || render::device().geom_instance(geom)) {
        get_log("level editor: copy of %s: %s", caption(source).c_str(),
                (made != 0U) ? "not a plain sector" : "not made");
        return 0;
    }
    maths::BBox& bounds = *geom->fetch_bounds(); // (its box as loaded)
    bounds.min.x = min.x, bounds.min.y = min.y, bounds.min.z = min.z;
    bounds.max.x = max.x, bounds.max.y = max.y, bounds.max.z = max.z;
    // (the collision's flags are copied with it: a source whose collision is switched off by itself - level_editor_walls.h
    // - gives a copy with collision on, as a layout makes copies again)
    if (gsl::owner<render::HitMesh*> coll = coll_of(copy); coll && is_operative(copy)) {
        coll->flag_bits() &= static_cast<uint16_t>(~4);
    }
    shared_collision::finish_copy(shared_faces, made, scene, copy, geom);
    mark_dirty(scene);
    get_clones().push_back({.checksum = made, .source = source, .source_at_copy_ = source_placement});
    std::array<char, kDuplicateNameSize> name{};
    text::format_to(name, "%s copy", caption(source).c_str());
    script::note_qb_name(made, name.data());
    if (debug_enabled()) {
        get_log("level editor: copy %08X of %s, box (%.0f, %.0f, %.0f)-(%.0f, %.0f, %.0f), meshes %s", made,
                caption(source).c_str(), min.x, min.y, min.z, max.x, max.y, max.z,
                meshes_movable(geom) ? meshes_movable(geom) : "movable");
    }
    return made;
}

bool placement_now(uint32_t checksum, Placement* placement)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    if (!sector) {
        return false;
    }
    *placement = placement_of(checksum, geom_of(sector));
    return true;
}

bool get_hidden(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    return (sector != nullptr) && !is_operative(sector);
}

void add_group(uint32_t checksum)
{
    for (const uint32_t member : group_of(checksum)) {
        if (!get_selected(member)) {
            level_editor_scenery_state().selection_.push_back(member);
        }
    }
    compute_pivot();
    select_rails();
}

std::vector<uint32_t> movable_pieces()
{
    std::vector<uint32_t> pieces;
    render::WorldScene* scene = main_scene();
    if (!scene) {
        return pieces;
    }
    for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
        render::Geom* geom = geom_of(sector);
        if (plain_geom(geom) && render::device().geom_active(geom) && !is_clone(checksum) &&
            !why_not_movable(checksum)) {
            pieces.push_back(checksum);
        }
    });
    return pieces;
}

void reset_selection()
{
    State state = save_state();
    for (SectorState& saved : state.sectors) {
        saved.placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
        saved.hidden = false;
    }
    for (RailState& saved : state.rails) {
        saved.placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
        saved.hidden = false;
    }
    restore_state(state);
}

void hide_selection()
{
    State state = save_state();
    for (SectorState& saved : state.sectors) {
        saved.hidden = true;
    }
    for (RailState& saved : state.rails) {
        saved.hidden = true;
    }
    restore_state(state);
    clear_selection();
}

bool selection_moved(Placement* placement)
{
    if (level_editor_scenery_state().selection_.empty()) {
        return false;
    }
    render::WorldCell* sector = find_sector(core::at(level_editor_scenery_state().selection_, 0), nullptr);
    if (!sector) {
        return false;
    }
    *placement = placement_of(core::at(level_editor_scenery_state().selection_, 0), geom_of(sector));
    return placement->quarters != 0 || !is_unit_scale(placement->scale) || placement->offset.x != 0.0F ||
           placement->offset.y != 0.0F || placement->offset.z != 0.0F;
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

GeomMeshes meshes_of(const render::Geom* geom)
{
    render::Device& device = render::device();
    const void* list = device.geom_mesh_list(geom);
    return {.geom = geom, .count = (list != nullptr) ? device.geom_mesh_count(geom) : 0U, .list = list};
}

render::MeshData* GeomMeshes::at(uint32_t index) const { return render::device().geom_mesh(geom, index); }

render::Geom* sector_geometry(uint32_t checksum)
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
    render::Geom* geom;
    render::InstanceData* instance;
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

Saved save_colour(TintOwner owner, render::Geom* geom)
{
    Saved saved = {.owner = owner,
                   .geom = geom,
                   .instance = render::device().geom_instance(geom),
                   .instance_flag_ = 0,
                   .instance_colour_ = {},
                   .meshes = {},
                   .used = true};
    if (saved.instance) {
        saved.instance_flag_ = saved.instance->flags() & 4;
        std::memcpy(saved.instance_colour_.data(), saved.instance->color().data(), 4);
        return saved;
    }
    const GeomMeshes meshes = meshes_of(geom);
    for (uint32_t i = 0; i < meshes.count && i < kMeshScanLimit; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        saved.meshes.push_back({.flag = mesh->flags() & 4, .color = {core::at(mesh->color(), 0), core::at(mesh->color(), 1), core::at(mesh->color(), 2)}});
    }
    return saved;
}

void restore_colour(const Saved& saved)
{
    if (saved.instance) {
        if (render::device().geom_instance(saved.geom) != saved.instance) {
            return;
        }
        saved.instance->flags() = (saved.instance->flags() & ~4U) | saved.instance_flag_;
        std::memcpy(saved.instance->color().data(), saved.instance_colour_.data(), 4);
        return;
    }
    const GeomMeshes meshes = meshes_of(saved.geom);
    const uint32_t count = meshes.count;
    if (meshes.list == nullptr || count != saved.meshes.size()) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        render::MeshData* mesh = meshes.at(i);
        mesh->flags() = (mesh->flags() & ~4U) | core::at(saved.meshes, i).flag;
        std::memcpy(mesh->color().data(), core::at(saved.meshes, i).color.data(), sizeof(mesh->color()));
    }
}

} // namespace

void get_tint(TintOwner owner, void* geom, uint32_t rgba)
{
    if (!tintable(geom)) {
        return;
    }
    auto* pc_geom = static_cast<render::Geom*>(geom);
    auto it = std::ranges::find_if(stored_tinted(), [&](const Saved& saved) { return saved.geom == pc_geom; });
    if (it == stored_tinted().end()) {
        stored_tinted().push_back(save_colour(owner, pc_geom));
    } else {
        it->used = true;
    }
    static_cast<render::Geom*>(geom)->change_tint(rgba);
}

void end_tints(void* (*resolve)(TintOwner), bool all)
{
    for (auto it = stored_tinted().begin(); it != stored_tinted().end();) {
        if (all || !it->used) {
            if (resolve(it->owner) == it->geom) { // (still there: not a dead object's)
                restore_colour(*it);
            }
            it = stored_tinted().erase(it);
        } else {
            it->used = false;
            ++it;
        }
    }
}

void finalise()
{
    if (level_editor_scenery_state().dirty_scenes_.empty()) {
        return;
    }
    // As the park editor does (0x00592090): the scene's UpdateSuperSectors (thiscall, a list of sectors to add - none)
    // with the bottom-up heap current.
    memory::Hub* manager = memory::Hub::get_pull();
    for (render::WorldScene* scene : level_editor_scenery_state().dirty_scenes_) {
        bool live = false;
        for_each_scene([&](render::WorldScene* world_scene) { live |= world_scene == scene; });
        if (!live || !scene->coll_grid()) {
            continue;
        }
        if (manager) {
            manager->enter_heap_scope(manager->bottom_up_heap());
        }
        scene->get_update_super_sectors(nullptr);
        if (manager) {
            manager->leave_heap_scope();
        }
        shared_collision::finalise(scene); // (the pieces' shared collision: level_editor_shared_coll.h)
    }
    if (debug_enabled()) {
        get_log("level editor: super sectors updated in %u scene%s", static_cast<unsigned>(level_editor_scenery_state().dirty_scenes_.size()),
                level_editor_scenery_state().dirty_scenes_.size() == 1 ? "" : "s");
    }
    level_editor_scenery_state().dirty_scenes_.clear();
}

void note_collision_moved(uint32_t checksum)
{
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(checksum, &scene);
    if (!sector) {
        return;
    }
    if ((flags_of(sector) & kSectorFlagMovable) != 0U) {
        flags_of(sector) |= kSectorFlagMoved; // (as apply_track / WorldCell::place_at_absolute_point 0x0049E6C0)
    }
    mark_dirty(scene);
}

int get_moved_count()
{
    int count = 0;
    for (const auto& entry : tracks_state()) {
        count += entry.second.moved ? 1 : 0;
    }
    return count;
}

void log_census()
{
    // (every node of the scene list, skies too: what the walk sees)
    auto* node = (render::engine_parts().scene_list().next());
    for (int i = 0; node && i < kCensusSceneLimit; ++i, node = node->next()) {
        const bool end = is_end(node);
        const render::WorldScene* scene = end ? nullptr : scene_of(node);
        const lists::KeyMap* table = scene ? scene->sectors() : nullptr;
        get_log("level editor: scene list node %d %p: scene %p%s, sky byte %d, name %08X, sector table %p (bits %u)", i,
                static_cast<void*>(node), static_cast<void*>(scene_of(node)), end ? " (end)" : "",
                scene ? static_cast<int>(scene->is_sky()) : -1, scene ? scene->checksum() : 0,
                static_cast<const void*>(table), table ? static_cast<unsigned>(table->bits()) : 0U);
        if (end) {
            break;
        }
    }
    for_each_scene([](render::WorldScene* scene) {
        int sectors = 0;
        int with_geometry = 0;
        int with_collision = 0;
        int movable = 0;
        int see_through = 0;
        for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
            ++sectors;
            with_geometry += geom_of(sector) ? 1 : 0;
            with_collision += coll_of(sector) ? 1 : 0;
            movable += why_not_movable(checksum) ? 0 : 1;
            see_through += geom_of(sector) && plain_geom(geom_of(sector)) && geom_see_through(geom_of(sector)) ? 1 : 0;
        });
        const uint32_t name = scene->checksum();
        get_log("level editor: scene %08X (%s): %d sectors, %d with geometry, %d with collision, %d movable, %d "
                "see-through",
                name, get_name_of(name) ? get_name_of(name) : "?", sectors, with_geometry, with_collision, movable,
                see_through);
    });
}

} // namespace scenery
} // namespace frontend::level_editor
