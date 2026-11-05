// NOT ORIGINAL - the level editor's chains put into the level: rebuilding the rails, and making the level match a
// saved state point by point (ours). See level_editor_chains.h.

#include "editor/level_editor_chains.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <utility>
#include "editor/level_editor.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_scenery.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "game/world/trick_objects.h"
#include "game/commands/game_cfuncs.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "platform/run_log.h"
#include "game/skate/skate_module.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/maths/vec4.h"
#include "editor/level_editor_chains_internal.h"

namespace frontend::level_editor::chains {
// What the source file's anonymous namespace held that its pieces share (level_editor_chains_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::chains

namespace frontend::level_editor::chains {

void rebuild_rails()
{
    objects::GrindRailHub* rails = get_rails();
    if (!rails) {
        return;
    }
    const std::span<objects::GrindPoint> old(rails->nodes(), rails->nodes() ? static_cast<size_t>(rails->num()) : 0);
    const int old_count = static_cast<int>(old.size());
    // The nodes: the ones it has whose node is still there (a dropped one too - kept switched off, so the editor's
    // moves of it stay), then the overlay's new rail points.
    std::vector<int> kept;
    for (int i = 0; i < old_count; ++i) {
        if (look_up_node_params(core::at(old, i).label())) {
            kept.push_back(i);
        }
    }
    std::vector<std::pair<uint32_t, script::Struct*>> added;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        const uint32_t name = name_hash(spec.name);
        script::Struct* node = look_up_node_params(name);
        const uint32_t cls = class_of(node);
        if (!node || (cls != rail_node_class() && cls != climbing_node_class())) {
            continue;
        }
        bool there = false;
        for (const int index : kept) {
            there = there || core::at(old, index).label() == name;
        }
        if (!there) {
            added.emplace_back(name, node);
        }
    }
    const int count = static_cast<int>(kept.size() + added.size());
    objects::GrindPoint* nodes_block = nullptr;
    if (count != 0) {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->bottom_up_heap());
        nodes_block = static_cast<objects::GrindPoint*>(memory::alloc_block(count * sizeof(objects::GrindPoint)));
        memory::Hub::get_pull()->leave_heap_scope();
        if (!nodes_block) {
            get_log("level editor: no memory for the rail manager's nodes - rails not rebuilt");
            return;
        }
    }
    const std::span<objects::GrindPoint> nodes(nodes_block, static_cast<size_t>(count));
    for (size_t i = 0; i < kept.size(); ++i) {
        core::at(nodes, i) = core::at(old, core::at(kept, i));
    }
    rails->set_nodes(nodes_block);
    rails->set_num(count);
    for (size_t i = 0; i < added.size(); ++i) {
        rails->set_adding(static_cast<int>(kept.size() + i));
        rails->append_vertex(core::at(added, i).first, core::at(added, i).second); // (the game's own: flags, place, terrain)
    }
    rails->set_adding(count);
    // On / off: dropped points off (their state noted first), shown ones back as they were.
    std::unordered_map<uint32_t, int> index;
    set_dropped_states(nodes, index);
    // The links, as add_from_node_array makes them: a node's next is its first link (or a DefaultLine one), the linked
    // node's previous likewise. Dropped points take no part.
    link_rail_nodes(nodes, index);
    for (int i = 0; i < count; ++i) {
        cuboid(&core::at(nodes, i));
    }
    // An added point's terrain from its node (it is made again when its terrain changes, but its rail manager node was
    // kept); terrains the editor set on the level's points (copied nodes keep theirs).
    for (size_t i = 0; i < kept.size(); ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        uint32_t terrain = 0;
        script::Struct* params = node_create::is_added(node->label()) ? look_up_node_params(node->label()) : nullptr;
        if (params && ((node->flag_bits() & objects::kRailFlagClimbing) == 0U) &&
            params->read_hash_value(kSurfaceKindKey, &terrain, 0) && known_terrain(terrain)) {
            node->set_surface_type(surface_kind::get_terrain_index(terrain));
        }
    }
    for (const auto& entry : terrains_state()) {
        auto it = index.find(entry.first);
        uint8_t value = 0;
        if (it != index.end() && terrain_byte(entry.second.terrain, &value)) {
            core::at(nodes, it->second).set_surface_type(value);
        }
    }
    // Ladders: an added point's orientation from its node's Angles (turned since it was made), a level point's as the
    // editor turned it.
    for (int i = 0; i < count; ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        if ((node->flag_bits() & objects::kRailFlagClimbing) == 0U) {
            continue;
        }
        if (node_create::is_added(node->label())) {
            maths::Vec4 angles;
            level_nodes::read_turn_angles(look_up_node_params(node->label()), &angles);
            node->set_climb(climb_of(angles.y));
        } else if (facings_state().contains(node->label())) {
            node->set_climb(climb_of(facings_state()[node->label()].facing));
        }
    }
    if (!old.empty()) {
        level_editor_chains_state().retired_.push_back(old.data()); // (a skater's grind may point into it: freed with the level)
    }
    level_editor_chains_state().built_ = nodes_block;
    register_trick_objects();
    ++flat_chains().flat_generation_;
    scenery::rails_rebuilt();
    nodes::mark_stale();
    if (debug_enabled()) {
        get_log("level editor: rail manager built again - %d nodes (%u kept, %u new)", count,
                static_cast<unsigned>(kept.size()), static_cast<unsigned>(added.size()));
    }
}

State at_hand()
{
    State state;
    if (!in_level()) {
        return state;
    }
    state.points = added_points();
    for (const auto& entry : original_links()) {
        script::Struct* node = look_up_node_params(entry.first);
        if (node != entry.second.node) {
            continue;
        }
        const std::vector<uint32_t> now = own_links(node);
        if (now != entry.second.links) {
            state.links.push_back({.name = entry.first, .links = now});
        }
    }
    state.dropped = stored_dropped();
    std::ranges::sort(state.dropped);
    for (const auto& entry : terrains_state()) {
        state.terrains.push_back({.name = entry.first, .terrain = entry.second.terrain});
    }
    for (const auto& entry : facings_state()) {
        state.facings.push_back({.name = entry.first, .facing = entry.second.facing});
    }
    state.grind_flat_ = level_flat();
    std::ranges::sort(state.grind_flat_);
    for (const auto& entry : level_speeds()) {
        state.speeds.push_back({.name = entry.first, .rate = entry.second}); // (a map: sorted by name)
    }
    return state;
}

namespace {

// (the points not wanted, or wanted different - other than place and links - removed)
void remove_changed_points(const std::vector<Point>& points, const State& target)
{
    for (const Point& chain_point : points) { // (change_phase's own copy of the points before)
        const Point* want = nullptr;
        for (const Point& target_point : target.points) {
            want = same_text(target_point.name, chain_point.name.c_str()) ? &target_point : want;
        }
        if (!want || want->cls != chain_point.cls || want->like != chain_point.like ||
            want->terrain != chain_point.terrain || !same_text(want->type, chain_point.type.c_str()) ||
            want->hang != chain_point.hang) {
            node_create::Spec removed;
            int at = 0;
            node_create::drop_out(name_hash(chain_point.name), &removed, &at);
        }
    }
}

// (the missing points made: one re-registration)
void make_missing_points(const State& target)
{
    std::vector<node_create::Spec> make;
    const std::vector<Point> remaining = added_points();
    for (const Point& target_point : target.points) {
        bool there = false;
        for (const Point& remaining_point : remaining) {
            there = there || same_text(target_point.name, remaining_point.name.c_str());
        }
        if (!there) {
            make.push_back(spec_of(target_point));
        }
    }
    if (!make.empty()) {
        std::string message;
        node_create::create_all(make, &message);
        if (!message.empty()) {
            get_log("level editor: %s", message.c_str());
        }
    }
}

// (the points' links and places - a ladder's facing)
void set_point_places(const State& target)
{
    for (const Point& target_point : target.points) {
        const uint32_t name = name_hash(target_point.name);
        node_create::set_links(name, target_point.links);
        if (script::Struct* node = look_up_node_params(name)) {
            maths::Vec4 pos;
            level_nodes::fetch_place(node, &pos);
            if (pos.x != target_point.pos.x || pos.y != target_point.pos.y || pos.z != target_point.pos.z) {
                write_pos(node, target_point.pos);
            }
            maths::Vec4 angles;
            level_nodes::read_turn_angles(node, &angles);
            if (same_text(target_point.cls, "ClimbingNode") &&
                (angles.x != 0.0F || angles.y != target_point.facing || angles.z != 0.0F)) {
                write_angles(node, {.x = 0.0F,
                                    .y = target_point.facing,
                                    .z = 0.0F}); // (a ladder turned: rebuild_rails turns its rail node)
            }
        }
    }
}

// (the points' cluster, grind-flat and speed-up)
void set_point_extras(const State& target)
{
    extras_state().clear();
    for (const Point& target_point : target.points) {
        if ((target_point.cluster != 0U) || target_point.grind_flat_ || target_point.speed > 0.0F) {
            extras_state()[name_hash(target_point.name)] = {
                .cluster = target_point.cluster,
                .grind_flat_ = target_point.grind_flat_,
                .speed = target_point.speed > 0.0F ? clamp_speed(target_point.speed) : 0.0F};
        }
    }
}

// (the level rails' speed-ups)
void set_level_speeds(const State& target)
{
    level_speeds().clear();
    for (const SpeedEdit& speed_edit : target.speeds) {
        if (!node_create::is_added(speed_edit.name) && look_up_node_params(speed_edit.name)) {
            level_speeds()[speed_edit.name] = clamp_speed(speed_edit.rate);
        }
    }
}

void set_level_facings(const State& target)
{
    // Level ladders turned (the rail manager's orientation as the level had it kept to put back), level rails marked
    // grindable on flat ground.
    for (auto it = facings_state().begin(); it != facings_state().end();) {
        bool wanted = false;
        for (const FacingEdit& facing_edit : target.facings) {
            wanted = wanted || facing_edit.name == it->first;
        }
        if (!wanted) {
            if (objects::GrindPoint* node = rail_node_named(it->first)) {
                node->set_climb(it->second.original);
            }
            it = facings_state().erase(it);
        } else {
            ++it;
        }
    }
    for (const FacingEdit& facing_edit : target.facings) {
        const objects::GrindPoint* node = rail_node_named(facing_edit.name);
        if (!node || ((node->flag_bits() & objects::kRailFlagClimbing) == 0U) ||
            node_create::is_added(facing_edit.name)) {
            continue;
        }
        if (!facings_state().contains(facing_edit.name)) {
            facings_state()[facing_edit.name] = {.facing = facing_edit.facing, .original = node->climb()};
        }
        facings_state()[facing_edit.name].facing = facing_edit.facing;
    }
}

// (the level rails grindable on flat ground)
void set_level_flat(const State& target)
{
    level_flat().clear();
    for (const uint32_t name : target.grind_flat_) {
        if (!node_create::is_added(name) && look_up_node_params(name)) {
            level_flat().push_back(name);
        }
    }
}

std::vector<uint32_t> set_level_link_edits(const State& target)
{
    // The level's links: as the target has them, else as the level had them.
    std::set<uint32_t> names;
    for (const auto& entry : original_links()) {
        names.insert(entry.first);
    }
    for (const LinkEdit& link_edit : target.links) {
        names.insert(link_edit.name);
    }
    std::vector<uint32_t> paths_changed;
    for (const uint32_t name : names) {
        std::vector<uint32_t> want = level_links_original(name);
        for (const LinkEdit& link_edit : target.links) {
            if (link_edit.name == name) {
                want = link_edit.links;
            }
        }
        if (level_links_now(name) != want) {
            const uint32_t cls = class_of(look_up_node_params(name));
            if (cls == pedestrian_class() || cls == vehicle_class()) {
                paths_changed.push_back(name);
            }
        }
        set_level_links(name, want);
    }
    return paths_changed;
}

void set_dropped(const State& target)
{
    // Dropped points: hidden with phase 3's marker (shown again when no longer dropped).
    std::vector<uint32_t> dropped = target.dropped;
    for (const uint32_t name : std::vector<uint32_t>(stored_dropped())) {
        if (std::ranges::find(dropped, name) == dropped.end()) {
            node_create::conceal(name, false);
        }
    }
    std::vector<uint32_t> kept;
    for (const uint32_t name : dropped) {
        if (node_create::is_added(name) || !look_up_node_params(name)) {
            continue;
        }
        if (node_create::get_hidden(name) || node_create::conceal(name, true)) {
            kept.push_back(name);
        }
    }
    stored_dropped() = kept;
}

void set_level_terrains(const State& target)
{
    // Terrains (level points): put back where no longer set.
    for (auto it = terrains_state().begin(); it != terrains_state().end();) {
        bool wanted = false;
        for (const TerrainEdit& terrain_edit : target.terrains) {
            wanted = wanted || terrain_edit.name == it->first;
        }
        if (!wanted) {
            if (objects::GrindPoint* node = rail_node_named(it->first)) {
                node->set_surface_type(it->second.original);
            }
            it = terrains_state().erase(it);
        } else {
            ++it;
        }
    }
    for (const TerrainEdit& terrain_edit : target.terrains) {
        const objects::GrindPoint* node = rail_node_named(terrain_edit.name);
        uint8_t value = 0;
        if (!node || !terrain_byte(terrain_edit.terrain, &value)) {
            continue;
        }
        if (!terrains_state().contains(terrain_edit.name)) {
            terrains_state()[terrain_edit.name] = {.terrain = terrain_edit.terrain, .original = node->surface_type()};
        }
        terrains_state()[terrain_edit.name].terrain = terrain_edit.terrain;
    }
}

} // namespace

bool change_phase(const State& target)
{
    if (!in_level()) {
        return false;
    }
    const State before = at_hand();
    if (same_state(before, target)) {
        return true; // (nothing to do: the rail manager isn't built again)
    }
    // Points: those not wanted (or wanted different, other than place and links) removed; the missing made (one
    // re-registration); places and links set.
    remove_changed_points(before.points, target);
    make_missing_points(target);
    set_point_places(target);
    set_point_extras(target);
    set_level_speeds(target);
    set_level_facings(target);
    set_level_flat(target);
    const std::vector<uint32_t> paths_changed = set_level_link_edits(target);
    set_dropped(target);
    set_level_terrains(target);
    rebuild_rails();
    // The added points' places in the rail manager (a point made before keeps its node there).
    for (const Point& target_point : target.points) {
        move_added_point(name_hash(target_point.name), target_point.pos);
    }
    for (const uint32_t name : paths_changed) {
        get_remake(name);
    }
    apply_speeds();
    nodes::mark_stale();
    return true;
}

void forget_level()
{
    for (const auto& entry : original_links()) {
        script::Struct* node = look_up_node_params(entry.first);
        if (node && node == entry.second.node) {
            set_own_links(node, entry.second.links);
        }
    }
    original_links().clear();
    stored_dropped().clear();
    drop_active().clear();
    terrains_state().clear();
    level_editor_chains_state().remake_.clear();
    level_editor_chains_state().draw_ = Blit::kNone;
    level_editor_chains_state().draw_last_ = 0;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    level_editor_chains_state().ladder_have_foot_ = false;
    // (the rail manager and the trick object manager are the level's: they go with it)
    extras_state().clear();
    facings_state().clear();
    level_flat().clear();
    trick_state().clear();
    ++flat_chains().flat_generation_;
    // (the speed-ups' TriggerScripts: the level's nodes put back while its array is the one loaded)
    for (const auto& entry : speed_applied()) {
        script::Struct* node = look_up_node_params(entry.first);
        if (node && node == entry.second.node) {
            fields::put_in(node, kTriggerScriptKey, false, entry.second.original);
        }
    }
    speed_applied().clear();
    level_speeds().clear();
    level_speed_cache() = LevelSpeedCache{};
}

namespace internal {

// The rail manager's nodes whose segment to the next is grindable on flat ground: an added point marked so, a level
// point marked so, or a node carrying the flag ed_grind_flat (a level baked by apply_layout.ts).
void refresh_flat(objects::GrindRailHub* rails)
{
    if (flat_chains().flat_cache_.nodes == rails->nodes() && flat_chains().flat_cache_.count == rails->num() &&
        flat_chains().flat_cache_.generation == flat_chains().flat_generation_) {
        return;
    }
    flat_chains().flat_cache_.nodes = rails->nodes();
    flat_chains().flat_cache_.count = rails->num();
    flat_chains().flat_cache_.generation = flat_chains().flat_generation_;
    flat_chains().flat_cache_.marked.clear();
    if (!rails->nodes() || !level_nodes::node_manager_instance()) {
        return;
    }
    static const uint32_t kFlag = name_hash("ed_grind_flat");
    for (int i = 0; i < rails->num(); ++i) {
        const objects::GrindPoint* node = &core::at(rails->node_run(), i);
        if (((node->flag_bits() & objects::kRailFlagClimbing) != 0U) || !node->succ()) {
            continue;
        }
        auto extras = extras_state().find(node->label());
        bool marked = (extras != extras_state().end() && extras->second.grind_flat_) ||
                      std::ranges::find(level_flat(), node->label()) != level_flat().end();
        if (!marked) {
            script::Struct* params = look_up_node_params(node->label());
            marked = (params != nullptr) && params->has_bare_flag(kFlag);
        }
        if (marked) {
            flat_chains().flat_cache_.marked.push_back(node);
        }
    }
    if (debug_enabled() && !flat_chains().flat_cache_.marked.empty()) {
        get_log("level editor: %u rail segments grindable on flat ground",
                static_cast<unsigned>(flat_chains().flat_cache_.marked.size()));
    }
}

bool flat_hook(const maths::Vec4* a, const maths::Vec4* b)
{
    return flat_grindable({.x = a->x, .y = a->y, .z = a->z}, {.x = b->x, .y = b->y, .z = b->z});
}

} // namespace internal
} // namespace frontend::level_editor::chains
