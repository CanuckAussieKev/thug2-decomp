// NOT ORIGINAL - the level editor's scenery state: what is saved and restored for undo, the copies made, the
// collision and census (ours). See level_editor_scenery.h.

#include "editor/level_editor_scenery.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "engine/memory/arena_alloc.h"
#include "engine/memory/manager.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shared_coll.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/sector.h"
#include "base/maths/vec4.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_obj.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "editor/level_editor_scenery_internal.h"
#include "editor/level_editor_checks.h"

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

constexpr std::size_t kDuplicateNameSize = 96;
constexpr int kCensusSceneLimit = 16; // scenes listed in the census

} // namespace
namespace scenery {
namespace {

// WorldCell::change_live (checksums::Name{0x0049E9B0}, thiscall, RET 4: the geometry's active flag - vfunc +0x34 - and the collision's
// "off" flag 4) / is_operative (0x0049E9E0).
void change_live(render::WorldCell* sector, bool active) { sector->change_live(active); }
bool is_operative(render::WorldCell* sector) { return sector->is_operative(); }

constexpr float kProbeReach = 0.35F;  // the probes' points: this far from the middle along the box's own x / z (of its size)
constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr float kProbeAbove = 1.0F; // the probes start this far above the box's top (under a roof over it)

// [features] debug_editor: a piece turned to any angle (a twist) checked against its collision - the game's ray straight
// down at two points of its top that only the turned box covers (along its own x / z, off the world's axes) and at the two
// that only the unturned one would. Logged with what each met first: the piece's own collision there (and not at the
// unturned points) means it is turned with the piece.
void log_twist_probe(uint32_t checksum, const Placement& placement)
{
    OrientedBox box{};
    if (!debug_enabled() || placement.twist == 0.0F || !cuboid(checksum, &box)) {
        return;
    }
    const V3 size = box_size(box);
    V3 low{};
    V3 high{};
    bounds_of(box, &low, &high);
    const V3 centre = {.x = (low.x + high.x) * 0.5F, .y = high.y, .z = (low.z + high.z) * 0.5F};
    const V3 own_x = make_unit(core::at(box.axes, 0)) * (size.x * kProbeReach);
    const V3 own_z = make_unit(core::at(box.axes, 2)) * (size.z * kProbeReach);
    const V3 world_x = {.x = size.x * kProbeReach, .y = 0.0F, .z = 0.0F};
    const V3 world_z = {.x = 0.0F, .y = 0.0F, .z = size.z * kProbeReach};
    const std::array<std::pair<const char*, V3>, 4> probes = {{{"turned +x+z", own_x + own_z},
                                                               {"turned -x-z", (own_x + own_z) * -1.0F},
                                                               {"unturned +x+z", world_x + world_z},
                                                               {"unturned -x-z", (world_x + world_z) * -1.0F}}};
    get_log("level editor: twist probe %s: heading %.2f degrees, box %.0f x %.0f x %.0f, top %.1f", caption(checksum).c_str(),
            static_cast<double>(heading_of(placement)) * kDegreesPerRadian, size.x, size.y, size.z, high.y);
    for (const auto& [what, offset] : probes) {
        const V3 from = {.x = centre.x + offset.x, .y = high.y + kProbeAbove, .z = centre.z + offset.z};
        const V3 to = {.x = from.x, .y = low.y - kProbeAbove, .z = from.z};
        V3 hit{};
        V3 normal{};
        uint32_t sector = 0;
        const bool met = checks::get_cast(from, to, {.point = &hit, .normal = &normal}, &sector);
        get_log("level editor:   %-14s at (%.1f, %.1f): %s %s at height %.1f", what, from.x, from.z,
                met ? "met" : "nothing", met ? caption(sector).c_str() : "", met ? hit.y : 0.0F);
    }
}

} // namespace

State save_state()
{
    State state;
    state.selection = picked_scenery().selection_;
    for (const uint32_t checksum : picked_scenery().selection_) {
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
        if (track->placement.quarters != saved.placement.quarters || track->placement.twist != saved.placement.twist ||
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
    for (const SectorState& saved : sectors) {
        log_twist_probe(saved.checksum, saved.placement);
    }
}

void restore_state(const State& state)
{
    apply_states(state.sectors, state.rails);
    scenery_rails().rails_.clear();
    for (const RailState& saved : state.rails) {
        scenery_rails().rails_.push_back(saved.name);
    }
    picked_scenery().selection_ = state.selection;
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
        if (hidden || (placement.quarters != 0) || placement.twist != 0.0F || !is_unit_scale(placement.scale) ||
            placement.offset.x != 0.0F || placement.offset.y != 0.0F || placement.offset.z != 0.0F) {
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
        if (hidden || (placement.quarters != 0) || placement.twist != 0.0F || !is_unit_scale(placement.scale) ||
            placement.offset.x != 0.0F || placement.offset.y != 0.0F || placement.offset.z != 0.0F) {
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
            picked_scenery().selection_.push_back(member);
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
    if (picked_scenery().selection_.empty()) {
        return false;
    }
    render::WorldCell* sector = find_sector(core::at(picked_scenery().selection_, 0), nullptr);
    if (!sector) {
        return false;
    }
    *placement = placement_of(core::at(picked_scenery().selection_, 0), geom_of(sector));
    return placement->quarters != 0 || placement->twist != 0.0F || !is_unit_scale(placement->scale) ||
           placement->offset.x != 0.0F ||
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
    if (dirty_scenes().empty()) {
        return;
    }
    // As the park editor does (0x00592090): the scene's UpdateSuperSectors (thiscall, a list of sectors to add - none)
    // with the bottom-up heap current.
    memory::Hub* manager = memory::Hub::get_pull();
    for (render::WorldScene* scene : dirty_scenes()) {
        bool live = false;
        for_each_scene([&](render::WorldScene* world_scene) { live |= world_scene == scene; });
        if (!live || !scene->coll_grid()) {
            continue;
        }
        if (manager) {
            manager->enter_heap_scope(manager->bottom_up_heap());
        }
        scene->get_update_super_sectors(nullptr);
        // (collision moved beyond the box the grid was made over - a prop pack's scene is made at the origin - is in
        // no cell and outside the box the line queries look at: the grid made again over a box that holds it)
        if (maths::BBox box; scene->fit_collision_grid(&box)) {
            get_log("level editor: collision grid of scene %08X grown to (%.0f, %.0f, %.0f) - (%.0f, %.0f, %.0f)",
                    scene->checksum(), box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z);
        }
        if (manager) {
            manager->leave_heap_scope();
        }
        shared_collision::finalise(scene); // (the pieces' shared collision: level_editor_shared_coll.h)
    }
    if (debug_enabled()) {
        get_log("level editor: super sectors updated in %u scene%s", static_cast<unsigned>(dirty_scenes().size()),
                dirty_scenes().size() == 1 ? "" : "s");
    }
    dirty_scenes().clear();
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
