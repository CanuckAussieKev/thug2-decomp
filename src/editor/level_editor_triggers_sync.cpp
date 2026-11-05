// NOT ORIGINAL - the level editor's triggers against the live level: reading the level's own triggers and gaps,
// placing the editor's boxes, and keeping the level's proximity nodes, gaps and checklist in step (ours). See
// level_editor_triggers.h.

#include "editor/level_editor_triggers.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include "base/core/enum_ops.h"
#include "editor/level_editor.h"
#include "engine/render/coll_static.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/sector.h"
#include "base/maths/mat4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/script/load.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include <gsl/pointers>
#include <array>
#include "base/core/checked_index.h"
#include "engine/memory/mem.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "editor/level_editor_triggers_internal.h"

namespace frontend::level_editor::triggers {
// What the source file's anonymous namespace held that its pieces share (level_editor_triggers_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::triggers

namespace frontend::level_editor::triggers {
namespace {

constexpr std::uint32_t kParseNodeArrayIgnoreGaps = 0xF7ECD937U; // ParseNodeArrayIgnoreGaps

constexpr float kBreakBoxGrowth = 12.0F;     // a breakable box is its piece box grown by this on each axis

// --- the level's triggers ---

render::HitMesh* coll_of(render::WorldCell* sector)
{
    render::HitMesh* coll = sector ? sector->hit_body() : nullptr;
    return render::is_static_collider(coll) ? coll : nullptr;
}
bool turned_off_by_us(uint32_t sector) { return std::ranges::find(level_editor_triggers_state().turned_off_, sector) != level_editor_triggers_state().turned_off_.end(); }

V3 to_v3(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

// A sector's trigger faces read into `t` (false: none) - its faces' box, the first ones' corners, whether every face is
// a trigger's; where its collision is now (a wall moved: read again - RefreshMoved).
bool read_faces(render::WorldCell* sector, LevelTrigger* trigger)
{
    const render::HitMesh* coll = coll_of(sector);
    auto* tri = coll ? coll->geometry() : nullptr;
    if (!tri || !tri->faces() || !tri->vertices() || tri->tri_count() == 0) {
        return false;
    }
    trigger->faces.clear();
    trigger->face_count_ = 0;
    trigger->all_trigger_ = true;
    trigger->wall = static_cast<render::Geom*>(sector->geom_handle()) == nullptr; // (no geometry: collision only)
    trigger->bounds_low_ = {.x = tri->bounds().min.x, .y = tri->bounds().min.y, .z = tri->bounds().min.z};
    trigger->bounds_high_ = {.x = tri->bounds().max.x, .y = tri->bounds().max.y, .z = tri->bounds().max.z};
    bool any = false;
    for (int face_index = 0; std::cmp_less(face_index, tri->tri_count()); ++face_index) {
        const render::FaceFlag flags = tri->face_flags(face_index);
        if (!core::any(flags & render::FaceFlag::kTrigger)) {
            trigger->all_trigger_ = false;
            continue;
        }
        std::array<V3, 3> corners{};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const uint32_t index = tri->face_vertex(face_index, k);
            ok = index < tri->num_vertices();
            if (ok) {
                core::at(corners, k) = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
            }
        }
        if (!ok) {
            continue;
        }
        for (const V3& corner : corners) {
            grow_box(&trigger->min, &trigger->max, any, corner, corner);
            any = true;
        }
        if (trigger->face_count_ < kMaxFacesKept) {
            trigger->faces.insert(trigger->faces.end(), corners.begin(), corners.end());
        }
        ++trigger->face_count_;
    }
    return any;
}

void scan_sector(void* /*unused*/, uint32_t checksum, render::WorldCell* sector)
{
    LevelTrigger level_trigger;
    if (!read_faces(sector, &level_trigger)) {
        return;
    }
    level_trigger.sector = checksum;
    if (script::Struct* node = level_nodes::fetch_vertex(checksum, false)) {
        level_trigger.has_node_ = true;
        node->read_hash_value(kTriggerScript, &level_trigger.script, 0);
    }
    level_editor_triggers_state().level_.push_back(std::move(level_trigger));
}

} // namespace
namespace internal {

void scan_level()
{
    level_editor_triggers_state().level_.clear();
    level_editor_triggers_state().scanned_ = true;
    if (!level_nodes::node_manager_instance() || !scenery::main_scene()) {
        return;
    }
    scenery::for_each_level_sector(&scan_sector, nullptr);
    for (LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        level_trigger.calls = get_walk(level_trigger.script);
        level_trigger.kind = kind_of_calls(level_trigger.calls, level_trigger.script != 0);
    }
    get_log("level editor: %u trigger volumes in the level", static_cast<unsigned>(level_editor_triggers_state().level_.size()));
}

} // namespace internal
namespace {

// The level's own gaps' names (every EndGap its nodes' TriggerScripts reach - not the editor's): the gaps the editor
// never takes out of the checklist.
std::vector<std::string>*& stored_names()
{
    static std::vector<std::string>* names{};
    return names;
}
void on_level_gap(script::Struct* params, const uint8_t* /*unused*/)
{
    const char* text = nullptr;
    if (params && params->fetch_text(get_text(), &text, 0) && text) {
        stored_names()->emplace_back(text);
    }
}
void read_level_gaps()
{
    trigger_edit().level_gap_names_.clear();
    trigger_edit().level_gaps_read_ = true;
    std::vector<uint32_t> scripts;
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.added || !node.trigger_script_) {
            continue;
        }
        script::Struct* structure = nodes::struct_of(node);
        uint32_t script = 0;
        if ((structure != nullptr) && structure->read_hash_value(kTriggerScript, &script, 0) && (script != 0U) &&
            std::ranges::find(scripts, script) == scripts.end()) {
            scripts.push_back(script);
        }
    }
    stored_names() = &trigger_edit().level_gap_names_;
    script::clear_script_flag_6f_2564();
    for (const uint32_t script : scripts) {
        if (is_script(script)) {
            level_nodes::for_each_script_call({.script = script, .target = end_gap()}, &on_level_gap, nullptr, 0);
        }
    }
    script::set_script_flag_6f_2564();
    stored_names() = nullptr;
}

// --- the boxes ---

gsl::owner<render::HitMovingBox*> make_box(uint32_t name)
{
    gsl::owner<render::HitMovingBox*> box{memory::make_owned_value<render::HitMovingBox>()};
    if (!box) {
        return nullptr;
    }
    box->assign_checksum(name);
    box->set_poly_attr_bits(kVolumeFlags);
    box->set_surface_type(0);
    render::register_movable(box);
    return box;
}

} // namespace
namespace internal {

void free_box(render::HitMovingBox* box)
{
    render::unregister_movable(box);
    gsl::owner<decltype(box)> owned = memory::owned_element(box); // (the editor's)
    delete owned; // (out of the collision caches too)
}

} // namespace internal
namespace {

void place_box(Cuboid* b, V3 pos, float yaw, V3 size)
{
    const OrientedBox placed = box_of(pos, yaw, size);
    b->coll->box().min = {.x = placed.min.x, .y = placed.min.y, .z = placed.min.z, .w = 1.0F};
    b->coll->box().max = {.x = placed.max.x, .y = placed.max.y, .z = placed.max.z, .w = 1.0F};
    b->coll->set_anchor_point({.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0F});
    maths::Mat4 matrix;
    for (int row = 0; row < 3; ++row) {
        core::at(matrix.row, row) = {.x = core::at(placed.axes, row).x,
                                     .y = core::at(placed.axes, row).y,
                                     .z = core::at(placed.axes, row).z,
                                     .w = 0.0F};
    }
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    b->coll->change_facing(&matrix);
    b->pos = pos;
    b->yaw = yaw;
    b->size = size;
}

} // namespace
namespace internal {

void drop_boxes()
{
    for (const Cuboid& box : trigger_edit().boxes_) {
        free_box(box.coll);
    }
    trigger_edit().boxes_.clear();
}

} // namespace internal
namespace {

// --- the gap checklist ---

goals::GapTally* get_checklist()
{
    modules::Skate* skate = modules::Skate::sole();
    return skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
}

bool level_gap(const std::string& name)
{
    return std::ranges::find(trigger_edit().level_gap_names_, name) != trigger_edit().level_gap_names_.end();
}

// The editor's gaps that are no longer wanted out of `checklist` (a level's own gap of that name stays).
void take_out_gaps(goals::GapTally* checklist, const std::vector<std::pair<std::string, int>>& wanted)
{
    if (!checklist) {
        return;
    }
    for (const auto& gap : trigger_edit().gaps_added_) {
        if (std::ranges::find(wanted, gap) != wanted.end() || level_gap(gap.first)) {
            continue;
        }
        const int index = checklist->find_gap(gap.first.c_str());
        goals::GapEntry* check = index >= 0 ? checklist->get_gap(index) : nullptr;
        if (check && check->points_held() == gap.second) {
            check->teardown(1);
            get_log("level editor: gap \"%s\" taken out of the gap list", gap.first.c_str());
        }
    }
}

// --- syncing what the specs say with the game ---

const char* game_problem(const Spec& spec)
{
    if (const char* problem = get_problem(spec)) {
        return problem;
    }
    if ((spec.kind == Kind::kKill || spec.kind == Kind::kJumpTo) &&
        !level_nodes::fetch_vertex(checksum_of(spec.target), false)) {
        return "That node isn't in this level";
    }
    if (spec.kind == Kind::kScripts && !is_script(checksum_of(spec.script))) {
        return "Script not found";
    }
    return nullptr;
}

// The level's triggers as they are now: switched off or on (Walls::Disengaged - the editor's switch, or a level script's Kill),
// and moved (an invisible wall moved by level_editor_walls.h: its faces read again when its collision's box changed).
void refresh_level()
{
    for (LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        render::WorldCell* sector = scenery::sector_of(level_trigger.sector);
        const render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
        auto* tri = coll ? coll->geometry() : nullptr;
        if (!tri) {
            continue;
        }
        level_trigger.off = walls::disengaged(level_trigger.sector) || (coll->flag_bits() & 4) != 0;
        if (tri->bounds().min.x != level_trigger.bounds_low_.x || tri->bounds().min.y != level_trigger.bounds_low_.y ||
            tri->bounds().min.z != level_trigger.bounds_low_.z || tri->bounds().max.x != level_trigger.bounds_high_.x ||
            tri->bounds().max.y != level_trigger.bounds_high_.y ||
            tri->bounds().max.z != level_trigger.bounds_high_.z) {
            LevelTrigger now = level_trigger;
            if (read_faces(sector, &now)) {
                level_trigger.faces = now.faces;
                level_trigger.min = now.min;
                level_trigger.max = now.max;
            }
            level_trigger.bounds_low_ = now.bounds_low_;
            level_trigger.bounds_high_ = now.bounds_high_;
        }
    }
}

} // namespace
namespace internal {

int spec_index(const std::string& name)
{
    for (size_t i = 0; i < level_editor_triggers_state().specs_.size(); ++i) {
        if (same_text(core::at(level_editor_triggers_state().specs_, i).name, name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// --- the feature map: breakables, proximity nodes ---

// A scenery piece's sector, and whether it's there (WorldCell::is_operative 0x0049E9E0: its geometry's active flag - a Kill,
// the editor's hiding - else its collision's).
bool is_sector_enabled(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    return (sector != nullptr) && sector->is_operative();
}

// A breakable's box: the piece's box where it is now (axis-aligned), grown 6 each way so the skater meets it before
// the piece (false: the piece isn't there).
bool break_place(const Spec& spec, BreakPlace place)
{
    auto* const pos = place.pos;
    auto* const size = place.size;
    const uint32_t piece = checksum_of(spec.target);
    OrientedBox box{};
    if ((piece == 0U) || !scenery::cuboid(piece, &box)) {
        return false;
    }
    V3 min{};
    V3 max{};
    bounds_of(box, &min, &max);
    *pos = (min + max) * kHalf;
    *size = clamp_size((max - min) + V3{.x = kBreakBoxGrowth, .y = kBreakBoxGrowth, .z = kBreakBoxGrowth});
    return true;
}

// Proximity nodes: the edits, what each ProximNode had before the editor touched it (put back), the scripts defined.
std::vector<ProximEdit>& stored_proxims()
{
    static core::NoDestroy<std::vector<ProximEdit>> proxims;
    return proxims.get();
}

} // namespace internal
namespace {

struct ProximOriginal {
    uint32_t script;
    objects::ProximShape shape;
    float radius;
    V3 min, max;
};
std::map<uint32_t, ProximOriginal>& proxim_originals()
{
    static std::map<uint32_t, ProximOriginal> value;
    return value;
}
std::vector<uint8_t>& proxim_defined()
{
    static core::NoDestroy<std::vector<uint8_t>> proxim_defined;
    return proxim_defined.get();
}

} // namespace
namespace internal {

bool have_proxim_manager() { return objects::proximity_hub() != nullptr; }

int proxim_index(uint32_t node)
{
    for (size_t i = 0; i < stored_proxims().size(); ++i) {
        if (core::at(stored_proxims(), i).node == node) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace internal
namespace {

void put_proxim_back(objects::ProximNode* proxim_node, const ProximOriginal& proxim_original)
{
    proxim_node->own_script() = proxim_original.script;
    proxim_node->set_form(proxim_original.shape);
    proxim_node->radius() = proxim_original.radius;
    proxim_node->set_reach_sq(proxim_original.radius * proxim_original.radius);
    proxim_node->box().min = {
        .x = proxim_original.min.x, .y = proxim_original.min.y, .z = proxim_original.min.z, .w = 1.0F};
    proxim_node->box().max = {
        .x = proxim_original.max.x, .y = proxim_original.max.y, .z = proxim_original.max.z, .w = 1.0F};
}

} // namespace
namespace internal {

// Every frame (Guard): each edited node's ProximNode as its edit says - its script (defined here), its shape - and those
// no longer edited (or in a network session) put back.
void sync_proxims()
{
    if (!have_proxim_manager() || (stored_proxims().empty() && proxim_originals().empty())) {
        return;
    }
    const bool refused = layout::get_refused();
    std::vector<ProximScript> scripts;
    std::vector<std::pair<const ProximEdit*, objects::ProximNode*>> live;
    for (const ProximEdit& stored_proxim : stored_proxims()) {
        objects::ProximNode* proxim_node =
            refused || !proxim_edited(stored_proxim) ? nullptr : objects::proxim_get_node(stored_proxim.node);
        if (!proxim_node) {
            continue;
        }
        auto original_it = proxim_originals().find(stored_proxim.node);
        if (original_it == proxim_originals().end()) {
            const ProximOriginal original = {
                .script = proxim_node->own_script(),
                .shape = proxim_node->form(),
                .radius = proxim_node->radius(),
                .min = {.x = proxim_node->box().min.x, .y = proxim_node->box().min.y, .z = proxim_node->box().min.z},
                .max = {.x = proxim_node->box().max.x, .y = proxim_node->box().max.y, .z = proxim_node->box().max.z}};
            original_it = proxim_originals().emplace(stored_proxim.node, original).first;
        }
        if (!stored_proxim.enter.empty() || !stored_proxim.exit.empty()) {
            scripts.push_back({.node = stored_proxim.node,
                               .enter = checksum_of(stored_proxim.enter),
                               .exit = checksum_of(stored_proxim.exit),
                               .own = original_it->second.script});
        }
        live.emplace_back(&stored_proxim, proxim_node);
    }
    // (put back: those not edited now)
    for (auto it = proxim_originals().begin(); it != proxim_originals().end();) {
        const bool kept = std::ranges::any_of(live, [&](const auto& entry) { return entry.first->node == it->first; });
        if (!kept) {
            if (objects::ProximNode* proxim_node = objects::proxim_get_node(it->first)) {
                put_proxim_back(proxim_node, it->second);
            }
            it = proxim_originals().erase(it);
        } else {
            ++it;
        }
    }
    std::vector<uint8_t> file = proxim_qb_file(scripts);
    if (!scripts.empty() && file != proxim_defined()) {
        script::load_qb_data("editor\\thug2_decomp_proxim.qb", file, 0, 0, 0);
        proxim_defined() = file;
    }
    for (const auto& entry : live) {
        const ProximEdit& proxim_edit = *entry.first;
        objects::ProximNode* proxim_node = entry.second;
        const ProximOriginal& proxim_original = proxim_originals()[proxim_edit.node];
        const uint32_t script = !proxim_edit.enter.empty() || !proxim_edit.exit.empty()
                                    ? get_checksums(proxim_script_name(proxim_edit.node).c_str())
                                    : proxim_original.script;
        if (proxim_node->own_script() != script) {
            proxim_node->own_script() = script;
        }
        if (!proxim_edit.shape) {
            proxim_node->set_form(proxim_original.shape);
            proxim_node->radius() = proxim_original.radius;
            proxim_node->set_reach_sq(proxim_original.radius * proxim_original.radius);
            proxim_node->box().min = {
                .x = proxim_original.min.x, .y = proxim_original.min.y, .z = proxim_original.min.z, .w = 1.0F};
            proxim_node->box().max = {
                .x = proxim_original.max.x, .y = proxim_original.max.y, .z = proxim_original.max.z, .w = 1.0F};
        } else if (proxim_edit.box) {
            // (objects::ProximShape::kBox: the world's axes; its middle kept at the node's place plus the offset)
            const V3 centre =
                V3{.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z} +
                proxim_edit.offset;
            const V3 half = proxim_edit.size * 0.5F;
            proxim_node->set_form(objects::ProximShape::kBox);
            proxim_node->box().min = {
                .x = centre.x - half.x, .y = centre.y - half.y, .z = centre.z - half.z, .w = 1.0F};
            proxim_node->box().max = {
                .x = centre.x + half.x, .y = centre.y + half.y, .z = centre.z + half.z, .w = 1.0F};
        } else {
            proxim_node->set_form(objects::ProximShape::kSphere);
            proxim_node->radius() = proxim_edit.radius;
            proxim_node->set_reach_sq(proxim_edit.radius * proxim_edit.radius);
        }
    }
}

} // namespace internal
namespace {

// What each spec is now: alive where its node is, its problem, its calls, a breakable's box and state.
void sync_added()
{
    // What each spec is now.
    level_editor_triggers_state().added_.clear();
    for (const Spec& spec : level_editor_triggers_state().specs_) {
        Added added;
        added.spec = spec;
        added.alive = node_alive(spec.name) && node_place(spec.name, &added.pos, &added.yaw);
        added.problem = game_problem(spec);
        if (spec.kind == Kind::kScripts && !added.problem) {
            added.calls = calls_of(checksum_of(spec.script));
        }
        if (spec.kind == Kind::kBreak && added.alive) {
            // (its box is its piece's, wherever the piece is now; none while the piece is gone)
            V3 pos{};
            V3 size{};
            if (break_place(spec, {.pos = &pos, .size = &size})) {
                added.pos = pos;
                added.yaw = 0.0F;
                added.spec.size = size;
            }
            added.broken = !is_sector_enabled(checksum_of(spec.target));
        }
        level_editor_triggers_state().added_.push_back(added);
    }
}

// The boxes: one for each alive trigger, where its node is (none in a network session or a career-type mode -
// Guard).
void sync_boxes()
{
    // The boxes: one for each alive trigger, where its node is (none in a network session or a career-type mode -
    // Guard).
    const bool network = layout::get_refused();
    for (size_t i = 0; i < trigger_edit().boxes_.size();) {
        int at = -1;
        for (size_t k = 0; k < level_editor_triggers_state().added_.size(); ++k) {
            if (core::at(level_editor_triggers_state().added_, k).alive &&
                !core::at(level_editor_triggers_state().added_, k).broken &&
                hash_of_name(core::at(level_editor_triggers_state().added_, k).spec.name) ==
                    core::at(trigger_edit().boxes_, i).name) {
                at = static_cast<int>(k);
            }
        }
        if (at < 0 || network) {
            free_box(core::at(trigger_edit().boxes_, i).coll);
            trigger_edit().boxes_.erase(trigger_edit().boxes_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const Added& added : level_editor_triggers_state().added_) {
        if (!added.alive || added.broken || network) {
            continue;
        }
        uint32_t name = hash_of_name(added.spec.name);
        auto it = std::ranges::find_if(trigger_edit().boxes_, [&](const Cuboid& b) { return b.name == name; });
        if (it == trigger_edit().boxes_.end()) {
            Cuboid cuboid = {.name = name, .coll = make_box(name), .pos = {}, .size = {}, .yaw = 0.0F};
            if (!cuboid.coll) {
                continue;
            }
            place_box(&cuboid, added.pos, added.yaw, added.spec.size);
            trigger_edit().boxes_.push_back(cuboid);
            get_log("level editor: trigger %s (%s) in place", added.spec.name.c_str(), kind_name(added.spec.kind));
        } else if (it->pos.x != added.pos.x || it->pos.y != added.pos.y || it->pos.z != added.pos.z ||
                   it->yaw != added.yaw || it->size.x != added.spec.size.x || it->size.y != added.spec.size.y ||
                   it->size.z != added.spec.size.z) {
            place_box(&*it, added.pos, added.yaw, added.spec.size);
        }
    }
}

// The level's triggers an alive editable copy stands in for: switched off (through level_editor_walls.h - the
// collision switch's one owner: WorldCell::change_live for a wall), on again once no copy stands in for them.
void sync_replaced()
{
    // The level's triggers an alive editable copy stands in for: switched off (through level_editor_walls.h - the
    // collision switch's one owner: WorldCell::change_live for a wall), on again once no copy stands in for them.
    std::vector<uint32_t> replaced;
    for (const Added& added : level_editor_triggers_state().added_) {
        if (added.alive && (added.spec.replaces != 0U) && scenery::sector_of(added.spec.replaces)) {
            replaced.push_back(added.spec.replaces);
        }
    }
    for (size_t i = 0; i < level_editor_triggers_state().turned_off_.size();) {
        if (std::ranges::find(replaced, core::at(level_editor_triggers_state().turned_off_, i)) == replaced.end()) {
            walls::set_off(core::at(level_editor_triggers_state().turned_off_, i), false);
            level_editor_triggers_state().turned_off_.erase(level_editor_triggers_state().turned_off_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const uint32_t sector : replaced) {
        if (turned_off_by_us(sector)) {
            continue;
        }
        if (!walls::disengaged(sector)) {
            walls::set_off(sector, true);
        }
        level_editor_triggers_state().turned_off_.push_back(sector);
    }
}

// The gap list: built again as the level's load builds it (or when the scripts were defined again); the editor's
// gaps no longer there taken out.
void sync_gaps(bool redefined)
{
    // The gap list: built again as the level's load builds it; the editor's gaps no longer there taken out.
    std::vector<std::pair<std::string, int>> wanted;
    for (const Added& added : level_editor_triggers_state().added_) {
        if (added.alive && !added.problem && added.spec.kind == Kind::kGapEnd) {
            wanted.emplace_back(clean_text(added.spec.text), added.spec.score);
        }
    }
    std::ranges::sort(wanted);
    if (redefined || wanted != trigger_edit().gaps_added_) {
        goals::GapTally* checklist = get_checklist();
        if (checklist && (script::fetch_int_value(kParseNodeArrayIgnoreGaps) == 0)) {
            if (!trigger_edit().level_gaps_read_) {
                read_level_gaps();
            }
            checklist->get_build();
            take_out_gaps(checklist, wanted);
            trigger_edit().checklist_ = checklist;
            if (wanted != trigger_edit().gaps_added_) {
                get_log("level editor: gap list built again - %u of the editor's gaps",
                        static_cast<unsigned>(wanted.size()));
            }
            trigger_edit().gaps_added_ = wanted;
        }
    }
}

} // namespace
namespace internal {

void get_sync()
{
    sync_added();
    // Their scripts (every spec's: a deleted one's undo brings its node back naming it). The game's loader defines
    // them (an unchanged script is left as it is).
    std::vector<Spec> scripted;
    for (const Added& added : level_editor_triggers_state().added_) {
        Spec spec = added.spec;
        if (added.problem) {
            spec.kind = Kind::kOther; // (an empty script: it does nothing yet)
        }
        scripted.push_back(spec);
    }
    std::vector<uint8_t> file = scripted.empty() ? std::vector<uint8_t>() : qb_file(scripted);
    bool redefined = false;
    if (file != trigger_edit().defined_ && !file.empty()) {
        script::load_qb_data(kFileName.data(), file, 0, 0, 0);
        trigger_edit().defined_ = file;
        redefined = true;
    }
    sync_boxes();
    sync_replaced();
    refresh_level();
    sync_gaps(redefined);
    sync_proxims();
}

} // namespace internal

bool get_editor_gaps(const goals::GapTally* checklist, std::vector<std::string>* names,
                     std::vector<std::string>* level_names)
{
    names->clear();
    level_names->clear();
    if (!checklist || checklist != trigger_edit().checklist_ || trigger_edit().gaps_added_.empty()) {
        return false;
    }
    for (const auto& gap : trigger_edit().gaps_added_) {
        names->push_back(gap.first);
    }
    *level_names = trigger_edit().level_gap_names_;
    return true;
}

namespace internal {

// The level's array going (node_create's listener): everything of this level forgotten - its boxes unregistered, the
// editor's gaps out of its checklist (kept by the career for the level).
void get_forget()
{
    drop_boxes();
    take_out_gaps(trigger_edit().checklist_, {});
    trigger_edit().gaps_added_.clear();
    trigger_edit().checklist_ = nullptr;
    level_editor_triggers_state().specs_.clear();
    level_editor_triggers_state().added_.clear();
    level_editor_triggers_state().switched_.clear();
    level_editor_triggers_state().turned_off_.clear(); // (their collision goes with the level)
    level_editor_triggers_state().select_wall_ = 0;
    level_editor_triggers_state().level_.clear();
    level_editor_triggers_state().scanned_ = false;
    trigger_edit().level_gap_names_.clear();
    trigger_edit().level_gaps_read_ = false;
    calls_cache().clear();
    level_editor_triggers_state().inspected_ = 0;
    trigger_edit().has_undo_ = false;
    trigger_edit().select_ = -1;
    trigger_edit().actions_.clear();
    level_editor_triggers_state().level_array_ = 0;
    // (proximity nodes: put back while the level's are still there - none once nearby_zone_shutdown has run)
    if (have_proxim_manager()) {
        for (const auto& proxim_original : proxim_originals()) {
            if (objects::ProximNode* proxim_node = objects::proxim_get_node(proxim_original.first)) {
                put_proxim_back(proxim_node, proxim_original.second);
            }
        }
    }
    stored_proxims().clear();
    proxim_originals().clear();
}

void on_array_removed(uint32_t name)
{
    if ((name == 0U) || name != level_editor_triggers_state().level_array_) {
        return;
    }
    if (!level_editor_triggers_state().specs_.empty()) {
        get_log("level editor: the level's node array left - its %u triggers go with it",
                static_cast<unsigned>(level_editor_triggers_state().specs_.size()));
    }
    get_forget();
}

bool array_loaded(level_nodes::NodeManager* manager, uint32_t name)
{
    for (uint32_t i = 0; i < manager->num() && i < static_cast<uint32_t>(level_nodes::NodeManager::kMaxArrays); ++i) {
        if (core::at(manager->arrays(), i).label_ == name) {
            return true;
        }
    }
    return false;
}

} // namespace internal
} // namespace frontend::level_editor::triggers
