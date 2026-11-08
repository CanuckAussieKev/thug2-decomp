// NOT ORIGINAL - the level editor's placing: the browser's entries, nodes and objects put into the level, drawing a
// chain point by point (ours). See level_editor.h.

#include "editor/level_editor.h"
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_adjust.h"
#include "base/core/format.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_ui.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr uint32_t kCrownColour = 0xFFFFC233;     // ARGB: the Place palette entries
constexpr uint32_t kLightColour = 0xFFFFE9A8;
constexpr uint32_t kSoundColour = 0xFF4FD1C5;
constexpr uint32_t kProximityColour = 0xFFF08A3C;

constexpr float kPlaceRaise = 150.0F; // a new spawn is put this far above the ground under it

constexpr float kRayReach = 600.0F;

} // namespace
namespace internal {

const std::vector<PlaceEntry>& place_entries()
{
    static std::vector<PlaceEntry> entries;
    if (!entries.empty()) {
        return entries;
    }
    using node_create::Kind;
    using node_create::Team;
    const auto teams = std::to_array<Team>({Team::kRed, Team::kBlue, Team::kGreen, Team::kYellow});
    entries.push_back({.kind = Kind::kSpawn, .team = Team::kRed, .label = "Multiplayer Spawn", .colour = nodes::spawn_colour(nodes::SpawnPointKind::kMultiplayer, 0)});
    for (Team team : teams) {
        const std::string colour = node_create::get_team_name(team);
        entries.push_back({.kind = Kind::kTeamSpawn,
                           .team = team,
                           .label = "Team spawn (" + colour + ")",
                           .colour = nodes::team_colour(("_" + colour).c_str())});
    }
    entries.push_back({.kind = Kind::kCrown, .team = Team::kRed, .label = "Crown", .colour = kCrownColour});
    for (Team team : teams) {
        const std::string colour = node_create::get_team_name(team);
        entries.push_back({.kind = Kind::kFlagSet,
                           .team = team,
                           .label = "CTF Flag (" + colour + ")",
                           .colour = nodes::team_colour(("_" + colour).c_str())});
    }
    for (Team team : teams) {
        const std::string colour = node_create::get_team_name(team);
        entries.push_back({.kind = Kind::kFlagSet,
                           .team = team,
                           .label = "Team Flag (" + colour + ")",
                           .colour = nodes::team_colour(("_" + colour).c_str()),
                           .lobby = true});
    }
    entries.push_back({.kind = Kind::kLight, .team = Team::kRed, .label = "Level light", .colour = kLightColour});
    entries.push_back({.kind = Kind::kSound, .team = Team::kRed, .label = "Sound Emitter", .colour = kSoundColour});
    entries.push_back({.kind = Kind::kProximityNode, .team = Team::kRed, .label = "Proximity Node", .colour = kProximityColour});
    return entries;
}

} // namespace internal
namespace {

nodes::Family family_of(node_create::Kind kind)
{
    switch (kind) {
    case node_create::Kind::kSpawn:
    case node_create::Kind::kTeamSpawn:
        return nodes::Family::kSpawn;
    case node_create::Kind::kCrown:
        return nodes::Family::kCrown;
    case node_create::Kind::kFlagSet:
        return nodes::Family::kBitMark;
    case node_create::Kind::kLight:
        return nodes::Family::kLight;
    case node_create::Kind::kSound:
        return nodes::Family::kSound;
    case node_create::Kind::kProximityNode:
        return nodes::Family::kFireOff;
    }
    return nodes::Family::kOther;
}

// Palette entry `entry` placed at `at` (a point the cursor or the view found): dropped to the ground below it as G
// drops a node (a light raised 150 units), facing the camera's heading; selected; one undo step.
bool place_entity(int entry, V3 at);

} // namespace
namespace internal {

void place_node(int entry, V3 at)
{
    if (place_entity(entry, at)) {
        return; // (the entities' entries - after the chains': level_editor_entities.h)
    }
    const std::vector<PlaceEntry>& entries = place_entries();
    if (entry < 0 || std::cmp_greater_equal(entry, entries.size())) {
        return;
    }
    const PlaceEntry& place_entry = core::at(entries, entry);
    if (const char* why = place_entry.lobby ? node_create::why_not_lobby_flag(place_entry.team)
                                            : node_create::get_why_not(place_entry.kind, place_entry.team)) {
        hud::get_message(why, hud::kMessageSeconds);
        return;
    }
    V3 ground{};
    if (ground_below(ground_ray_start(at, at.y, true), &ground)) {
        at = dropped_to(at, at.y, ground.y);
    }
    if (place_entry.kind == node_create::Kind::kLight) {
        at.y += kPlaceRaise;
    }
    std::string name = place_entry.lobby ? node_create::lobby_flag_name(place_entry.team)
                                         : node_create::pinned_node_name(place_entry.kind, place_entry.team);
    if (name.empty()) {
        name = node_create::fresh_name(node_create::name_stem(place_entry.kind), node_create::name_taken);
    }
    std::vector<node_create::Spec> specs = {
        node_create::default_spec(place_entry.kind, place_entry.team, at, fly_camera().yaw_, name)};
    if (place_entry.kind == node_create::Kind::kFlagSet &&
        !place_entry.lobby) { // (a lobby flag has no base - as the levels' own)
        specs.push_back(node_create::current_base(specs.front()));
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kAdded;
    step.exist = false; // (undone: they're removed)
    std::string message;
    for (const node_create::Spec& spec : specs) {
        if (!node_create::fabricate(spec, -1, &message)) {
            break;
        }
        step.added.push_back(spec);
        step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), spec.name));
    }
    if (step.added.empty()) {
        hud::get_message(message, hud::kMessageSeconds);
        return;
    }
    push_undo(step, "Place node");
    drawn_view().pickables_.clear();
    picked_set().hover_node_ = -1;
    const int index = nodes::get_find(node_create::name_hash(specs.front().name));
    if (index >= 0) {
        select_node_only(index);
    }
    note_online_change(family_of(place_entry.kind), specs.front().name.c_str(), "added");
    hud::get_message("Added " + specs.front().name);
    adjust::ActStep placed; // (level_editor_adjust.h)
    placed.kind = adjust::Kind::kPlace;
    adjust::get_note(placed);
    note_action("added %s (%s)", specs.front().name.c_str(), place_entry.label.c_str());
}

} // namespace internal
namespace {

// The Place palette's chain entries (after the node kinds): a new rail, a new path, a new ladder.
constexpr int kChainEntries = 4; // (the feature map's ledge the fourth)
int chain_entry(int entry)
{
    const int base = static_cast<int>(place_entries().size());
    return entry >= base && entry < base + kChainEntries ? entry - base : -1;
}
// The Place palette's entities (after the chains): peds, cars, bouncy sets, particle systems, sound emitters
// (level_editor_entities.h). Placed at `at` (dropped to the ground below it): copies (one undo step, as the browser's)
// or a sound emitter with its sound (an added node: its undo step). False: not one of those entries.
bool place_entity(int entry, V3 at)
{
    const int index = entry - static_cast<int>(place_entries().size()) - kChainEntries;
    if (entry < 0 || index < 0 || std::cmp_greater_equal(index, entities::get_palette().size())) {
        return false;
    }
    const entities::PaletteEntry& palette_entry = core::at(entities::get_palette(), index);
    if (!palette_entry.why.empty()) {
        hud::get_message(palette_entry.why, hud::kMessageSeconds);
        return true;
    }
    V3 ground{};
    if (ground_below(ground_ray_start(at, at.y, true), &ground)) {
        at = dropped_to(at, at.y, ground.y);
    }
    entities::Placed placed = entities::get_place(index, at, fly_camera().yaw_);
    if (placed.emitter) {
        const std::string name =
            node_create::fresh_name(node_create::name_stem(node_create::Kind::kSound), node_create::name_taken);
        const node_create::Spec spec =
            node_create::default_spec(node_create::Kind::kSound, node_create::Team::kRed, at, fly_camera().yaw_, name);
        std::string message;
        if (!node_create::fabricate(spec, -1, &message)) {
            hud::get_message(message, hud::kMessageSeconds);
            return true;
        }
        entities::attach_sound({.emitter = node_create::name_hash(name), .sound = placed.sound});
        UndoStep step;
        step.kind = UndoStep::Kind::kAdded;
        step.exist = false;
        step.added.push_back(spec);
        step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), name));
        push_undo(step, "Place");
        drawn_view().pickables_.clear();
        picked_set().hover_node_ = -1;
        const int node = nodes::get_find(node_create::name_hash(name));
        if (node >= 0) {
            select_node_only(node);
        }
        hud::get_message("Added " + name + " (" + nodes::caption(placed.sound) + ")");
        note_action("added the sound emitter %s (%s)", name.c_str(), nodes::caption(placed.sound).c_str());
        return true;
    }
    if (placed.copies.empty()) {
        hud::get_message(placed.message.empty() ? std::string("Could not place") : placed.message, hud::kMessageSeconds);
        return true;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    std::vector<objects::GameEntity*> made;
    for (const uint32_t name : placed.copies) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
        if (!object) {
            continue;
        }
        uint32_t source = 0;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == name) {
                source = copy.source;
            }
        }
        step.copies.push_back({.name = name, .source = source, .state = object_edit::store_away(object)});
        OrientedBox box{};
        if (object_edit::model_box(object, &box)) {
            made.push_back(object);
        }
    }
    push_undo(step, "Place");
    if (!made.empty()) {
        select_objects(made);
    } else if (const int node = nodes::get_find(placed.copies.front()); node >= 0) {
        drawn_view().pickables_.clear();
        select_node_only(node); // (a particle system: its node)
    }
    hud::get_message(placed.message);
    note_action("%s", placed.message.c_str());
    adjust::ActStep adjust; // (level_editor_adjust.h)
    adjust.kind = adjust::Kind::kPlace;
    adjust::get_note(adjust);
    return true;
}

} // namespace
namespace internal {

bool place_palette_entity(int index, V3 at)
{
    return place_entity(static_cast<int>(place_entries().size()) + kChainEntries + index, at);
}

chains::Blit chain_kind_of(int entry)
{
    switch (chain_entry(entry)) {
    case 0:
        return chains::Blit::kRail;
    case 1:
        return chains::Blit::kRouteWay;
    case 2:
        return chains::Blit::kLadder;
    case 3:
        return chains::Blit::kLedge;
    default:
        return chains::Blit::kNone;
    }
}

// A click while drawing: a point where the cursor's ray meets the level (edges snapped unless Alt; a free end near it
// taken), linked on from the last.
void draw_chain_point(V3 dir, bool alt)
{
    if (chains::get_drawing() == chains::Blit::kLadder) {
        // (a ladder: its foot on a wall, then its top - level_editor_chains.h)
        const chains::State before = chains::at_hand();
        std::string message;
        uint32_t select = 0;
        const bool made = chains::ladder_click(fly_camera().pos_, make_unit(dir), kMaxPickDistance, &message, &select);
        after_chain_change(before, made ? select : 0, message);
        if (made && chain_kind_of(place_palette().place_armed_) != chains::Blit::kNone) {
            place_palette().place_armed_ = -1;
        }
        return;
    }
    V3 point{};
    uint32_t end = 0;
    if (!chains::magnet_point(fly_camera().pos_, make_unit(dir), kMaxPickDistance, alt, &point, &end)) {
        hud::get_message("No surface under the cursor");
        return;
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    chains::draw_at(point, end, &message, &select);
    // (nothing selected while drawing: the camera keeps flying - a selection would be orbited)
    unpick();
    after_chain_change(before, 0, message);
    if (chains::get_drawing() == chains::Blit::kNone &&
        chain_kind_of(place_palette().place_armed_) != chains::Blit::kNone) {
        place_palette().place_armed_ = -1; // (linked onto an end: done)
    }
}

// Drawing finished (Enter, a right click) or an armed pick / drawing cancelled (Esc).
void stop_chain_drawing(const char* message)
{
    std::string text;
    uint32_t select = 0;
    chains::apply_op(chains::ChainOp::kCancel, 0, 0, &text, &select);
    if (chain_kind_of(place_palette().place_armed_) != chains::Blit::kNone) {
        place_palette().place_armed_ = -1;
    }
    if (message) {
        hud::get_message(message);
    }
}

// The selected node deleted: an added one removed (undoable); a level's own hidden - after the confirm window when the
// UI runs (`confirmed`), at once with the old HUD.
void unlink_list_node(bool confirmed)
{
    const int index = picked_node_index();
    if (index < 0) {
        return;
    }
    const nodes::Elem node = core::at(nodes::get_all(), index);
    // A rail or path point: deleted from its chain, its neighbours linked (a level's own hidden - level_editor_chains.h).
    if (node.family == nodes::Family::kRail || node.family == nodes::Family::kRouteWay ||
        node.family == nodes::Family::kLadder) { // (a ladder: whole)
        run_chain_op(chains::ChainOp::kDelete, 0);
        return;
    }
    // A copy selected by its node (a particle system's - level_editor_entities.h): deleted as the object copies are.
    if (object_edit::is_copy(node.name)) {
        UndoStep step;
        step.kind = UndoStep::Kind::kMade;
        step.exist = true; // (undone: made again)
        uint32_t source = 0;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == node.name) {
                source = copy.source;
            }
        }
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(node.name));
        if (!object || ((object->object_flags() & objects::kObjectFlagDead) != 0U)) {
            return;
        }
        step.copies.push_back({.name = node.name, .source = source, .state = object_edit::store_away(object)});
        object_edit::remove_copy(node.name);
        push_undo(step, "Delete copy");
        picked_set().node_selected_ = false;
        drawn_view().pickables_.clear();
        picked_set().hover_node_ = -1;
        hud::get_message("Deleted");
        note_action("deleted the copy %s", nodes::caption(node.name).c_str());
        return;
    }
    if (node.added) {
        UndoStep step;
        step.kind = UndoStep::Kind::kAdded;
        step.exist = true; // (undone: made again)
        node_create::Spec spec;
        int at = 0;
        if (!node_create::drop_out(node.name, &spec, &at)) {
            return;
        }
        step.added.push_back(spec);
        step.added_at_.push_back(at);
        push_undo(step, "Delete node");
        picked_set().node_selected_ = false;
        drawn_view().pickables_.clear();
        picked_set().hover_node_ = -1;
        note_online_change(node.family, spec.name.c_str(), "deleted");
        hud::get_message("Deleted " + spec.name);
        note_action("deleted the added node %s", spec.name.c_str());
        return;
    }
    if (node.hidden) {
        hud::get_message("Already hidden");
        return;
    }
    const char* why = nullptr;
    if (!node_create::can_hide(node, &why)) {
        hud::get_message(why ? why : "Can't be hidden", hud::kMessageSeconds);
        return;
    }
    if (ui::underway() && !confirmed) {
        confirmations().confirm_hide_ = node.name;
        return;
    }
    if (!node_create::conceal(node.name, true)) {
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kHidden;
    step.hidden_names_ = {node.name};
    step.hidden = false; // (undone: shown)
    push_undo(step, "Hide node");
    note_online_change(node.family, nodes::caption(node.name).c_str(), "hidden");
    hud::get_message("Hidden " + nodes::caption(node.name));
    note_action("hid the level's node %s", nodes::caption(node.name).c_str());
}

// Home on a hidden node: shown again (undoable).
bool show_hidden_node()
{
    const int index = picked_node_index();
    if (index < 0 || !core::at(nodes::get_all(), index).hidden) {
        return false;
    }
    const uint32_t name = core::at(nodes::get_all(), index).name;
    if (chains::is_chain_node(name)) {
        // (a deleted rail / path point: its neighbours were linked past it - the chains' undo puts both back)
        hud::get_message("Deleted points can be restored with Undo");
        return true;
    }
    if (!node_create::conceal(name, false)) {
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kHidden;
    step.hidden_names_ = {name};
    step.hidden = true; // (undone: hidden again)
    push_undo(step, "Show node");
    hud::get_message("Shown " + nodes::caption(name));
    note_action("showed the level's node %s again", nodes::caption(name).c_str());
    return true;
}

// An added crown earlier / later among the added crowns (one undo step).
void move_selected_crown(int delta)
{
    const int index = picked_node_index();
    if (index < 0) {
        return;
    }
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    UndoStep step;
    step.kind = UndoStep::Kind::kOrder;
    step.order = node_create::get_order();
    std::string message;
    const uint32_t name = node.name;
    const nodes::Family family = node.family;
    if (!node_create::move_crown(checksums::Name{name}, delta, &message)) {
        hud::get_message(message, hud::kMessageSeconds);
        return;
    }
    push_undo(step, "Crown order");
    const int now = nodes::get_find(name);
    if (now >= 0) {
        select_node_only(now);
        std::array<char, kCaptionTextSize> text{};
        text::format_to(text, "Now crown #%d", core::at(nodes::get_all(), now).order);
        hud::get_message(text.data());
    }
    note_online_change(family, nodes::caption(name).c_str(), "reordered");
}

// Where the cursor points: the ground below the point its ray first meets the level (a wall: the floor at its foot;
// a roof: the roof) - none: 600 units ahead, on the ground below that if any.
V3 cursor_point(V3 dir, bool have_ray, const TickCycle& frame)
{
    V3 hit{};
    V3 ground{};
    const V3 ray = have_ray ? make_unit(dir) : frame.back * -1.0F;
    if (get_cast(fly_camera().pos_, fly_camera().pos_ + ray * kMaxPickDistance, &hit)) {
        const V3 back = hit - ray * 5.0F + V3{.x = 0.0F, .y = 5.0F, .z = 0.0F};
        return ground_below(back, &ground) ? ground : hit;
    }
    V3 ahead = fly_camera().pos_ + ray * kRayReach;
    if (ground_below(ahead, &hit)) {
        return hit;
    }
    return ahead;
}

void place_from_browser(V3 at, bool keep_open)
{
    if (std::cmp_greater_equal(stored_browser().index, stored_browser().items.size())) {
        return;
    }
    const uint32_t source = core::at(stored_browser().items, stored_browser().index).id;
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    if (stored_browser().tab == 0) {
        const uint32_t copy = scenery::get_duplicate(source);
        if (copy == 0U) {
            hud::get_message("Couldn't copy the piece");
            return;
        }
        picked_set().objects_.clear();
        scenery::set_selection({copy});
        const V3 pivot = scenery::get_pivot();
        scenery::move_to({.x = at.x, .y = at.y + (pivot.y - scenery::get_bottom()), .z = at.z});
        scenery::finalise();
        step.sectors.push_back(copy);
    } else {
        objects::GameEntity* object = object_edit::make_copy(source, at);
        if (!object) {
            hud::get_message("Couldn't copy the object");
            return;
        }
        select_objects({object});
        step.copies.push_back({.name = object->ident(), .source = source, .state = object_edit::store_away(object)});
    }
    push_undo(step, "Place copy");
    thumbs::note_used(stored_browser().tab, source); // (the palette's Recently used - level_editor_thumbs.h)
    adjust::ActStep placed;                   // (level_editor_adjust.h: where it went, its turn)
    placed.kind = adjust::Kind::kPlace;
    placed.quarters = stored_browser().tab == 0;
    adjust::get_note(placed);
    if (!keep_open) { // (Shift+click with the arrange tools' Click places: more of it - level_editor_arrange.h)
        stored_browser().open = false;
        hud::hide_panel();
    }
    hud::get_message("Placed " + object_edit::caption(source));
    note_action("placed a copy of %s (the browser)", object_edit::caption(source).c_str());
}

} // namespace internal
} // namespace frontend::level_editor
