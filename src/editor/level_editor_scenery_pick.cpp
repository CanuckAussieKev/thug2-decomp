// NOT ORIGINAL - the level editor's scenery picking: the ray against the level's meshes, the pieces under the
// cursor and their descriptions (ours). See level_editor_scenery.h.

#include "editor/level_editor_scenery.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
#include "editor/level_editor_ground.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shared_coll.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/sector.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/script/nodes.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/render/coll_obj.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_scenery_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_scenery_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor::scenery {
// What the source file's anonymous namespace held that its pieces share (level_editor_scenery_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::scenery

namespace frontend::level_editor {
namespace {

constexpr int kMaxCollisionRayHits = 32; // collision met on the way before the ray gives up
constexpr std::size_t kHoverSizeTextSize = 64;
constexpr std::size_t kHoverLineSize = 64;
constexpr std::size_t kSelectionMoreSize = 160;
constexpr int kDegreesPerQuarterTurn = 90;

} // namespace
namespace scenery {
namespace internal {

SceneryHover& scenery_hover()
{
    static core::NoDestroy<SceneryHover> state;
    return state.get();
}

} // namespace internal
namespace {

// The feeler (as ground_below in level_editor.cpp): constructor 0x00417760, cast 0x00417710 (start, end, movables).
// Its collision data from +0x24: the normal, the point (+0x34), the collision object hit (+0x54) and its checksum
// (+0x58: the object's +4, written by 0x004131F0).
bool cast_static(V3 from, V3 to, V3* point, uint32_t* checksum)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& hit = ray->hit().point;
    *point = {.x = hit[0], .y = hit[1], .z = hit[2]};
    *checksum = ray->hit().object_checksum_;
    return ray->hit().object != nullptr;
}

// The ray against a geometry's meshes as drawn (their vertex copies are where the sector is now: moving it moves
// them): each mesh's first index buffer as a triangle strip; a billboard (turned to the camera when drawn) by its
// sphere.
bool ray_meshes(render::Geom* geom, V3 origin, V3 dir, float max_t, float* hit_distance)
{
    const GeomMeshes meshes = meshes_of(geom);
    bool found = false;
    float best = max_t;
    for (uint32_t i = 0; i < meshes.count && i < kMeshScanLimit; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        if (!mesh) {
            continue;
        }
        float hit = 0.0F;
        if (mesh->billboard() || ((mesh->flags() & kMeshHiddenFlag) != 0U)) {
            if (ray_sphere({.origin = origin, .dir = dir}, {.x = core::at(mesh->centre(), 0), .y = core::at(mesh->centre(), 1), .z = core::at(mesh->centre(), 2)}, mesh->radius(), &hit) &&
                hit < best) {
                best = hit;
                found = true;
            }
            continue;
        }
        const render::VertexBufferData* vertices =
            (mesh->vertex_buffer_count() != 0U) ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        const render::IndexBufferData* indices = core::at(mesh->index_buffers(), 0);
        if (!vertices || !vertices->data() || !indices || !indices->indices() || mesh->stride() < kPositionBytes) {
            continue;
        }
        if (picking::ray_strip(origin, dir, vertices->data(), mesh->stride(), mesh->vertex_count(), indices->indices(), {.index_count_ = core::at(mesh->index_counts(), 0), .max_t_ = best}, &hit)) {
            best = hit;
            found = true;
        }
    }
    if (found) {
        *hit_distance = best;
    }
    return found;
}

} // namespace
namespace internal {

// Whether a geometry's meshes are all see-through (Picking::material_see_through: the material's +0x20 - the first
// pass's flags, 0x40 the scene's transparent sort - and +0x80, its blend mode; the scene loader 0x004D3390 fills both).
bool geom_see_through(render::Geom* geom)
{
    const GeomMeshes meshes = meshes_of(geom);
    const uint32_t count = meshes.count;
    if (count == 0 || count > kMeshScanLimit) {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        const render::MaterialRef* material = mesh ? mesh->material() : nullptr;
        if (!material || !picking::material_see_through({.pass_flags_ = core::at(material->pass_flags(), 0),
                                                         .blend_mode_ = core::at(material->blend_modes(), 0)})) {
            return false;
        }
    }
    return true;
}

// The level part of a geometry's upward-facing surface (its meshes' first index buffers as strips, as ray_meshes reads
// them; billboards left out), measured once a piece while the editor runs (moves and quarter turns don't change it).
std::unordered_map<uint32_t, std::pair<render::Geom*, float>>& level_shares()
{
    static std::unordered_map<uint32_t, std::pair<render::Geom*, float>> value;
    return value;
}

float geom_level_share(uint32_t checksum, render::Geom* geom)
{
    auto it = level_shares().find(checksum);
    if (it != level_shares().end() && it->second.first == geom) {
        return it->second.second;
    }
    picking::TopArea area;
    const GeomMeshes meshes = meshes_of(geom);
    for (uint32_t i = 0; i < meshes.count && i < kMeshScanLimit; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        if (!mesh || mesh->billboard() || ((mesh->flags() & kMeshHiddenFlag) != 0U)) {
            continue;
        }
        const render::VertexBufferData* vertices =
            (mesh->vertex_buffer_count() != 0U) ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        const render::IndexBufferData* indices = core::at(mesh->index_buffers(), 0);
        if (!vertices || !vertices->data() || !indices || !indices->indices() || mesh->stride() < kPositionBytes) {
            continue;
        }
        picking::add_top_strip(&area, vertices->data(), mesh->stride(), mesh->vertex_count(), indices->indices(),
                               core::at(mesh->index_counts(), 0));
    }
    const float share = picking::get_level_share(area);
    level_shares()[checksum] = {geom, share};
    return share;
}

// Whether a geometry's drawn triangles (its meshes' first index buffers as strips, as ray_meshes reads them - where the
// sector is now; billboards left out) come within `reach` of `point`.
bool geom_near(render::Geom* geom, V3 point, float reach)
{
    const GeomMeshes meshes = meshes_of(geom);
    for (uint32_t i = 0; i < meshes.count && i < kMeshScanLimit; ++i) {
        const render::MeshData* mesh = meshes.at(i);
        if (!mesh || mesh->billboard() || ((mesh->flags() & kMeshHiddenFlag) != 0U)) {
            continue;
        }
        const render::VertexBufferData* vertices =
            (mesh->vertex_buffer_count() != 0U) ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        const render::IndexBufferData* indices = core::at(mesh->index_buffers(), 0);
        if (!vertices || !vertices->data() || !indices || !indices->indices() || mesh->stride() < kPositionBytes) {
            continue;
        }
        if (picking::strip_near(point, reach, vertices->data(), mesh->stride(), mesh->vertex_count(),
                                indices->indices(), core::at(mesh->index_counts(), 0))) {
            return true;
        }
    }
    return false;
}

} // namespace internal

bool get_see_through(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    return (sector != nullptr) && plain_geom(geom_of(sector)) && geom_see_through(geom_of(sector));
}

float get_level_share(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    render::Geom* geom = sector ? geom_of(sector) : nullptr;
    return plain_geom(geom) ? geom_level_share(checksum, geom) : 1.0F;
}

bool get_passed(uint32_t checksum, V3 camera)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    render::Geom* geom = sector ? geom_of(sector) : nullptr;
    if (!plain_geom(geom)) {
        return false;
    }
    OrientedBox box{};
    V3 min{};
    V3 max{};
    if (current_box(checksum, geom, &box)) {
        bounds_of(box, &min, &max);
        if (picking::is_structure(min, max, camera, geom_level_share(checksum, geom))) {
            return true;
        }
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
    auto note = [&](uint32_t checksum, render::Geom* geom, float fraction, bool collision) {
        if (view::hidden_sector(checksum)) { // (H: level_editor_view.h - not shown, not met)
            return;
        }
        const bool locked = picking::latched(picking::kScenery, checksum);
        const bool occludes = collision || !(plain_geom(geom) && geom_see_through(geom));
        met.push_back({.id = checksum,
                       .t = fraction,
                       .passed = get_passed(checksum, origin),
                       .occludes = occludes,
                       .locked = locked});
        if (occludes && !locked && !layers && fraction < stop) {
            stop = fraction;
        }
    };
    // The collision ray, past collision that has no geometry to show (invisible walls), at most a few dozen faces.
    V3 from = origin;
    for (int i = 0; i < kMaxCollisionRayHits; ++i) {
        V3 point{};
        uint32_t checksum = 0;
        if (!cast_static(from, origin + unit_dir * max_t, &point, &checksum)) {
            break;
        }
        const float fraction = get_dot(point - origin, unit_dir);
        if (fraction >= stop) {
            break;
        }
        render::WorldCell* sector = find_sector(checksum, nullptr);
        if (sector && geom_of(sector)) {
            note(checksum, geom_of(sector), fraction, true);
        }
        from = point + unit_dir * 1.0F;
    }
    // Every shown piece by its triangles (the box only to skip the rest - a decal's box can fill a room): pieces without
    // collision, and the drawn parts of pieces with it that their collision doesn't cover.
    for_each_scene([&](render::WorldScene* scene) {
        for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
            render::Geom* geom = geom_of(sector);
            if (!plain_geom(geom) || !render::device().geom_active(geom)) {
                return;
            }
            OrientedBox box{};
            if (!current_box(checksum, geom, &box)) {
                return;
            }
            float fraction = 0.0F;
            bool inside = false;
            {
                V3 min{};
                V3 max{};
                bounds_of(box, &min, &max);
                inside = origin.x >= min.x && origin.x <= max.x && origin.y >= min.y && origin.y <= max.y &&
                         origin.z >= min.z && origin.z <= max.z;
            }
            if (!inside &&
                (!ray_box({.origin = origin, .dir = unit_dir}, box.origin, box.axes, box.min, box.max, &fraction) ||
                 fraction >= stop)) {
                return;
            }
            if (ray_meshes(geom, origin, unit_dir, stop, &fraction)) {
                note(checksum, geom, fraction, false);
            }
        });
    });
    const picking::Resolved resolved = picking::settle_up(met);
    hit->checksum = resolved.take;
    hit->t = resolved.take_t_;
    hit->passed = resolved.passed;
    hit->passed_t_ = resolved.passed_t_;
    hit->blocked = resolved.blocked;
    hit->block_t_ = resolved.block_t_;
    for (const picking::Met& met_entry : met) {
        if (!met_entry.locked && !met_entry.passed && ((hit->nearest == 0U) || met_entry.t < hit->nearest_t_)) {
            hit->nearest = met_entry.id;
            hit->nearest_t_ = met_entry.t;
        }
    }
    if (layers) {
        // (each piece once, at its nearest; not locked)
        for (const picking::Met& met_entry : met) {
            if (met_entry.locked) {
                continue;
            }
            auto it = std::ranges::find_if(*layers, [&](const Layer& layer) { return layer.checksum == met_entry.id; });
            if (it == layers->end()) {
                layers->push_back({.checksum = met_entry.id, .t = met_entry.t, .passed = met_entry.passed});
            } else if (met_entry.t < it->t) {
                it->t = met_entry.t;
            }
        }
        std::ranges::stable_sort(*layers, [](const Layer& a, const Layer& b) { return a.t < b.t; });
    }
    return (hit->checksum != 0U) || (hit->passed != 0U) || (hit->nearest != 0U);
}

bool cuboid(uint32_t checksum, OrientedBox* box)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    if (!sector || !plain_geom(geom_of(sector))) {
        return false;
    }
    return current_box(checksum, geom_of(sector), box);
}

const char* why_not_movable(uint32_t checksum)
{
    render::WorldCell* sector = find_sector(checksum, nullptr);
    if (!sector) {
        return "not found";
    }
    if (ground::is_piece(checksum)) {
        return "generated ground piece: edit with the Ground Tool"; // (UX9)
    }
    render::Geom* geom = geom_of(sector);
    if (!geom) {
        return "collision only";
    }
    if (!render::is_pc_geom(geom)) {
        return "odd geometry";
    }
    if (render::device().geom_instance(geom)) {
        return "an instance";
    }
    OrientedBox box{};
    if (!current_box(checksum, geom, &box)) {
        return "no box";
    }
    if (!sector_size_movable(box.min, box.max, kMaxSectorExtent)) {
        return "too big";
    }
    return meshes_movable(geom);
}

std::string describe_hover(uint32_t checksum)
{
    std::string text = "scenery " + caption(checksum);
    OrientedBox box{};
    if (cuboid(checksum, &box)) {
        std::array<char, kHoverSizeTextSize> size{};
        const V3 size3 = box_size(box); // (scaled: level_editor_scale.h)
        text::format_to(size, "  %.0f x %.0f x %.0f", size3.x, size3.y, size3.z);
        text += size.data();
    }
    if (const uint32_t cluster = cluster_of(checksum)) {
        const char* name = get_name_of(cluster);
        std::array<char, kHoverLineSize> line{};
        text::format_to(line, "  cluster %s", name ? name : "");
        text += name ? line.data() : "  (in a cluster)";
    }
    if (const char* why = why_not_movable(checksum)) {
        text += std::string("  (can't move: ") + why + ")";
    }
    return text;
}

std::string describe_selection()
{
    if (picked_scenery().selection_.empty()) {
        return "-";
    }
    std::string text = "scenery " + caption(core::at(picked_scenery().selection_, 0));
    std::array<char, kSelectionMoreSize> more{};
    int quarters = 0;
    V3 scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    auto it = tracks_state().find(core::at(picked_scenery().selection_, 0));
    if (it != tracks_state().end()) {
        quarters = it->second.placement.quarters;
        scale = it->second.placement.scale;
    }
    text::format_to(more, "  at (%.0f, %.0f, %.0f)  turned %d", picked_scenery().pivot_.x, picked_scenery().pivot_.y, picked_scenery().pivot_.z, quarters * kDegreesPerQuarterTurn);
    if (!is_unit_scale(scale)) {
        const size_t used = std::strlen(more.data());
        text::format_to(std::span(more).subspan(used), "  scaled %s", scale_tool::describe(scale).c_str());
    }
    if (picked_scenery().selection_.size() > 1) {
        text += " + " + std::to_string(picked_scenery().selection_.size() - 1) + " more";
    }
    if (!scenery_rails().rails_.empty()) {
        text += ", " + std::to_string(scenery_rails().rails_.size()) + " rail points";
    }
    return text + more.data();
}

bool has_selection() { return !picked_scenery().selection_.empty(); }
const std::vector<uint32_t>& get_selection() { return picked_scenery().selection_; }

bool get_selected(uint32_t checksum) { return std::ranges::find(picked_scenery().selection_, checksum) != picked_scenery().selection_.end(); }

void clear_selection()
{
    picked_scenery().selection_.clear();
    scenery_rails().rails_.clear();
    scenery_rails().dropped_rails_.clear();
}

bool is_shadow(uint32_t checksum, uint32_t* caster)
{
    auto it = shadow_of().find(checksum);
    if (it == shadow_of().end()) {
        return false;
    }
    if (caster) {
        *caster = it->second;
    }
    return true;
}

const std::vector<uint32_t>& selected_rail_names() { return scenery_rails().rails_; }

bool carries_rail(uint32_t name) { return std::ranges::find(scenery_rails().rails_, name) != scenery_rails().rails_.end(); }

void drop_rail(uint32_t name)
{
    if (!carries_rail(name)) {
        return;
    }
    scenery_rails().dropped_rails_.push_back(name);
    scenery_rails().rails_.erase(std::ranges::find(scenery_rails().rails_, name));
}

void keep_only(uint32_t checksum)
{
    scenery_rails().dropped_rails_.clear();
    set_selection({checksum});
}

} // namespace scenery
} // namespace frontend::level_editor
