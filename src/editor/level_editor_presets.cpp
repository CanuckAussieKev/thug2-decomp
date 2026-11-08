// NOT ORIGINAL - the level editor's moving-object presets: a car or a ped on a path in one click (ours). See
// level_editor_presets.h.

#include "editor/level_editor_presets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible as it was.
// (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor::presets {

namespace {

constexpr float kLoopSide = 1000.0F;   // the path's square: this long a side
constexpr float kGroundProbeLift = 200.0F; // each waypoint dropped to the ground from this far above
constexpr int kLoopPoints = 4;
constexpr const char* kLikeMark = "  like "; // (the palette's label: "... like <source node>")

// The source node a palette entry copies (its label's "like <name>"; 0: none read).
uint32_t source_of(const entities::PaletteEntry& entry)
{
    const size_t at = entry.label.rfind(kLikeMark);
    uint32_t name = 0;
    if (at == std::string::npos || !fields::parse_name(entry.label.substr(at + std::char_traits<char>::length(kLikeMark)), &name)) {
        return 0;
    }
    return name;
}

// A node's first link that is a path's waypoint (0: none).
uint32_t first_waypoint(uint32_t name)
{
    const int index = nodes::get_find(name);
    if (index < 0) {
        return 0;
    }
    const std::vector<nodes::Elem>& all = nodes::get_all();
    for (const int link : core::at(all, index).links) {
        if (link >= 0 && std::cmp_less(link, all.size()) && core::at(all, link).family == nodes::Family::kRouteWay) {
            return core::at(all, link).name;
        }
    }
    return 0;
}

// The palette entry to copy: the first car / ped (`vehicle`) that can be placed, one whose source follows a path
// first (a walking ped, a driving car). -1: the level has none.
int pick_entry(bool vehicle)
{
    const char* prefix = vehicle ? "Car: " : "Ped: ";
    const std::vector<entities::PaletteEntry>& palette = entities::get_palette();
    int first = -1;
    for (size_t i = 0; i < palette.size(); ++i) {
        const entities::PaletteEntry& entry = core::at(palette, i);
        if (!entry.label.starts_with(prefix) || !entry.why.empty()) {
            continue;
        }
        if (first_waypoint(source_of(entry)) != 0U) {
            return static_cast<int>(i);
        }
        if (first < 0) {
            first = static_cast<int>(i);
        }
    }
    return first;
}

V3 on_ground(V3 point)
{
    V3 ground{};
    if (ground_below(point + V3{.x = 0.0F, .y = kGroundProbeLift, .z = 0.0F}, &ground)) {
        point.y = ground.y;
    }
    return point;
}

} // namespace

void add_on_path(bool vehicle)
{
    const int entry = pick_entry(vehicle);
    if (entry < 0) {
        hud::get_message(vehicle ? "This level has no cars to copy" : "This level has no peds to copy",
                         hud::kMessageSeconds);
        return;
    }
    // Where the view looks (the first surface along it).
    const TickCycle view = fly_camera().frame();
    const V3 forward = make_unit(view.back * -1.0F);
    V3 at{};
    uint32_t end = 0;
    if (!chains::magnet_point(fly_camera().pos_, forward, kMaxPickDistance, true, &at, &end)) {
        hud::get_message("Look at the ground to place it", hud::kMessageSeconds);
        return;
    }
    const size_t copies_before = object_edit::get_copies().size();
    if (!place_palette_entity(entry, at) || object_edit::get_copies().size() <= copies_before) {
        return; // (the palette said why)
    }
    const object_edit::DupFrom copy = object_edit::get_copies().back();
    if (nodes::get_find(copy.name) < 0) {
        hud::get_message("Placed, but its node wasn't found: assign a path in the Inspector", hud::kMessageSeconds);
        return;
    }
    // The loop: ahead and to the right of the copy, flat, each corner on the ground.
    const V3 ahead = make_unit(V3{.x = forward.x, .y = 0.0F, .z = forward.z}) * kLoopSide;
    const V3 right = V3{.x = ahead.z, .y = 0.0F, .z = -ahead.x};
    const std::array<V3, kLoopPoints> corners = {
        {on_ground(at), on_ground(at + ahead), on_ground(at + ahead + right), on_ground(at + right)}};
    const chains::State before = chains::at_hand();
    chains::State state = before;
    const uint32_t like = first_waypoint(copy.source);
    std::vector<std::string> names;
    names.reserve(kLoopPoints);
    for (int i = 0; i < kLoopPoints; ++i) {
        names.push_back(node_create::fresh_name("ed_path", [&](const std::string& name) {
            return node_create::name_taken(name) ||
                   std::ranges::any_of(names, [&](const std::string& made) { return made == name; }) ||
                   std::ranges::any_of(state.points, [&](const chains::Point& point) { return point.name == name; });
        }));
    }
    state.points.reserve(state.points.size() + kLoopPoints);
    for (int i = 0; i < kLoopPoints; ++i) {
        chains::Point point;
        point.name = core::at(names, i);
        point.cls = "Waypoint";
        point.pos = core::at(corners, i);
        point.like = like;
        point.links = {node_create::name_hash(core::at(names, (i + 1) % kLoopPoints))};
        state.points.push_back(point);
    }
    // The copy's links= its path's first waypoint (its links to anything else kept), as Assign Path sets them.
    std::vector<uint32_t> links = {node_create::name_hash(names.front())};
    const std::vector<nodes::Elem>& all = nodes::get_all();
    for (const int link : core::at(all, nodes::get_find(copy.name)).links) {
        if (link >= 0 && std::cmp_less(link, all.size()) && core::at(all, link).family != nodes::Family::kRouteWay) {
            links.push_back(core::at(all, link).name);
        }
    }
    std::erase_if(state.links, [&](const chains::LinkEdit& edit) { return edit.name == copy.name; });
    const auto place = std::ranges::lower_bound(state.links, copy.name, {}, &chains::LinkEdit::name);
    state.links.insert(place, chains::LinkEdit{.name = copy.name, .links = links});
    if (!chains::change_phase(state)) {
        hud::get_message("Couldn't make its path", hud::kMessageSeconds);
        return;
    }
    after_chain_change(before, copy.name, vehicle ? "Car on a path added" : "Ped on a path added");
}

} // namespace frontend::level_editor::presets
