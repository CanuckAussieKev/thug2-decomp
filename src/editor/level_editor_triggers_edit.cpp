// NOT ORIGINAL - the level editor's trigger edits: the undo snapshot and what restores it, applying a layout's
// triggers, breakables and proximity nodes (ours). See level_editor_triggers.h.

#include "editor/level_editor_triggers.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <new>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include "editor/level_editor.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/sector.h"
#include "engine/objects/proximity.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"
#include <gsl/pointers>
#include <array>
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_triggers_internal.h"

namespace frontend::level_editor::triggers {
// What the source file's anonymous namespace held that its pieces share (level_editor_triggers_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::triggers

namespace frontend::level_editor::triggers {
namespace {

constexpr std::size_t kStatusTextSize = 160;

constexpr float kBoostWidth = 250.0F;
constexpr float kBoostHeight = 60.0F;

} // namespace

// --- undo ---

State store_away()
{
    State state;
    state.specs = editor_trigger_set().specs_;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (spec.owned && spec_index(spec.name) >= 0) {
            state.nodes.push_back(spec);
        }
    }
    if (!level_trigger_set().switched_.empty()) {
        state.walls = walls::store_away(level_trigger_set().switched_);
    }
    state.proxims = stored_proxims();
    return state;
}

void get_restore(const State& state)
{
    // The trigger nodes there now that the state hasn't: removed; those it has: made, or put where it had them.
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (!spec.owned || spec_index(spec.name) < 0) {
            continue;
        }
        const bool keep = std::ranges::any_of(
            state.nodes, [&](const node_create::Spec& state_node) { return same_text(state_node.name, spec.name); });
        if (!keep) {
            node_create::Spec removed;
            int at = 0;
            node_create::drop_out(hash_of_name(spec.name), &removed, &at);
        }
    }
    editor_trigger_set().specs_ = state.specs;
    std::string message;
    for (const node_create::Spec& node : state.nodes) {
        const uint32_t name = hash_of_name(node.name);
        if (!node_create::is_added(name)) {
            node_create::fabricate(node, -1, &message);
        } else {
            node_edit::Values values;
            if (node_edit::get_take_in(name, &values) &&
                (values.pos.x != node.pos.x || values.pos.y != node.pos.y || values.pos.z != node.pos.z ||
                 values.angles.y != node.angles.y)) {
                values.pos = node.pos;
                values.angles = node.angles;
                node_edit::assign_value(name, values);
            }
        }
    }
    if (!state.walls.empty()) {
        walls::apply_states(state.walls);
    }
    stored_proxims() = state.proxims;
    trigger_edit().has_undo_ = false;
    get_sync();
}

bool take_undo(State* before)
{
    if (!trigger_edit().has_undo_) {
        return false;
    }
    trigger_edit().has_undo_ = false;
    *before = std::move(trigger_edit().undo_before_);
    trigger_edit().undo_before_ = State{};
    return true;
}

int take_select()
{
    const int select = trigger_edit().select_;
    trigger_edit().select_ = -1;
    return select;
}

uint32_t take_wall_select()
{
    const uint32_t wall = trigger_focus().select_wall_;
    trigger_focus().select_wall_ = 0;
    return wall;
}

// --- the layout ---

std::vector<Placed> capture_added()
{
    std::vector<Placed> out;
    for (const Spec& spec : editor_trigger_set().specs_) {
        Placed placed;
        placed.spec = spec;
        if (!node_alive(spec.name) || !node_place(spec.name, &placed.pos, &placed.yaw)) {
            continue;
        }
        V3 pos{};
        V3 size{};
        if (spec.kind == Kind::kBreak && break_place(spec, {.pos = &pos, .size = &size})) {
            // (a breakable's box is its piece's where it is now - what the bake makes)
            placed.pos = pos;
            placed.yaw = 0.0F;
            placed.spec.size = size;
        }
        out.push_back(placed);
    }
    return out;
}

int get_apply(const std::vector<Placed>& placed, std::string* message)
{
    note_level();
    if (!level_trigger_set().scanned_) {
        scan_level();
    }
    int made = 0;
    for (const Placed& placed_trigger : placed) {
        if (spec_index(placed_trigger.spec.name) >= 0 || node_create::name_taken(placed_trigger.spec.name)) {
            get_log("level editor: the layout's trigger %s is there already - skipped",
                    placed_trigger.spec.name.c_str());
            continue;
        }
        if (!node_create::fabricate(node_spec(placed_trigger.spec, placed_trigger.pos, placed_trigger.yaw), -1,
                                    message)) {
            continue;
        }
        editor_trigger_set().specs_.push_back(placed_trigger.spec);
        ++made;
    }
    get_sync();
    return made;
}

void reset_all()
{
    for (const Spec& spec : std::vector<Spec>(editor_trigger_set().specs_)) {
        node_create::Spec removed;
        int at = 0;
        node_create::drop_out(hash_of_name(spec.name), &removed, &at);
    }
    editor_trigger_set().specs_.clear();
    level_trigger_set().switched_.clear();
    trigger_focus().inspected_ = 0;
    trigger_edit().has_undo_ = false;
    stored_proxims().clear(); // (sync_proxims puts their nodes back)
    get_sync();
}

std::vector<ProximEdit> capture_proxims()
{
    std::vector<ProximEdit> out;
    for (const ProximEdit& stored_proxim : stored_proxims()) {
        if (proxim_edited(stored_proxim)) {
            out.push_back(stored_proxim);
        }
    }
    return out;
}

int apply_proxims(const std::vector<ProximEdit>& edits)
{
    note_level();
    int found = 0;
    for (const ProximEdit& edit : edits) {
        const int index = proxim_index(edit.node);
        if (index >= 0) {
            core::at(stored_proxims(), index) = edit;
        } else {
            stored_proxims().push_back(edit);
        }
        found += have_proxim_manager() && objects::proxim_get_node(edit.node) ? 1 : 0;
    }
    sync_proxims();
    return found;
}

// --- the feature map's game side ---

Calls script_calls(uint32_t script) { return (script != 0U) ? calls_of(script) : Calls{}; }

void add_boost()
{
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kBoost;
    placed.spec.size = {.x = kBoostWidth, .y = kBoostHeight, .z = kBoostWidth};
    placed.spec.strength = kDefaultBoostStrength;
    placed.pos = place_ahead(placed.spec.size, 0.0F);
    placed.yaw = trigger_edit().yaw_;
    fabricate({placed}, "a jump boost");
}

void add_camera()
{
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kViewCam;
    placed.spec.seconds = kDefaultCameraShotSeconds;
    placed.pos = place_ahead(placed.spec.size, 0.0F);
    placed.yaw = trigger_edit().yaw_;
    // (the nearest of the editor's cameras - another is chosen in the Inspector)
    float best = 0.0F;
    for (const cameras::ViewCam& camera : cameras::get_all()) {
        const V3 delta = camera.pos - placed.pos;
        const float distance = (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
        if (placed.spec.target.empty() || distance < best) {
            best = distance;
            placed.spec.target = camera.name;
        }
    }
    const std::string what =
        placed.spec.target.empty() ? std::string("a camera trigger") : "a camera trigger (" + placed.spec.target + ")";
    fabricate({placed}, what.c_str());
}

const Added* breakable_of(uint32_t sector)
{
    for (const Added& added : editor_trigger_set().added_) {
        if (added.alive && added.spec.kind == Kind::kBreak && checksum_of(added.spec.target) == sector) {
            return &added;
        }
    }
    return nullptr;
}

void set_breakable(uint32_t sector, bool on)
{
    const Added* there = breakable_of(sector);
    if (on == (there != nullptr)) {
        return;
    }
    if (on) {
        Placed placed;
        placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
        placed.spec.kind = Kind::kBreak;
        placed.spec.target = name_for(sector, script::kept_qb_name(sector));
        placed.spec.sound = break_sounds().front();
        placed.spec.replaces =
            sector; // (its collision off while it's breakable: the skater goes into it, as into glass)
        if (!break_place(placed.spec, {.pos = &placed.pos, .size = &placed.spec.size})) {
            hud::get_message("Piece has no bounds", hud::kShortMessageSeconds);
            return;
        }
        fabricate({placed}, "a breakable");
        return;
    }
    // (off: its trigger removed - its collision comes back with it - and the piece shown if a test run broke it)
    note_undo();
    const std::string name = there->spec.name;
    node_create::Spec removed;
    int at = 0;
    node_create::drop_out(hash_of_name(name), &removed, &at);
    const int index = spec_index(name);
    if (index >= 0) {
        editor_trigger_set().specs_.erase(editor_trigger_set().specs_.begin() + index);
    }
    if (render::WorldCell* piece = scenery::sector_of(sector);
        piece && !is_sector_enabled(sector) && !scenery::get_hidden(sector)) {
        piece->change_live(true);
    }
    get_sync();
    hud::get_message("No longer breakable", hud::kShortMessageSeconds);
}

const ProximEdit* proxim_of(uint32_t node)
{
    const int index = proxim_index(node);
    return index >= 0 ? &core::at(stored_proxims(), index) : nullptr;
}

void set_proxim(const ProximEdit& edit, bool undo)
{
    if (undo) {
        note_undo();
    }
    ProximEdit proxim_edit = edit;
    proxim_edit.radius = std::max(kProximMin, std::min(kProximMax, proxim_edit.radius));
    proxim_edit.size = {.x = std::max(kProximMin, std::min(kProximMax, proxim_edit.size.x)),
                        .y = std::max(kProximMin, std::min(kProximMax, proxim_edit.size.y)),
                        .z = std::max(kProximMin, std::min(kProximMax, proxim_edit.size.z))};
    const int index = proxim_index(proxim_edit.node);
    if (!proxim_edited(proxim_edit)) {
        if (index >= 0) {
            stored_proxims().erase(stored_proxims().begin() + index);
        }
    } else if (index >= 0) {
        core::at(stored_proxims(), index) = proxim_edit;
    } else {
        stored_proxims().push_back(proxim_edit);
    }
    sync_proxims();
}

bool is_proxim(uint32_t node)
{
    return (node != 0U) && have_proxim_manager() && objects::proxim_get_node(node) != nullptr;
}

bool has_handles(uint32_t node) { return is_trigger(node) || is_proxim(node); }

std::string describe()
{
    int alive = 0;
    for (const Added& added : editor_trigger_set().added_) {
        alive += added.alive ? 1 : 0;
    }
    int off = 0;
    for (const LevelTrigger& level_trigger : level_trigger_set().level_) {
        off += level_trigger.off ? 1 : 0;
    }
    std::array<char, kStatusTextSize> text{};
    text::format_to(text, "%u level trigger volumes (%d off), %d added", static_cast<unsigned>(level_trigger_set().level_.size()), off,
                    alive);
    return text.data();
}

std::string describe_level_trigger(const LevelTrigger& level_trigger)
{
    return describe(level_trigger.calls, level_trigger.kind);
}

} // namespace frontend::level_editor::triggers
