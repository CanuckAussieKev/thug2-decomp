// NOT ORIGINAL - the level editor's trigger volumes, kill zones, teleports and gaps (ours). See
// level_editor_triggers.h. This file holds the state, the hooks the scripts reach, adding triggers and the per-frame
// tick; the pieces (triggers_records / triggers_sync / triggers_draw / triggers_edit) are level_editor_triggers_<name>.cpp, sharing level_editor_triggers_internal.h.

#include "editor/level_editor_triggers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <new>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include "base/core/enum_ops.h"
#include "editor/level_editor.h"
#include "engine/render/coll_static.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/sector.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_world.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include <gsl/pointers>
#include <array>
#include "base/core/parse_number.h"
#include "base/core/down_cast.h"
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

constexpr std::size_t kGapNameSize = 32;

constexpr float kMinPieceTriggerSize = 20.0F; // the least a trigger copied from a piece box measures

constexpr float kRailAccelerateRate = 0.05F; // THAW default

// Placing a new trigger.
constexpr float kGroundProbeDepth = 5000.0F;
constexpr float kPlaceAheadReach = 4000.0F;   // how far ahead of the camera the trigger is looked for
constexpr float kPlaceAheadFallback = 600.0F; // and where it goes if nothing is hit
constexpr float kPlaceAboveGround = 200.0F;   // the start of the ground probe above the placement
constexpr float kWaterKillWidth = 800.0F;     // the water kill zone width and depth
constexpr float kTeleportSize = 200.0F;
constexpr float kScriptTriggerWidth = 300.0F;
constexpr float kScriptTriggerHeight = 200.0F;
constexpr float kGapGateWidth = 300.0F; // a gap start and end gates
constexpr float kGapGateHeight = 300.0F;
constexpr float kGapGateDepth = 60.0F;
constexpr float kGapGateSpacing = 600.0F; // the end gate is put this far ahead of the start

} // namespace
namespace internal {

EditorTriggerSet& editor_trigger_set()
{
    static core::NoDestroy<EditorTriggerSet> triggers;
    return triggers.get();
}

LevelTriggerSet& level_trigger_set()
{
    static core::NoDestroy<LevelTriggerSet> triggers;
    return triggers.get();
}

TriggerFocus& trigger_focus()
{
    static core::NoDestroy<TriggerFocus> focus;
    return focus.get();
}

TriggerEdit& trigger_edit()
{
    static core::NoDestroy<TriggerEdit> state;
    return state.get();
}

uint32_t hash_of_name(const std::string& name) { return node_create::name_hash(name); }

std::string get_label_of(uint32_t checksum) { return nodes::caption(checksum); }

// The overlay's trigger nodes now.
bool node_alive(const std::string& name) { return node_create::is_added(hash_of_name(name)); }

bool node_place(const std::string& name, V3* pos, float* yaw)
{
    script::Struct* node =
        level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(hash_of_name(name), false) : nullptr;
    if (!node) {
        return false;
    }
    maths::Vec4 point = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 point_a = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    level_nodes::fetch_place(node, &point);
    level_nodes::read_turn_angles(node, &point_a);
    *pos = {.x = point.x, .y = point.y, .z = point.z};
    *yaw = point_a.y;
    return true;
}

} // namespace internal
namespace {

// --- what a script calls ---

void on_kill(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->kill = true;
    uint32_t target = 0;
    if (params && params->read_hash_value(current_node_name(), &target, 0) && (target != 0U)) {
        trigger_edit().calls_->target = target;
    }
}
void on_water(script::Struct* params, const uint8_t* call)
{
    on_kill(params, call);
    trigger_edit().calls_->water = true;
}
void on_teleport(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->teleport = true;
    uint32_t target = 0;
    if (params && params->read_hash_value(current_node_name(), &target, 0) && (target != 0U)) {
        trigger_edit().calls_->target = target;
    }
}
void on_start_gap(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->start_gap_ = true;
    uint32_t id = 0;
    if (params && params->read_hash_value(get_gap_id(), &id, 0) && (trigger_edit().calls_->gap_id_ == 0U)) {
        trigger_edit().calls_->gap_id_ = id;
    }
}
void on_end_gap(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->end_gap_ = true;
    uint32_t id = 0;
    if (params && params->read_hash_value(get_gap_id(), &id, 0)) {
        trigger_edit().calls_->gap_id_ = id;
    }
    const char* text = nullptr;
    if (params && params->fetch_text(get_text(), &text, 0) && text) {
        trigger_edit().calls_->gap_text_ = text;
    }
    int score = 0;
    if (params && params->fetch_int_value(get_score(), &score, 0)) {
        trigger_edit().calls_->gap_score_ = score;
    }
}

void on_boost(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->boost = true;
    int speed = 0;
    float value = 0.0F;
    if (params && params->fetch_int_value(get_checksums("speed"), &speed, 0)) {
        trigger_edit().calls_->boost_speed_ = static_cast<float>(speed);
    } else if (params && params->fetch_float(get_checksums("speed"), &value, 0)) {
        trigger_edit().calls_->boost_speed_ = value;
    }
    if (params && params->fetch_float(get_checksums("multi"), &value, 0)) {
        trigger_edit().calls_->boost_multi_ = value;
    }
}
void on_shatter(script::Struct* /*params*/, const uint8_t* /*unused*/) { trigger_edit().calls_->shatter = true; }
void on_camera(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->camera = true;
    uint32_t node = 0;
    if (params && params->read_hash_value(get_checksums("camera"), &node, 0)) {
        trigger_edit().calls_->camera_node_ = node;
    }
}
void on_rail_accelerate(script::Struct* params, const uint8_t* /*unused*/)
{
    trigger_edit().calls_->rail_speed_ = true;
    trigger_edit().calls_->rail_rate_ = kRailAccelerateRate; // (THAW's default)
    float rate = 0.0F;
    if (params && params->fetch_float(get_checksums("rate"), &rate, 0)) {
        trigger_edit().calls_->rail_rate_ = rate;
    }
}

} // namespace
namespace internal {

// What a script calls, walked once a level (a trigger that runs a script: shown every frame).
std::vector<std::pair<uint32_t, Calls>>& calls_cache()
{
    static core::NoDestroy<std::vector<std::pair<uint32_t, Calls>>> calls_cache;
    return calls_cache.get();
}
const Calls& calls_of(uint32_t script)
{
    for (const auto& call : calls_cache()) {
        if (call.first == script) {
            return call.second;
        }
    }
    calls_cache().emplace_back(script, get_walk(script));
    return calls_cache().back().second;
}

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = (name != 0U) ? script::resolve_symbol(name) : nullptr;
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

Calls get_walk(uint32_t script)
{
    Calls calls;
    if (!is_script(script)) {
        return calls;
    }
    trigger_edit().calls_ = &calls;
    static const auto kKills = std::to_array<uint32_t>({kill_skater(), get_checksums("SK3_KillSkater_Pungee"),
                                      get_checksums("SK3_Killskater_Finish"), get_checksums("KillSkater")});
    script::clear_script_flag_6f_2564(); // (as for_each_node_script_call walks them)
    for (const uint32_t target : kKills) {
        level_nodes::for_each_script_call({.script = script, .target = target}, &on_kill, nullptr, 0);
    }
    level_nodes::for_each_script_call({.script = script, .target = kill_skater_water()}, &on_water, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_teleport()}, &on_teleport, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = start_gap()}, &on_start_gap, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = end_gap()}, &on_end_gap, nullptr, 0);
    // (the feature map: jump boosts, breakables, THAW's rail speed-ups - and the editor's own)
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("trigger_jump_boost")}, &on_boost, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("Shatter")}, &on_shatter, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("ShatterAndDie")}, &on_shatter, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("RailAccelerate")}, &on_rail_accelerate, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("ed_RailSpeed")}, &on_rail_accelerate, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("LevelEditor_PlayCamera")}, &on_camera, nullptr,
                                      0); // (map4)
    script::set_script_flag_6f_2564();
    trigger_edit().calls_ = nullptr;
    return calls;
}

// The level's node array noted (the triggers belong to it): another array added after it (a cutscene's) doesn't
// change it; the level's gone and another there forgets them.
void note_level()
{
    node_create::set_array_removed_listener(&on_array_removed);
    if (level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get()) {
        if ((level_trigger_set().level_array_ != 0U) &&
            array_loaded(manager, level_trigger_set().level_array_)) {
            return;
        }
        const uint32_t level = manager->get_last_node_array_name();
        if ((level != 0U) && level != level_trigger_set().level_array_) {
            if (level_trigger_set().level_array_ != 0U) {
                get_forget();
            }
            level_trigger_set().level_array_ = level;
        }
    }
}

void note_undo()
{
    if (!trigger_edit().has_undo_) {
        trigger_edit().undo_before_ = store_away();
        trigger_edit().has_undo_ = true;
    }
}

// A node for a spec at `pos` / `yaw` in the overlay (false: `message`).
node_create::Spec node_spec(const Spec& spec, V3 pos, float yaw)
{
    node_create::Spec overlay_spec;
    overlay_spec.name = spec.name;
    overlay_spec.cls = "GenericNode";
    overlay_spec.type = "ed_trigger";
    overlay_spec.pos = pos;
    overlay_spec.angles = {.x = 0.0F, .y = yaw, .z = 0.0F};
    node_create::Prop script = {.type = node_create::Prop::kName, .key = "TriggerScript"};
    script.text = get_script_name(spec);
    overlay_spec.extra.push_back(script);
    overlay_spec.owned = true;
    return overlay_spec;
}

bool name_taken(const std::string& name) { return spec_index(name) >= 0 || node_create::name_taken(name); }

} // namespace internal
namespace {

std::string fresh_gap()
{
    return node_create::fresh_name("ed_gap", [](const std::string& id) {
        return std::ranges::any_of(editor_trigger_set().specs_,
                                   [&](const Spec& spec) { return same_text(spec.gap, id); });
    });
}

// The nearest restart (a player or multiplayer one) to `pos` ("" none).
std::string nearest_restart(V3 pos)
{
    float best = 0.0F;
    std::string name;
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.family != nodes::Family::kSpawn || node.hidden || !node.has_pos_ ||
            !core::any(node.restart_types_ & (nodes::SpawnPointKind::kPlayer1 | nodes::SpawnPointKind::kMultiplayer))) {
            continue;
        }
        const V3 delta = node.pos - pos;
        const float distance = get_dot(delta, delta);
        if (name.empty() || distance < best) {
            best = distance;
            name = name_for(node.name, script::kept_qb_name(node.name));
        }
    }
    return name;
}

// The game's collision ray (a ProbeRay: constructor 0x00417760; 0x00417710 casts it from `from` to `to` - thiscall, true
// on a hit, its point at +0x34), static collision only (as the editor's G): trigger faces (0x10) are passed.
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& point = ray->hit().point;
    *hit = {.x = point[0], .y = point[1], .z = point[2]};
    return true;
}
bool ground_below(V3 from, V3* hit) { return get_cast(from, {.x = from.x, .y = from.y - kGroundProbeDepth, .z = from.z}, hit); }

} // namespace
namespace internal {

// Where a new trigger goes: the ground where the view looks (else 600 units ahead), its box standing on it.
V3 place_ahead(V3 size, float ahead)
{
    const V3 forward = make_unit(trigger_edit().forward_);
    V3 at{};
    if (!get_cast(trigger_edit().camera_, trigger_edit().camera_ + forward * kPlaceAheadReach, &at)) {
        at = trigger_edit().camera_ + forward * kPlaceAheadFallback;
    }
    const V3 flat = make_unit({.x = forward.x, .y = 0.0F, .z = forward.z});
    at = at + flat * ahead;
    V3 ground{};
    if (ground_below(at + V3{.x = 0.0F, .y = kPlaceAboveGround, .z = 0.0F}, &ground)) {
        at.y = ground.y;
    }
    at.y += size.y * kHalf;
    return at;
}

// New triggers made (their nodes in the overlay); the first selected. One undo step.
void fabricate(const std::vector<Placed>& made, const char* what)
{
    note_undo();
    std::string message;
    int first = -1;
    for (const Placed& placed : made) {
        if (!node_create::fabricate(node_spec(placed.spec, placed.pos, placed.yaw), -1, &message)) {
            hud::get_message(message, hud::kMessageSeconds);
            break;
        }
        editor_trigger_set().specs_.push_back(placed.spec);
        if (first < 0) {
            first = static_cast<int>(editor_trigger_set().specs_.size()) - 1;
        }
        get_log("level editor: added trigger %s (%s) at (%.1f, %.1f, %.1f)", placed.spec.name.c_str(),
                kind_name(placed.spec.kind), placed.pos.x, placed.pos.y, placed.pos.z);
    }
    get_sync();
    if (first >= 0) {
        trigger_edit().select_ = nodes::get_find(hash_of_name(core::at(editor_trigger_set().specs_, first).name));
        hud::get_message(std::string("Added ") + what, hud::kShortMessageSeconds);
    }
}

} // namespace internal

void get_guard()
{
    sync_proxims(); // (proximity nodes as their edits say - put back in a network session)
    // A breakable broken in play (its piece killed by its own script): its box goes, so it isn't shattered again.
    for (size_t i = 0; i < trigger_edit().boxes_.size();) {
        int at = -1;
        for (size_t k = 0; k < editor_trigger_set().specs_.size(); ++k) {
            if (core::at(editor_trigger_set().specs_, k).kind == Kind::kBreak &&
                hash_of_name(core::at(editor_trigger_set().specs_, k).name) ==
                    core::at(trigger_edit().boxes_, i).name) {
                at = static_cast<int>(k);
            }
        }
        if (at >= 0 && !is_sector_enabled(checksum_of(core::at(editor_trigger_set().specs_, at).target))) {
            free_box(core::at(trigger_edit().boxes_, i).coll);
            trigger_edit().boxes_.erase(trigger_edit().boxes_.begin() + static_cast<std::ptrdiff_t>(i));
            get_log("level editor: breakable %s broken - its box taken out until the piece is back",
                    core::at(editor_trigger_set().specs_, at).name.c_str());
        } else {
            ++i;
        }
    }
    std::string why;
    if (trigger_edit().boxes_.empty() || !layout::get_refused(&why)) {
        return;
    }
    get_log("level editor: the editor's %u trigger volumes taken out (%s)", static_cast<unsigned>(trigger_edit().boxes_.size()),
            why.c_str());
    drop_boxes();
}

const std::vector<LevelTrigger>& level_triggers() { return level_trigger_set().level_; }
const std::vector<Added>& added_triggers() { return editor_trigger_set().added_; }

bool is_trigger(uint32_t name)
{
    return std::ranges::any_of(editor_trigger_set().specs_,
                               [&](const Spec& spec) { return hash_of_name(spec.name) == name; });
}

bool get_shown()
{
    if (!trigger_edit().setting_read_) {
        trigger_edit().setting_read_ = true;
        trigger_edit().shown_ = config_int("editor", "triggers", 1) != 0;
    }
    return trigger_edit().shown_;
}

void set_shown(bool shown)
{
    get_shown();
    trigger_edit().shown_ = shown;
    config_set_int("editor", "triggers", shown ? 1 : 0);
}

uint32_t inspected_sector() { return trigger_focus().inspected_; }
void inspect(uint32_t sector) { trigger_focus().inspected_ = sector; }

void get_queue(std::function<void()> action) { trigger_edit().actions_.push_back(std::move(action)); }

void kickoff()
{
    note_level();
    scan_level();
    trigger_edit().level_gaps_read_ = false;
    // (breakables broken while playing: their pieces back to edit - and to break again)
    for (const Spec& spec : editor_trigger_set().specs_) {
        const uint32_t piece = spec.kind == Kind::kBreak ? checksum_of(spec.target) : 0;
        render::WorldCell* sector = (piece != 0U) ? scenery::sector_of(piece) : nullptr;
        if (sector && node_alive(spec.name) && !is_sector_enabled(piece) && !scenery::get_hidden(piece)) {
            sector->change_live(true);
            get_log("level editor: breakable %s's piece back (broken while playing)", spec.name.c_str());
        }
    }
    get_sync();
}

void halt() { trigger_edit().actions_.clear(); }

void tick(V3 camera, V3 forward, float yaw)
{
    trigger_edit().camera_ = camera;
    trigger_edit().forward_ = forward;
    trigger_edit().yaw_ = yaw;
    note_level();
    if (!level_trigger_set().scanned_) {
        scan_level();
    }
    std::vector<std::function<void()>> actions;
    actions.swap(trigger_edit().actions_);
    for (auto& action : actions) {
        action();
    }
    get_sync();
}

// --- the UI's actions (level_editor_triggers_ui.cpp queues them) ---

void add_kill(bool water)
{
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kKill;
    placed.spec.water = water;
    placed.spec.size = water ? V3{.x = kWaterKillWidth, .y = 100.0F, .z = kWaterKillWidth}
                             : V3{.x = kDefaultTriggerWidth, .y = kDefaultTriggerHeight, .z = kDefaultTriggerDepth};
    placed.pos = place_ahead(placed.spec.size, 0.0F);
    placed.yaw = trigger_edit().yaw_;
    placed.spec.target = nearest_restart(placed.pos);
    fabricate({placed}, water ? "a water kill zone" : "a kill zone");
}

void add_teleport()
{
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kJumpTo;
    placed.spec.size = {.x = kTeleportSize, .y = kTeleportSize, .z = kTeleportSize};
    placed.pos = place_ahead(placed.spec.size, 0.0F);
    placed.yaw = trigger_edit().yaw_;
    placed.spec.target = nearest_restart(placed.pos);
    fabricate({placed}, "a teleport");
}

void append_script()
{
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kScripts;
    placed.spec.size = {.x = kScriptTriggerWidth, .y = kScriptTriggerHeight, .z = kScriptTriggerWidth};
    placed.pos = place_ahead(placed.spec.size, 0.0F);
    placed.yaw = trigger_edit().yaw_;
    fabricate({placed}, "a script trigger");
}

void register_gap_entry()
{
    Placed start;
    Placed end;
    const std::string gap = fresh_gap();
    start.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    start.spec.kind = Kind::kGapStart;
    start.spec.size = {.x = kGapGateWidth, .y = kGapGateHeight, .z = kGapGateDepth};
    start.spec.gap = gap;
    start.spec.flags = {"CANCEL_GROUND"};
    const int number = text::to_int(std::next(gap.c_str(), 6));
    std::array<char, kGapNameSize> text{};
    text::format_to(text, "Editor Gap %d", number > 0 ? number : 1);
    start.spec.text = text.data();
    start.spec.score = kDefaultGapScore;
    start.pos = place_ahead(start.spec.size, 0.0F);
    start.yaw = trigger_edit().yaw_;
    end = start;
    end.spec.name = "";
    end.spec.kind = Kind::kGapEnd;
    // (the end 600 units further along the view, on the ground there)
    end.pos = place_ahead(end.spec.size, kGapGateSpacing);
    // (two fresh names: the first isn't taken until it's made)
    std::string first = start.spec.name;
    end.spec.name = node_create::fresh_name(
        "ed_trigger", [&](const std::string& name) { return same_text(name, first) || name_taken(name); });
    fabricate({start, end}, "a gap");
}

// (UX9: the gap wizard - level_editor_gap_wizard.h) A gap's two ends as given, their names and GapID fresh.
void add_gap_pair(Placed start, Placed end)
{
    const std::string gap = fresh_gap();
    start.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    std::string first = start.spec.name;
    end.spec.name = node_create::fresh_name(
        "ed_trigger", [&](const std::string& name) { return same_text(name, first) || name_taken(name); });
    start.spec.kind = Kind::kGapStart;
    end.spec.kind = Kind::kGapEnd;
    for (Placed* placed : {&start, &end}) {
        placed->spec.gap = gap;
        placed->spec.size = clamp_size(placed->spec.size);
        placed->spec.text = clean_text(placed->spec.text);
    }
    end.spec.flags = start.spec.flags;
    end.spec.text = start.spec.text;
    end.spec.score = start.spec.score;
    fabricate({start, end}, "a gap");
}

// --- presets ---

namespace {

constexpr float kBoosterPadWidth = 250.0F, kBoosterPadHeight = 60.0F;
constexpr float kBoosterAreaWidth = 800.0F, kBoosterAreaHeight = 120.0F;
constexpr float kExitWidth = 60.0F, kExitHeight = 120.0F; // an Exit's marker box (no collision)
constexpr float kExitSpacing = 600.0F;                    // a new Exit this far beyond its teleport

// `count` fresh trigger names (none taken, none the same).
std::vector<std::string> fresh_names(int count)
{
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        names.push_back(node_create::fresh_name("ed_trigger", [&](const std::string& name) {
            return name_taken(name) ||
                   std::ranges::any_of(names, [&](const std::string& made) { return same_text(made, name); });
        }));
    }
    return names;
}

// The heading the view looks along, flat (a node's yaw: its +z axis (sin, 0, cos) that way).
float view_heading()
{
    const V3 forward = trigger_edit().forward_;
    if ((forward.x * forward.x) + (forward.z * forward.z) <= 0.0F) {
        return trigger_edit().yaw_;
    }
    return std::atan2(forward.x, forward.z);
}

Placed exit_spec(const std::string& name, V3 pos, float yaw)
{
    Placed exit;
    exit.spec.name = name;
    exit.spec.kind = Kind::kExit;
    exit.spec.size = {.x = kExitWidth, .y = kExitHeight, .z = kExitWidth};
    exit.pos = pos;
    exit.yaw = yaw;
    return exit;
}

} // namespace

void add_booster(bool area)
{
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kSpeed;
    placed.spec.size = area ? V3{.x = kBoosterAreaWidth, .y = kBoosterAreaHeight, .z = kBoosterAreaWidth}
                            : V3{.x = kBoosterPadWidth, .y = kBoosterPadHeight, .z = kBoosterPadWidth};
    placed.spec.strength = kDefaultBoosterSpeed;
    placed.pos = place_ahead(placed.spec.size, 0.0F);
    placed.yaw = view_heading();
    fabricate({placed}, area ? "a booster area" : "a booster pad");
}

void add_teleport_pair()
{
    const std::vector<std::string> names = fresh_names(2);
    Placed teleport;
    teleport.spec.name = names.front();
    teleport.spec.kind = Kind::kJumpTo;
    teleport.spec.size = {.x = kTeleportSize, .y = kTeleportSize, .z = kTeleportSize};
    teleport.pos = place_ahead(teleport.spec.size, 0.0F);
    teleport.yaw = view_heading();
    const V3 exit_size = {.x = kExitWidth, .y = kExitHeight, .z = kExitWidth};
    const Placed exit = exit_spec(names.back(), place_ahead(exit_size, kExitSpacing), teleport.yaw);
    teleport.spec.target = exit.spec.name;
    fabricate({teleport, exit}, "a teleport and its exit");
}

void add_exit_for(const std::string& name)
{
    const int index = spec_index(name);
    V3 pos{};
    float yaw = 0.0F;
    if (index < 0 || core::at(editor_trigger_set().specs_, index).kind != Kind::kJumpTo ||
        !node_place(name, &pos, &yaw)) {
        return;
    }
    const std::vector<std::string> names = fresh_names(1);
    const V3 ahead = turn_about_up({.x = 0.0F, .y = 0.0F, .z = 1.0F}, yaw) * kExitSpacing;
    Placed exit = exit_spec(names.front(), pos + ahead, yaw);
    V3 ground{};
    if (ground_below(exit.pos + V3{.x = 0.0F, .y = kPlaceAboveGround, .z = 0.0F}, &ground)) {
        exit.pos.y = ground.y + (kExitHeight * kHalf);
    }
    note_undo();
    Spec teleport = core::at(editor_trigger_set().specs_, index);
    teleport.target = exit.spec.name;
    core::at(editor_trigger_set().specs_, index) = teleport;
    fabricate({exit}, "a teleport exit");
}

void add_two_way_gap()
{
    const std::string there = fresh_gap();
    const std::string back = node_create::fresh_name("ed_gap", [&](const std::string& id) {
        return same_text(id, there) || std::ranges::any_of(editor_trigger_set().specs_,
                                                           [&](const Spec& spec) { return same_text(spec.gap, id); });
    });
    const std::vector<std::string> names = fresh_names(4);
    Placed first;
    first.spec.size = {.x = kGapGateWidth, .y = kGapGateHeight, .z = kGapGateDepth};
    first.spec.flags = {"CANCEL_GROUND"};
    first.spec.score = kDefaultGapScore;
    const int number = text::to_int(std::next(there.c_str(), 6));
    std::array<char, kGapNameSize> text{};
    text::format_to(text, "Editor Gap %d", number > 0 ? number : 1);
    first.pos = place_ahead(first.spec.size, 0.0F);
    first.yaw = trigger_edit().yaw_;
    Placed second = first;
    second.pos = place_ahead(second.spec.size, kGapGateSpacing);
    // (A -> B: gap `there`; B -> A: gap `back` - each place a start of one and the end of the other)
    std::array<Placed, 4> made = {first, second, second, first};
    const std::array<Kind, 4> kinds = {Kind::kGapStart, Kind::kGapEnd, Kind::kGapStart, Kind::kGapEnd};
    for (size_t i = 0; i < made.size(); ++i) {
        Spec& spec = core::at(made, i).spec;
        spec.name = core::at(names, i);
        spec.kind = core::at(kinds, i);
        spec.gap = i < 2 ? there : back;
        spec.text = i < 2 ? std::string(text.data()) : std::string(text.data()) + " Back";
    }
    fabricate(std::vector<Placed>(made.begin(), made.end()), "a two-way gap");
}

bool is_exit(const std::string& name)
{
    const int index = spec_index(name);
    return index >= 0 && core::at(editor_trigger_set().specs_, index).kind == Kind::kExit;
}

void set_spec(const std::string& name, const Spec& spec, bool undo)
{
    const int index = spec_index(name);
    if (index < 0) {
        return;
    }
    if (undo) {
        note_undo();
    }
    Spec stored_spec = spec;
    stored_spec.name = core::at(editor_trigger_set().specs_, index).name;
    stored_spec.size = clamp_size(stored_spec.size);
    stored_spec.text = clean_text(stored_spec.text);
    // (a gap's name, score and flags are the same at both ends)
    if (stored_spec.kind == Kind::kGapStart || stored_spec.kind == Kind::kGapEnd) {
        for (Spec& other : editor_trigger_set().specs_) {
            if (&other != &core::at(editor_trigger_set().specs_, index) && !stored_spec.gap.empty() &&
                same_text(other.gap, core::at(editor_trigger_set().specs_, index).gap) &&
                (other.kind == Kind::kGapStart || other.kind == Kind::kGapEnd)) {
                other.gap = stored_spec.gap;
                other.text = stored_spec.text;
                other.score = stored_spec.score;
                other.flags = stored_spec.flags;
            }
        }
    }
    core::at(editor_trigger_set().specs_, index) = stored_spec;
}

void begin_edit() { note_undo(); }

void turn_off(uint32_t sector, bool off)
{
    const LevelTrigger* trigger = nullptr;
    for (const LevelTrigger& level_trigger : level_trigger_set().level_) {
        if (level_trigger.sector == sector) {
            trigger = &level_trigger;
        }
    }
    if (!trigger || !(trigger->wall || trigger->all_trigger_)) {
        hud::get_message("Has non-trigger faces; can't be disabled", hud::kMessageSeconds);
        return;
    }
    if (std::ranges::find(level_trigger_set().switched_, sector) ==
        level_trigger_set().switched_.end()) {
        level_trigger_set().switched_.push_back(sector); // (its wall state in the undo steps from now on)
    }
    note_undo();
    if (!walls::set_off(sector, off)) {
        hud::get_message("Couldn't change the trigger", hud::kShortMessageSeconds);
        return;
    }
    hud::get_message(off ? "Trigger " + get_label_of(sector) + " disabled"
                         : "Trigger " + get_label_of(sector) + " enabled");
}

void select_wall(uint32_t sector) { trigger_focus().select_wall_ = sector; }

void make_editable(uint32_t sector)
{
    const LevelTrigger* trigger = nullptr;
    for (const LevelTrigger& level_trigger : level_trigger_set().level_) {
        if (level_trigger.sector == sector) {
            trigger = &level_trigger;
        }
    }
    if (!trigger) {
        return;
    }
    if (trigger->script == 0U) {
        hud::get_message("No TriggerScript to copy", hud::kShortMessageSeconds);
        return;
    }
    if (!trigger->wall && !trigger->all_trigger_) {
        hud::get_message("Has non-trigger faces; can't be replaced", hud::kMessageSeconds);
        return;
    }
    for (const Spec& spec : editor_trigger_set().specs_) {
        if (spec.replaces == sector && node_alive(spec.name)) {
            hud::get_message("Already has an editable copy: " + spec.name, hud::kShortMessageSeconds);
            return;
        }
    }
    Placed placed;
    placed.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    placed.spec.kind = Kind::kScripts;
    placed.spec.script = name_for(trigger->script, script::kept_qb_name(trigger->script));
    placed.spec.replaces = sector;
    placed.spec.size = clamp_size({.x = std::max(kMinPieceTriggerSize, trigger->max.x - trigger->min.x),
                                   .y = std::max(kMinPieceTriggerSize, trigger->max.y - trigger->min.y),
                                   .z = std::max(kMinPieceTriggerSize, trigger->max.z - trigger->min.z)});
    placed.pos = (trigger->min + trigger->max) * kHalf;
    placed.yaw = 0.0F;
    fabricate({placed}, ("an editable copy of " + get_label_of(sector)).c_str());
}

void select_partner(const std::string& name)
{
    const int index = spec_index(name);
    const int other = partner(editor_trigger_set().specs_, index);
    if (other >= 0) {
        trigger_edit().select_ = nodes::get_find(hash_of_name(core::at(editor_trigger_set().specs_, other).name));
    }
}

void select_node_named(uint32_t name)
{
    const int index = nodes::get_find(name);
    if (index >= 0) {
        trigger_edit().select_ = index;
    }
}

// --- picking and drawing ---

bool get_pick(V3 origin, V3 unit_dir, int* node, uint32_t* sector, float max_t)
{
    *node = -1;
    *sector = 0;
    if (!get_shown()) {
        return false;
    }
    float best = max_t;
    bool any = false;
    for (const Added& added : editor_trigger_set().added_) {
        if (!added.alive) {
            continue;
        }
        OrientedBox box = box_of(added.pos, added.yaw, added.spec.size);
        float fraction = 0.0F;
        if (ray_box({.origin = origin, .dir = unit_dir}, box.origin, box.axes, box.min, box.max, &fraction) &&
            fraction < best) {
            best = fraction;
            *node = nodes::get_find(hash_of_name(added.spec.name));
            *sector = 0;
            any = *node >= 0;
        }
    }
    for (const LevelTrigger& level_trigger : level_trigger_set().level_) {
        std::array<V3, 3> axes = {
            {{.x = 1.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 1.0F}}};
        const V3 grow = {.x = 2.0F, .y = 2.0F, .z = 2.0F};
        float fraction = 0.0F;
        if (!ray_box({.origin = origin, .dir = unit_dir}, {.x = 0.0F, .y = 0.0F, .z = 0.0F}, axes,
                     level_trigger.min - grow, level_trigger.max + grow, &fraction) ||
            fraction >= best) {
            continue;
        }
        // (the faces themselves when there are few; a big water plane by its box)
        float hit = fraction;
        bool found = level_trigger.face_count_ > kMaxFacesKept;
        for (size_t face_index = 0; !found && face_index + 2 < level_trigger.faces.size(); face_index += 3) {
            float ft = 0.0F;
            if (picking::ray_triangle({.origin = origin, .dir = unit_dir}, core::at(level_trigger.faces, face_index),
                                      core::at(level_trigger.faces, face_index + 1),
                                      core::at(level_trigger.faces, face_index + 2), &ft) &&
                ft < best) {
                hit = ft;
                found = true;
            }
        }
        if (!found || hit >= best) {
            continue;
        }
        best = hit;
        *node = level_trigger.has_node_ ? nodes::get_find(level_trigger.sector) : -1;
        *sector = level_trigger.sector;
        any = true;
    }
    if (any && (*sector != 0U)) {
        trigger_focus().inspected_ = *sector;
    }
    return any;
}

namespace {

constexpr uint32_t kSkater = 0x5B8AB877U; // Skater (the local skater's alias)
struct Seen {
    uint32_t node;
    bool inside;
};
std::vector<Seen>& stored_seen()
{
    static core::NoDestroy<std::vector<Seen>> seen;
    return seen.get();
}

// The trigger's TriggerScript run on the skater, as the trigger component's get_trigger_node (0x00434A60) runs it: not when
// the skate module skips the node (0x00565BB0), its online_mode_flag / Permanent flags, the node's name.
void fire_on_leaving(objects::Entity* skater, uint32_t node)
{
    script::Struct* structure = level_nodes::fetch_vertex(node, false);
    uint32_t script = 0;
    if (!structure || !structure->read_hash_value(kTriggerScript, &script, 0) || (script == 0U)) {
        return;
    }
    if (modules::Skate::should_be_absent(structure) != 0) {
        return;
    }
    static const uint32_t kOnlineFlag = get_checksums("NetEnabled");
    static const uint32_t kPermanent = get_checksums("Permanent");
    skater->start_script_task(script, static_cast<int>(node), structure->has_bare_flag(kOnlineFlag),
                              structure->has_bare_flag(kPermanent), nullptr);
    if (debug_enabled()) {
        get_log("level editor: trigger %s left - its script run (exit)", get_label_of(node).c_str());
    }
}

} // namespace

void perform(bool editing)
{
    const bool any = std::ranges::any_of(editor_trigger_set().added_, [](const Added& a) { return a.alive && a.spec.exit; });
    if (editing || !any || trigger_edit().boxes_.empty() || layout::get_refused()) { // (a network session or a career-type mode)
        stored_seen().clear(); // (seen afresh once play goes on: a box moved while editing doesn't fire)
        return;
    }
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater) {
        return;
    }
    const V3 at = {.x = skater->at_point().x, .y = skater->at_point().y, .z = skater->at_point().z};
    for (const Added& added : editor_trigger_set().added_) {
        if (!added.alive || !added.spec.exit || added.problem) {
            continue;
        }
        uint32_t node = hash_of_name(added.spec.name);
        const bool now = get_inside(box_of(added.pos, added.yaw, added.spec.size), at);
        auto it = std::ranges::find_if(stored_seen(), [&](const Seen& seen) { return seen.node == node; });
        const bool known = it != stored_seen().end();
        if (get_left(known, known && it->inside, now)) {
            fire_on_leaving(skater, node);
        }
        if (known) {
            it->inside = now;
        } else {
            stored_seen().push_back({.node = node, .inside = now});
        }
    }
}

} // namespace frontend::level_editor::triggers
