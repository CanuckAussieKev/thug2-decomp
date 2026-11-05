// NOT ORIGINAL - the level editor's trigger volumes, kill zones, teleports and gaps (ours). See
// level_editor_triggers.h. This file holds the state, the hooks the scripts reach, adding triggers and the per-frame
// tick; the pieces (triggers_records / triggers_sync) are level_editor_triggers_<name>.cpp, sharing level_editor_triggers_internal.h.

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
#include "editor/level_editor_ui.h"
#include "base/maths/vec2.h"
#include "engine/render/coll_static.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
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
namespace {

constexpr std::size_t kGapNameSize = 32;
constexpr std::size_t kStatusTextSize = 160;
constexpr std::size_t kBoostTextSize = 48;
constexpr std::size_t kSpeedUpTextSize = 64;

constexpr float kMinPieceTriggerSize = 20.0F; // the least a trigger copied from a piece box measures

constexpr uint32_t kHandleOutlineColour = 0xFF101010U;

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
constexpr float kBoostWidth = 250.0F;
constexpr float kBoostHeight = 60.0F;
constexpr float kDefaultBoostMultiplier = 7.5F; // shown when the call has none
// The overlay.
constexpr float kDefaultSegmentThickness = 1.5F;
constexpr float kOverlayLineThickness = 2.0F;
constexpr float kMinArrowLength = 4.0F; // shorter arrows are not drawn
constexpr uint32_t kLinkArrowAlpha = 0xE0;
constexpr uint32_t kKillArrowAlpha = 0x80;
constexpr uint32_t kGapArrowAlpha = 0xB0;
constexpr uint32_t kLevelBoostArrowAlpha = 0xA0;
constexpr float kMinSideLengthSquared = 1e-4F;
constexpr float kArrowHeadLength = 9.0F;
constexpr float kArrowHeadWidth = 7.0F;
constexpr float kSpeedUpLabelDistance = 1500.0F; // how close the camera is for a speed-up label
constexpr float kSpeedUpLabelLift = 14.0F;
constexpr float kProximDrawDistance = 6000.0F; // how far away an unselected proximity node is still drawn
constexpr float kProximBoxThickness = 1.5F;
constexpr float kProximLabelLift = 10.0F;
constexpr float kBreakDrawDistance = 4000.0F;
constexpr int kCrackRays = 5;
constexpr float kCrackThickness = 1.5F;
constexpr float kBreakLabelLift = 12.0F;
constexpr float kBoostArrowScale = 0.15F; // arrow length per unit of boost strength
constexpr float kBoostDrawDistance = 3000.0F;
constexpr float kDiameterFactor = 2.0F;
constexpr int kHandleCount = 6; // a box resize handles: one for each face
constexpr int kLastHandle = 5;
constexpr float kLitHandleRadius = 7.0F;
constexpr float kHandleRadius = 5.5F;
constexpr float kResizeSnapGrid = 25.0F;

constexpr float kDrawRadius = 8000.0F; // (level triggers drawn within this of the camera)
constexpr float kLabelRadius = 3000.0F;

} // namespace
namespace internal {

LevelEditorTriggersState& level_editor_triggers_state()
{
    static core::NoDestroy<LevelEditorTriggersState> state;
    return state.get();
}

TriggerEdit& trigger_edit()
{
    static core::NoDestroy<TriggerEdit> state;
    return state.get();
}

uint32_t hash_of_name(const std::string& name) { return node_create::name_hash(name); }

} // namespace internal
namespace {

std::string get_label_of(uint32_t checksum) { return nodes::caption(checksum); }

} // namespace
namespace internal {

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

} // namespace internal
namespace {

// The level's node array noted (the triggers belong to it): another array added after it (a cutscene's) doesn't
// change it; the level's gone and another there forgets them.
void note_level()
{
    node_create::set_array_removed_listener(&on_array_removed);
    if (level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get()) {
        if ((level_editor_triggers_state().level_array_ != 0U) &&
            array_loaded(manager, level_editor_triggers_state().level_array_)) {
            return;
        }
        const uint32_t level = manager->get_last_node_array_name();
        if ((level != 0U) && level != level_editor_triggers_state().level_array_) {
            if (level_editor_triggers_state().level_array_ != 0U) {
                get_forget();
            }
            level_editor_triggers_state().level_array_ = level;
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

std::string fresh_gap()
{
    return node_create::fresh_name("ed_gap", [](const std::string& id) {
        return std::ranges::any_of(level_editor_triggers_state().specs_,
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
        level_editor_triggers_state().specs_.push_back(placed.spec);
        if (first < 0) {
            first = static_cast<int>(level_editor_triggers_state().specs_.size()) - 1;
        }
        get_log("level editor: added trigger %s (%s) at (%.1f, %.1f, %.1f)", placed.spec.name.c_str(),
                kind_name(placed.spec.kind), placed.pos.x, placed.pos.y, placed.pos.z);
    }
    get_sync();
    if (first >= 0) {
        trigger_edit().select_ = nodes::get_find(hash_of_name(core::at(level_editor_triggers_state().specs_, first).name));
        hud::get_message(std::string("Added ") + what, hud::kShortMessageSeconds);
    }
}

} // namespace

void get_guard()
{
    sync_proxims(); // (proximity nodes as their edits say - put back in a network session)
    // A breakable broken in play (its piece killed by its own script): its box goes, so it isn't shattered again.
    for (size_t i = 0; i < trigger_edit().boxes_.size();) {
        int at = -1;
        for (size_t k = 0; k < level_editor_triggers_state().specs_.size(); ++k) {
            if (core::at(level_editor_triggers_state().specs_, k).kind == Kind::kBreak &&
                hash_of_name(core::at(level_editor_triggers_state().specs_, k).name) ==
                    core::at(trigger_edit().boxes_, i).name) {
                at = static_cast<int>(k);
            }
        }
        if (at >= 0 && !is_sector_enabled(checksum_of(core::at(level_editor_triggers_state().specs_, at).target))) {
            free_box(core::at(trigger_edit().boxes_, i).coll);
            trigger_edit().boxes_.erase(trigger_edit().boxes_.begin() + static_cast<std::ptrdiff_t>(i));
            get_log("level editor: breakable %s broken - its box taken out until the piece is back",
                    core::at(level_editor_triggers_state().specs_, at).name.c_str());
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

const std::vector<LevelTrigger>& level_triggers() { return level_editor_triggers_state().level_; }
const std::vector<Added>& added_triggers() { return level_editor_triggers_state().added_; }

bool is_trigger(uint32_t name)
{
    return std::ranges::any_of(level_editor_triggers_state().specs_,
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

uint32_t inspected_sector() { return level_editor_triggers_state().inspected_; }
void inspect(uint32_t sector) { level_editor_triggers_state().inspected_ = sector; }

void get_queue(std::function<void()> action) { trigger_edit().actions_.push_back(std::move(action)); }

void kickoff()
{
    note_level();
    scan_level();
    trigger_edit().level_gaps_read_ = false;
    // (breakables broken while playing: their pieces back to edit - and to break again)
    for (const Spec& spec : level_editor_triggers_state().specs_) {
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
    if (!level_editor_triggers_state().scanned_) {
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
    stored_spec.name = core::at(level_editor_triggers_state().specs_, index).name;
    stored_spec.size = clamp_size(stored_spec.size);
    stored_spec.text = clean_text(stored_spec.text);
    // (a gap's name, score and flags are the same at both ends)
    if (stored_spec.kind == Kind::kGapStart || stored_spec.kind == Kind::kGapEnd) {
        for (Spec& other : level_editor_triggers_state().specs_) {
            if (&other != &core::at(level_editor_triggers_state().specs_, index) && !stored_spec.gap.empty() &&
                same_text(other.gap, core::at(level_editor_triggers_state().specs_, index).gap) &&
                (other.kind == Kind::kGapStart || other.kind == Kind::kGapEnd)) {
                other.gap = stored_spec.gap;
                other.text = stored_spec.text;
                other.score = stored_spec.score;
                other.flags = stored_spec.flags;
            }
        }
    }
    core::at(level_editor_triggers_state().specs_, index) = stored_spec;
}

void begin_edit() { note_undo(); }

void turn_off(uint32_t sector, bool off)
{
    const LevelTrigger* trigger = nullptr;
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        if (level_trigger.sector == sector) {
            trigger = &level_trigger;
        }
    }
    if (!trigger || !(trigger->wall || trigger->all_trigger_)) {
        hud::get_message("Has non-trigger faces; can't be disabled", hud::kMessageSeconds);
        return;
    }
    if (std::ranges::find(level_editor_triggers_state().switched_, sector) ==
        level_editor_triggers_state().switched_.end()) {
        level_editor_triggers_state().switched_.push_back(sector); // (its wall state in the undo steps from now on)
    }
    note_undo();
    if (!walls::set_off(sector, off)) {
        hud::get_message("Couldn't change the trigger", hud::kShortMessageSeconds);
        return;
    }
    hud::get_message(off ? "Trigger " + get_label_of(sector) + " disabled"
                         : "Trigger " + get_label_of(sector) + " enabled");
}

void select_wall(uint32_t sector) { level_editor_triggers_state().select_wall_ = sector; }

void make_editable(uint32_t sector)
{
    const LevelTrigger* trigger = nullptr;
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
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
    for (const Spec& spec : level_editor_triggers_state().specs_) {
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
    const int other = partner(level_editor_triggers_state().specs_, index);
    if (other >= 0) {
        trigger_edit().select_ = nodes::get_find(hash_of_name(core::at(level_editor_triggers_state().specs_, other).name));
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
    for (const Added& added : level_editor_triggers_state().added_) {
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
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
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
        level_editor_triggers_state().inspected_ = *sector;
    }
    return any;
}

namespace {

struct Painter {
    V3 camera{};
    const TickCycle* frame{};
    const Lens* lens{};
    std::span<const uint32_t, 4> viewport;
    float near_z_{};
    std::vector<ui::Seg>* lines{};

    bool get_to_pixel(V3 point, float* x, float* y) const
    {
        float nx = 0.0F;
        float ny = 0.0F;
        if (!cast_onto(camera, *frame, *lens, point, near_z_, &nx, &ny)) {
            return false;
        }
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    }
    void get_seg(V3 a, V3 b, uint32_t argb, float thickness = kDefaultSegmentThickness) const
    {
        if (!clip_to_near(camera, *frame, near_z_ + kNearPlaneMargin, &a, &b)) {
            return;
        }
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (get_to_pixel(a, &ax, &ay) && get_to_pixel(b, &bx, &by)) {
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
        }
    }
    void cuboid(const OrientedBox& box, uint32_t argb, float thickness) const
    {
        std::array<V3, kBoxCorners> corners{};
        box_corners(box.origin, box.axes, box.min, box.max, corners);
        for (int i = 0; i < kBoxCorners; ++i) {
            for (const int bit : {1, 2, 4}) {
                if ((i & bit) == 0) {
                    get_seg(core::at(corners, i), core::at(corners, i | bit), argb, thickness);
                }
            }
        }
    }
    // An arrow from a to b, its head drawn on the screen.
    void get_arrow(V3 a, V3 b, uint32_t argb) const
    {
        get_seg(a, b, argb, kOverlayLineThickness);
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (!get_to_pixel(a, &ax, &ay) || !get_to_pixel(b, &bx, &by)) {
            return;
        }
        float dx = bx - ax;
        float dy = by - ay;
        float const length = std::sqrt((dx * dx) + (dy * dy));
        if (length < kMinArrowLength) {
            return;
        }
        dx /= length;
        dy /= length;
        const float head = std::min(14.0F, length * 0.4F);
        for (const float side : {-1.0F, 1.0F}) {
            const float hx = (-dx * 0.9F) + (side * -dy * 0.45F);
            const float hy = (-dy * 0.9F) + (side * dx * 0.45F);
            lines->push_back({.ax = bx,
                              .ay = by,
                              .bx = bx + (hx * head),
                              .by = by + (hy * head),
                              .argb = argb,
                              .thickness = kOverlayLineThickness});
        }
    }
};

uint32_t get_dim(uint32_t argb, uint32_t alpha) { return (argb & kRgbMask) | (alpha << kAlphaShift); }

float distance_to(V3 point, V3 min, V3 max)
{
    const float dx = std::max({min.x - point.x, 0.0F, point.x - max.x});
    const float dy = std::max({min.y - point.y, 0.0F, point.y - max.y});
    const float dz = std::max({min.z - point.z, 0.0F, point.z - max.z});
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

std::string describe(const Calls& calls, Kind kind)
{
    std::string text = kind_name(kind);
    switch (kind) {
    case Kind::kKill:
        if (calls.water) {
            text += " (water)";
        }
        if (calls.target != 0U) {
            text += " -> " + get_label_of(calls.target);
        }
        break;
    case Kind::kJumpTo:
        if (calls.target != 0U) {
            text += " -> " + get_label_of(calls.target);
        }
        break;
    case Kind::kGapEnd:
        text += ": " + (calls.gap_text_.empty() ? get_label_of(calls.gap_id_) : calls.gap_text_);
        if (calls.gap_score_ != 0) {
            text += " (" + std::to_string(calls.gap_score_) + ")";
        }
        break;
    case Kind::kGapStart:
        if (calls.gap_id_ != 0U) {
            text += ": " + get_label_of(calls.gap_id_);
        }
        break;
    case Kind::kBoost: {
        std::array<char, kBoostTextSize> value{};
        if (calls.boost_speed_ > 0.0F) {
            text::format_to(value, " (speed %.0f)", calls.boost_speed_);
        } else {
            text::format_to(value, " (x%.1f)",
                            calls.boost_multi_ > 0.0F ? calls.boost_multi_ : kDefaultBoostMultiplier);
        }
        text += value.data();
        break;
    }
    case Kind::kViewCam:
        if (calls.camera_node_ != 0U) {
            text += " -> " + get_label_of(calls.camera_node_);
        }
        break;
    default:
        break;
    }
    return text;
}

bool get_node_pos(uint32_t name, V3* pos)
{
    maths::Vec4 point;
    if ((name == 0U) || !level_nodes::fetch_vertex(name, false) || !level_nodes::fetch_place(name, &point)) {
        return false;
    }
    *pos = {.x = point.x, .y = point.y, .z = point.z};
    return true;
}

// One of the editor's triggers: its box (grey when it has a problem), the arrow to what it links to (a gap's to
// its end) and its label.
template <class Label>
void draw_added_trigger(Painter& paint, const Added& added, Label& label)
{
    const OrientedBox box = box_of(added.pos, added.yaw, added.spec.size);
    const uint32_t colour = added.problem ? 0xC0A0A0A0U : kind_colour(added.spec.kind);
    paint.cuboid(box, colour, kOverlayLineThickness);
    V3 target{};
    if (!added.problem &&
        (added.spec.kind == Kind::kJumpTo || added.spec.kind == Kind::kKill || added.spec.kind == Kind::kViewCam) &&
        get_node_pos(checksum_of(added.spec.target), &target)) {
        paint.get_arrow(added.pos, target,
                        get_dim(colour, added.spec.kind == Kind::kKill ? kKillArrowAlpha : kLinkArrowAlpha));
    }
    if (added.spec.kind == Kind::kGapStart) {
        const int other = partner(level_editor_triggers_state().specs_, spec_index(added.spec.name));
        if (other >= 0) {
            for (const Added& partner_trigger : level_editor_triggers_state().added_) {
                if (partner_trigger.alive &&
                    same_text(partner_trigger.spec.name, core::at(level_editor_triggers_state().specs_, other).name)) {
                    paint.get_arrow(added.pos, partner_trigger.pos,
                                    get_dim(kind_colour(Kind::kGapEnd), kGapArrowAlpha));
                }
            }
        }
    }
    std::string text = added.spec.name + ": " + kind_name(added.spec.kind);
    if (added.spec.kind == Kind::kKill || added.spec.kind == Kind::kJumpTo) {
        text += " -> " + added.spec.target;
    } else if (added.spec.kind == Kind::kScripts) {
        text += " " + added.spec.script;
    } else if (added.spec.kind == Kind::kGapStart || added.spec.kind == Kind::kGapEnd) {
        text += " \"" + added.spec.text + "\"";
    } else if (added.spec.kind == Kind::kBoost) {
        text += " " + std::to_string(added.spec.strength);
    } else if (added.spec.kind == Kind::kBreak || added.spec.kind == Kind::kViewCam) {
        text += " " + added.spec.target;
    }
    if (added.problem) {
        text += " (not active)";
    }
    label({.x = added.pos.x, .y = added.pos.y + (added.spec.size.y * kHalf), .z = added.pos.z}, colour, text);
}

void remember_view(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z);

} // namespace

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    remember_view(camera, frame, lens, viewport, near_z); // (the resize handles are drawn and picked with it)
    if (!get_shown()) {
        return;
    }
    Painter paint = {.camera = camera, .frame = &frame, .lens = &lens, .viewport = viewport, .near_z_ = near_z, .lines = lines};
    int labels = 0;
    auto label = [&](V3 at, uint32_t argb, const std::string& text) {
        if (labels >= kMaxLabels) {
            return;
        }
        float x = 0.0F;
        float y = 0.0F;
        if (paint.get_to_pixel(at, &x, &y)) {
            texts->push_back({.x = x, .y = y, .argb = argb, .text = text});
            ++labels;
        }
    };
    // The level's: nearest first.
    std::vector<std::pair<float, const LevelTrigger*>> near;
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        const float distance = distance_to(camera, level_trigger.min, level_trigger.max);
        if (distance <= kDrawRadius) {
            near.emplace_back(distance, &level_trigger);
        }
    }
    std::ranges::sort(near, [](const auto& a, const auto& b) { return a.first < b.first; });
    if (near.size() > static_cast<size_t>(kMaxLevelDrawn)) {
        near.resize(kMaxLevelDrawn);
    }
    for (const auto& candidate : near) {
        const LevelTrigger& level_trigger = *candidate.second;
        const bool inspected = level_trigger.sector == level_editor_triggers_state().inspected_;
        const uint32_t colour =
            level_trigger.off ? 0x70909090U : get_dim(kind_colour(level_trigger.kind), inspected ? 0xFFU : 0xC0U);
        const float thickness = inspected ? 2.5F : 1.0F;
        if (level_trigger.face_count_ <= kMaxFacesKept) {
            for (size_t face_index = 0; face_index + 2 < level_trigger.faces.size(); face_index += 3) {
                paint.get_seg(core::at(level_trigger.faces, face_index), core::at(level_trigger.faces, face_index + 1),
                              colour, thickness);
                paint.get_seg(core::at(level_trigger.faces, face_index + 1),
                              core::at(level_trigger.faces, face_index + 2), colour, thickness);
                paint.get_seg(core::at(level_trigger.faces, face_index + 2), core::at(level_trigger.faces, face_index),
                              colour, thickness);
            }
        } else {
            const OrientedBox box = {.origin = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                     .axes = {{{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                               {.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                               {.x = 0.0F, .y = 0.0F, .z = 1.0F}}},
                                     .min = level_trigger.min,
                                     .max = level_trigger.max};
            paint.cuboid(box, colour, thickness);
        }
        const V3 centre = (level_trigger.min + level_trigger.max) * 0.5F;
        V3 target{};
        if (!level_trigger.off &&
            ((level_trigger.kind == Kind::kJumpTo && get_node_pos(level_trigger.calls.target, &target)) ||
             (level_trigger.kind == Kind::kViewCam &&
              get_node_pos(level_trigger.calls.camera_node_, &target)))) { // (map4: the camera's)
            paint.get_arrow(centre, target, get_dim(kind_colour(level_trigger.kind), kLinkArrowAlpha));
        }
        if (candidate.first <= kLabelRadius || inspected) {
            label({.x = centre.x, .y = level_trigger.max.y, .z = centre.z}, colour,
                  (level_trigger.off ? "(off) " : "") + describe(level_trigger.calls, level_trigger.kind) + "  " +
                      get_label_of(level_trigger.sector));
        }
    }
    // The editor's.
    for (const Added& added : level_editor_triggers_state().added_) {
        if (!added.alive) {
            continue;
        }
        draw_added_trigger(paint, added, label);
    }
}

namespace {

// A label at a world point, if it's on screen.
void add_label(Painter& paint, std::vector<ui::Overlay::OverlayText>* texts, V3 at, uint32_t argb,
               const std::string& text)
{
    float x = 0.0F;
    float y = 0.0F;
    if (paint.get_to_pixel(at, &x, &y)) {
        texts->push_back({.x = x, .y = y, .argb = argb, .text = text});
    }
}

// Rail speed-ups near the camera: chevrons along each segment pointing uphill (where they speed the skater up),
// 60 apart, and their rates.
void draw_speed_ups(Painter& paint, V3 camera, std::vector<ui::Overlay::OverlayText>* texts)
{
    auto label = [&](V3 at, uint32_t argb, const std::string& text) { add_label(paint, texts, at, argb, text); };
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    // Rail speed-ups: chevrons along each segment pointing uphill (where they speed the skater up), 60 apart.
    for (const chains::SpeedSegment& speed_segment : chains::speed_segments(camera, 3000.0F)) {
        V3 point_a = speed_segment.a;
        V3 point_b = speed_segment.b;
        if (point_b.y < point_a.y) {
            std::swap(point_a, point_b);
        }
        const V3 delta = point_b - point_a;
        const float length = std::sqrt(get_dot(delta, delta));
        if (length < 1.0F) {
            continue;
        }
        const V3 dir = delta * (1.0F / length);
        V3 side = perpendicular(dir, up);
        if (get_dot(side, side) < kMinSideLengthSquared) {
            side = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
        }
        side = make_unit(side);
        const uint32_t colour = speed_segment.level ? 0xC0B4B4B4U : 0xFFFF9F1AU;
        const int count = std::max(1, std::min(24, static_cast<int>(length / 60.0F)));
        for (int i = 1; i <= count; ++i) {
            const V3 tip = point_a + delta * (static_cast<float>(i) / static_cast<float>(count + 1)) + up * 4.0F;
            paint.get_seg(tip - dir * kArrowHeadLength + side * kArrowHeadWidth, tip, colour, kOverlayLineThickness);
            paint.get_seg(tip - dir * kArrowHeadLength - side * kArrowHeadWidth, tip, colour, kOverlayLineThickness);
        }
        const V3 middle = (point_a + point_b) * 0.5F;
        const V3 middle_offset = middle - camera;
        if (get_dot(middle_offset, middle_offset) < kSpeedUpLabelDistance * kSpeedUpLabelDistance) {
            std::array<char, kSpeedUpTextSize> text{};
            text::format_to(text, "%s+%.1f%%", speed_segment.level ? "level speed-up " : "speed-up ",
                            speed_segment.rate * 100.0F);
            label(middle + up * kSpeedUpLabelLift, colour, text.data());
        }
    }
}

// A proximity node (near the camera, or the selected one): a sphere's three rings or a box's edges, and what it runs.
void draw_proxim_node(Painter& paint, V3 camera, std::vector<ui::Overlay::OverlayText>* texts, uint32_t node,
                      bool is_selected)
{
    auto label = [&](V3 at, uint32_t argb, const std::string& text) { add_label(paint, texts, at, argb, text); };
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const objects::ProximNode* proxim_node = is_proxim(node) ? objects::proxim_get_node(node) : nullptr;
    if (!proxim_node) {
        return;
    }
    const V3 at = {.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z};
    const V3 delta = at - camera;
    if (!is_selected && get_dot(delta, delta) > kProximDrawDistance * kProximDrawDistance) {
        return;
    }
    const uint32_t colour = is_selected ? 0xFFFFB060U : 0xC0F08A3CU;
    V3 top = at;
    if (proxim_node->form() == objects::ProximShape::kSphere && proxim_node->radius() > 0.0F) {
        constexpr int kSides = 32;
        const std::array<std::array<V3, 2>, 3> axes = {{{{{.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 0, .z = 1}}},
                                                        {{{.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0}}},
                                                        {{{.x = 0, .y = 1, .z = 0}, {.x = 0, .y = 0, .z = 1}}}}};
        for (const auto& plane : axes) {
            V3 previous = at + core::at(plane, 0) * proxim_node->radius();
            for (int i = 1; i <= kSides; ++i) {
                const float angle = 2.0F * 3.14159265F * static_cast<float>(i) / kSides;
                const V3 next = at + core::at(plane, 0) * (proxim_node->radius() * std::cos(angle)) +
                                core::at(plane, 1) * (proxim_node->radius() * std::sin(angle));
                paint.get_seg(previous, next, colour, 1.0F);
                previous = next;
            }
        }
        top = at + up * proxim_node->radius();
    } else if (proxim_node->form() == objects::ProximShape::kBox) {
        const V3 min = {.x = proxim_node->box().min.x, .y = proxim_node->box().min.y, .z = proxim_node->box().min.z};
        const V3 max = {.x = proxim_node->box().max.x, .y = proxim_node->box().max.y, .z = proxim_node->box().max.z};
        const OrientedBox box = {.origin = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                 .axes = {{{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                           {.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                           {.x = 0.0F, .y = 0.0F, .z = 1.0F}}},
                                 .min = min,
                                 .max = max};
        paint.cuboid(box, colour, kProximBoxThickness);
        top = {.x = (min.x + max.x) * kHalf, .y = max.y, .z = (min.z + max.z) * kHalf};
    }
    if (const ProximEdit* proxim_edit = proxim_of(node)) {
        std::string text = "Proximity";
        if (!proxim_edit->enter.empty()) {
            text += "  in: " + proxim_edit->enter;
        }
        if (!proxim_edit->exit.empty()) {
            text += "  out: " + proxim_edit->exit;
        }
        label(top + up * kProximLabelLift, colour, text);
    }
}

// A breakable: cracks over the piece's box (each face's middle, five jagged rays) and its state.
void draw_breakable(Painter& paint, const Added& added, std::vector<ui::Overlay::OverlayText>* texts)
{
    auto label = [&](V3 at, uint32_t argb, const std::string& text) { add_label(paint, texts, at, argb, text); };
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const uint32_t colour = added.broken ? 0x90A0A0A0U : 0xFFE8F4FFU;
    const V3 half = added.spec.size * 0.5F;
    const std::array<V3, 4> normals = {
        {{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0}, {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}}};
    for (const V3& normal : normals) {
        const V3 across = {.x = normal.z, .y = 0.0F, .z = -normal.x};
        const float width = (std::fabs(across.x) * half.x) + (std::fabs(across.z) * half.z);
        const V3 centre = added.pos + V3{.x = normal.x * half.x, .y = 0.0F, .z = normal.z * half.z};
        for (int ray = 0; ray < kCrackRays; ++ray) {
            const float angle = 0.6F + (static_cast<float>(ray) * 1.26F);
            V3 previous = centre;
            for (int step = 1; step <= 3; ++step) {
                const float reach = static_cast<float>(step) / 3.0F;
                const float jag = ((step & 1) != 0) ? 0.18F : -0.12F;
                const V3 next = centre + across * (std::cos(angle + jag) * width * 0.8F * reach) +
                                up * (std::sin(angle + jag) * half.y * 0.8F * reach);
                paint.get_seg(previous, next, colour, kCrackThickness);
                previous = next;
            }
        }
    }
    label(added.pos + up * (half.y + kBreakLabelLift), colour,
          std::string(added.broken ? "broken (back in the editor)" : "breaks when hit") +
              (added.spec.sound.empty() ? "" : "  " + added.spec.sound));
}

} // namespace

void add_extras(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport,
                ExtrasPick extras_pick, std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    const auto near_z = extras_pick.near_z_;
    const auto selected_node = extras_pick.selected_node_;
    Painter paint = {
        .camera = camera, .frame = &frame, .lens = &lens, .viewport = viewport, .near_z_ = near_z, .lines = lines};
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    draw_speed_ups(paint, camera, texts);
    // Proximity nodes: the edited ones and the selected one - a sphere's three rings, a box's edges.
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const uint32_t selected =
        selected_node >= 0 && std::cmp_less(selected_node, nodes.size()) ? core::at(nodes, selected_node).name : 0;
    auto draw_proxim = [&](uint32_t node, bool is_selected) {
        draw_proxim_node(paint, camera, texts, node, is_selected);
    };
    for (const ProximEdit& stored_proxim : stored_proxims()) {
        if (stored_proxim.node != selected) {
            draw_proxim(stored_proxim.node, false);
        }
    }
    if ((selected != 0U) && is_proxim(selected)) {
        draw_proxim(selected, true);
    }
    // Breakables: cracks over the piece's box (each face's middle, five jagged rays); jump boosts: an arrow up as
    // strong as they are (the level's too).
    for (const Added& added : level_editor_triggers_state().added_) {
        if (!added.alive) {
            continue;
        }
        const V3 delta = added.pos - camera;
        if (get_dot(delta, delta) > kBreakDrawDistance * kBreakDrawDistance) {
            continue;
        }
        if (added.spec.kind == Kind::kBreak) {
            draw_breakable(paint, added, texts);
        } else if (added.spec.kind == Kind::kBoost) {
            const V3 base = added.pos + up * (added.spec.size.y * 0.5F);
            paint.get_arrow(base, base + up * (static_cast<float>(added.spec.strength) * kBoostArrowScale),
                            kind_colour(Kind::kBoost));
        }
    }
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        if (level_trigger.kind != Kind::kBoost || level_trigger.off) {
            continue;
        }
        const V3 centre = (level_trigger.min + level_trigger.max) * 0.5F;
        const V3 delta = centre - camera;
        if (get_dot(delta, delta) > kBoostDrawDistance * kBoostDrawDistance) {
            continue;
        }
        const float strength = level_trigger.calls.boost_speed_ > 0.0F ? level_trigger.calls.boost_speed_ : 600.0F;
        const V3 base = {.x = centre.x, .y = level_trigger.max.y, .z = centre.z};
        paint.get_arrow(base, base + up * (strength * kBoostArrowScale), get_dim(kind_colour(Kind::kBoost), kLevelBoostArrowAlpha));
    }
}

namespace {

constexpr std::array<uint32_t, 3> kHandleColours = {
    {0xFFFF4040U, 0xFF40E040U, 0xFF4080FFU}}; // (x, y, z: as the move arrows)

// The selected editor trigger's node, the view add_overlay last drew with, the handles where they were drawn, the one
// under the cursor, and a resize under way.
uint32_t& stored_selected_node()
{
    static uint32_t selected_node{};
    return selected_node;
}
struct LastView {
    bool have = false;
    V3 camera = {};
    TickCycle frame = {};
    Lens lens = {};
    std::array<uint32_t, 4> viewport = {};
    float near_z_ = 1.0F;
};

LastView& last_view()
{
    static core::NoDestroy<LastView> state;
    return state.get();
}
struct DrawnHandle {
    int handle;
    float x, y;
};
// NOT ORIGINAL grouping - the handles drawn and the one hovered (were globals). Made on first use, never destroyed
// (trigger_handles()).
struct TriggerHandles {
    std::vector<DrawnHandle> drawn_handles_;
    int hover_handle_ = -1;
};

TriggerHandles& trigger_handles()
{
    static core::NoDestroy<TriggerHandles> state;
    return state.get();
}

struct ResizeDrag {
    bool on = false;
    std::string name;
    uint32_t node = 0;
    int handle = -1;
    V3 pos0 = {}, size0 = {}, start = {}, direction = {};
    float yaw = 0.0F, s0 = 0.0F;
    bool proxim = false, sphere = false; // (a proximity node's shape)
};

ResizeDrag& resize_drag()
{
    static core::NoDestroy<ResizeDrag> state;
    return state.get();
}

void remember_view(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z)
{
    last_view().have = true;
    last_view().camera = camera;
    last_view().frame = frame;
    last_view().lens = lens;
    for (int i = 0; i < 4; ++i) {
        core::at(last_view().viewport, i) = core::at(viewport, i);
    }
    last_view().near_z_ = near_z;
}

const Added* alive_named(uint32_t node)
{
    for (const Added& added : level_editor_triggers_state().added_) {
        if (added.alive && hash_of_name(added.spec.name) == node) {
            return &added;
        }
    }
    return nullptr;
}

} // namespace

void set_selected(uint32_t node)
{
    if (node != stored_selected_node()) {
        trigger_handles().hover_handle_ = -1;
    }
    stored_selected_node() = node;
}

namespace {

// What the handles resize: an editor trigger's box (not a breakable's: its box is its piece's), or a proximity node's
// sphere / box (its edit's, else the shape the game has).
struct HandleShape {
    bool trigger = false, sphere = false;
    V3 pos = {};
    float yaw = 0.0F;
    V3 size = {};
};
bool shape_for(uint32_t node, HandleShape* handle_shape)
{
    *handle_shape = HandleShape{};
    if (const Added* added = alive_named(node)) {
        if (added->spec.kind == Kind::kBreak) {
            return false;
        }
        handle_shape->trigger = true;
        handle_shape->pos = added->pos;
        handle_shape->yaw = added->yaw;
        handle_shape->size = added->spec.size;
        return true;
    }
    const objects::ProximNode* proxim_node = is_proxim(node) ? objects::proxim_get_node(node) : nullptr;
    if (!proxim_node) {
        return false;
    }
    const V3 at = {.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z};
    const ProximEdit* proxim_edit = proxim_of(node);
    if (proxim_edit && proxim_edit->shape) {
        handle_shape->sphere = !proxim_edit->box;
        handle_shape->pos = proxim_edit->box ? at + proxim_edit->offset : at;
        handle_shape->size = proxim_edit->box ? proxim_edit->size
                                              : V3{.x = proxim_edit->radius * kDiameterFactor,
                                                   .y = proxim_edit->radius * kDiameterFactor,
                                                   .z = proxim_edit->radius * kDiameterFactor};
        return true;
    }
    if (proxim_node->form() == objects::ProximShape::kSphere) {
        handle_shape->sphere = true;
        handle_shape->pos = at;
        handle_shape->size = {.x = proxim_node->radius() * kDiameterFactor,
                              .y = proxim_node->radius() * kDiameterFactor,
                              .z = proxim_node->radius() * kDiameterFactor};
        return proxim_node->radius() > 0.0F;
    }
    if (proxim_node->form() == objects::ProximShape::kBox) {
        const V3 min = {.x = proxim_node->box().min.x, .y = proxim_node->box().min.y, .z = proxim_node->box().min.z};
        const V3 max = {.x = proxim_node->box().max.x, .y = proxim_node->box().max.y, .z = proxim_node->box().max.z};
        handle_shape->pos = (min + max) * kHalf;
        handle_shape->size = max - min;
        return handle_shape->size.x > 0.0F && handle_shape->size.y > 0.0F && handle_shape->size.z > 0.0F;
    }
    return false; // (a sector's own shape: not resized here)
}

} // namespace

void add_handles(std::vector<ui::Overlay::Circle>* circles)
{
    trigger_handles().drawn_handles_.clear();
    HandleShape shape;
    if (!last_view().have || !shape_for(stored_selected_node(), &shape)) {
        return;
    }
    for (int handle = 0; handle < kHandleCount; ++handle) {
        const V3 point = handle_point(shape.pos, shape.yaw, shape.size, handle);
        float nx = 0.0F;
        float ny = 0.0F;
        float x = 0.0F;
        float y = 0.0F;
        if (!cast_onto(last_view().camera, last_view().frame, last_view().lens, point, last_view().near_z_, &nx, &ny)) {
            continue;
        }
        ndc_to_pixel(last_view().viewport, nx, ny, &x, &y);
        const bool lit =
            handle == trigger_handles().hover_handle_ || (resize_drag().on && handle == resize_drag().handle);
        const uint32_t colour = lit ? 0xFFFFFF40U : core::at(kHandleColours, handle >> 1);
        circles->push_back({.x = x,
                            .y = y,
                            .radius = lit ? kLitHandleRadius : kHandleRadius,
                            .argb = colour,
                            .filled = true,
                            .thickness = 1.0F});
        circles->push_back({.x = x,
                            .y = y,
                            .radius = lit ? kLitHandleRadius : kHandleRadius,
                            .argb = kHandleOutlineColour,
                            .filled = false,
                            .thickness = 1.0F});
        trigger_handles().drawn_handles_.push_back({.handle = handle, .x = x, .y = y});
    }
}

int handle_at(maths::Vec2 at, float radius)
{
    const float x = at.x;
    const float y = at.y;
    int best = -1;
    float best_distance = radius;
    for (const DrawnHandle& drawn_handle : trigger_handles().drawn_handles_) {
        const float distance =
            std::sqrt(((drawn_handle.x - x) * (drawn_handle.x - x)) + ((drawn_handle.y - y) * (drawn_handle.y - y)));
        if (distance <= best_distance) {
            best_distance = distance;
            best = drawn_handle.handle;
        }
    }
    return best;
}

void set_hover_handle(int handle) { trigger_handles().hover_handle_ = handle; }

bool begin_resize(int handle, V3 origin, V3 dir)
{
    HandleShape shape;
    if (!shape_for(stored_selected_node(), &shape) || handle < 0 || handle > kLastHandle) {
        return false;
    }
    const Added* added = shape.trigger ? alive_named(stored_selected_node()) : nullptr;
    ResizeDrag drag;
    drag.name = added ? added->spec.name : std::string();
    drag.node = stored_selected_node();
    drag.handle = handle;
    drag.pos0 = shape.pos;
    drag.size0 = shape.size;
    drag.yaw = shape.yaw;
    drag.proxim = !shape.trigger;
    drag.sphere = shape.sphere;
    drag.start = handle_point(shape.pos, shape.yaw, shape.size, handle);
    drag.direction = handle_direction({.yaw = shape.yaw, .handle = handle});
    if (!closest_on_axis({.origin = drag.start, .dir = drag.direction}, {.origin = origin, .dir = dir}, &drag.s0)) {
        return false; // (looking along the axis: no grip on it)
    }
    drag.on = true;
    resize_drag() = drag;
    note_undo(); // (the state before: one undo step, taken by the editor next frame)
    return true;
}

void rescale(V3 origin, V3 dir, bool snap)
{
    if (!resize_drag().on) {
        return;
    }
    float new_scale = 0.0F;
    if (!closest_on_axis({.origin = resize_drag().start, .dir = resize_drag().direction},
                         {.origin = origin, .dir = dir}, &new_scale)) {
        return;
    }
    float along = new_scale - resize_drag().s0;
    if (!std::isfinite(along) || std::fabs(along) > kMaxTriggerSize) {
        return; // (the ray nearly along the axis: far off)
    }
    const int axis = resize_drag().handle >> 1;
    const float size0 = axis_value(resize_drag().size0, axis);
    if (snap) {
        along = snap_to_grid(size0 + along, kResizeSnapGrid) - size0;
    }
    V3 pos{};
    V3 size{};
    resize_face(resize_drag().pos0, resize_drag().yaw, resize_drag().size0, {.handle = resize_drag().handle, .along = along}, {.new_pos_ = &pos, .new_size_ = &size});
    if (resize_drag().proxim) {
        // (a proximity node: a sphere's radius grows by what the face moved, about its middle; a box as a trigger's -
        // its middle kept as an offset from the node)
        const objects::ProximNode* proxim_node =
            is_proxim(resize_drag().node) ? objects::proxim_get_node(resize_drag().node) : nullptr;
        if (!proxim_node) {
            resize_drag().on = false;
            return;
        }
        ProximEdit proxim_edit = proxim_of(resize_drag().node) ? *proxim_of(resize_drag().node) : ProximEdit{};
        proxim_edit.node = resize_drag().node;
        proxim_edit.shape = true;
        proxim_edit.box = !resize_drag().sphere;
        if (resize_drag().sphere) {
            proxim_edit.radius = std::max(kProximMin, std::min(kProximMax, (size0 * kHalf) + along));
        } else {
            proxim_edit.size = size;
            proxim_edit.offset =
                pos -
                V3{.x = proxim_node->at_point().x, .y = proxim_node->at_point().y, .z = proxim_node->at_point().z};
        }
        set_proxim(proxim_edit, false);
        return;
    }
    const int index = spec_index(resize_drag().name);
    if (index < 0) {
        resize_drag().on = false;
        return;
    }
    Spec spec = core::at(level_editor_triggers_state().specs_, index);
    if (spec.size.x != size.x || spec.size.y != size.y || spec.size.z != size.z) {
        spec.size = size;
        set_spec(resize_drag().name, spec, false);
    }
    node_edit::Values values;
    if (node_edit::get_take_in(resize_drag().node, &values) &&
        (values.pos.x != pos.x || values.pos.y != pos.y || values.pos.z != pos.z)) {
        values.pos = pos;
        node_edit::assign_value(resize_drag().node, values);
    }
}

void end_resize() { resize_drag().on = false; }

bool resizing() { return resize_drag().on; }

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
    const bool any = std::ranges::any_of(level_editor_triggers_state().added_, [](const Added& a) { return a.alive && a.spec.exit; });
    if (editing || !any || trigger_edit().boxes_.empty() || layout::get_refused()) { // (a network session or a career-type mode)
        stored_seen().clear(); // (seen afresh once play goes on: a box moved while editing doesn't fire)
        return;
    }
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater) {
        return;
    }
    const V3 at = {.x = skater->at_point().x, .y = skater->at_point().y, .z = skater->at_point().z};
    for (const Added& added : level_editor_triggers_state().added_) {
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

// --- undo ---

State store_away()
{
    State state;
    state.specs = level_editor_triggers_state().specs_;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (spec.owned && spec_index(spec.name) >= 0) {
            state.nodes.push_back(spec);
        }
    }
    if (!level_editor_triggers_state().switched_.empty()) {
        state.walls = walls::store_away(level_editor_triggers_state().switched_);
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
    level_editor_triggers_state().specs_ = state.specs;
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
    const uint32_t wall = level_editor_triggers_state().select_wall_;
    level_editor_triggers_state().select_wall_ = 0;
    return wall;
}

// --- the layout ---

std::vector<Placed> capture_added()
{
    std::vector<Placed> out;
    for (const Spec& spec : level_editor_triggers_state().specs_) {
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
    if (!level_editor_triggers_state().scanned_) {
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
        level_editor_triggers_state().specs_.push_back(placed_trigger.spec);
        ++made;
    }
    get_sync();
    return made;
}

void reset_all()
{
    for (const Spec& spec : std::vector<Spec>(level_editor_triggers_state().specs_)) {
        node_create::Spec removed;
        int at = 0;
        node_create::drop_out(hash_of_name(spec.name), &removed, &at);
    }
    level_editor_triggers_state().specs_.clear();
    level_editor_triggers_state().switched_.clear();
    level_editor_triggers_state().inspected_ = 0;
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
    for (const Added& added : level_editor_triggers_state().added_) {
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
        level_editor_triggers_state().specs_.erase(level_editor_triggers_state().specs_.begin() + index);
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
    for (const Added& added : level_editor_triggers_state().added_) {
        alive += added.alive ? 1 : 0;
    }
    int off = 0;
    for (const LevelTrigger& level_trigger : level_editor_triggers_state().level_) {
        off += level_trigger.off ? 1 : 0;
    }
    std::array<char, kStatusTextSize> text{};
    text::format_to(text, "%u level trigger volumes (%d off), %d added", static_cast<unsigned>(level_editor_triggers_state().level_.size()), off,
                    alive);
    return text.data();
}

std::string describe_level_trigger(const LevelTrigger& level_trigger)
{
    return describe(level_trigger.calls, level_trigger.kind);
}

} // namespace frontend::level_editor::triggers
