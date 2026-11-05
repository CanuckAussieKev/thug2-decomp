// NOT ORIGINAL - the level editor's chain operations: insert, delete, unlink, terrain, speed and the rest, and
// drawing a chain with the mouse (ours). See level_editor_chains.h.

#include "editor/level_editor_chains.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <initializer_list>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "engine/render/probe_ray.h"
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
namespace {

constexpr std::size_t kFacingMessageSize = 96;
constexpr std::size_t kListTextSize = 160;
constexpr std::size_t kMessageTextSize = 128;

constexpr float kDropProbeDepth = 70.0F;
constexpr float kEdgeSnapReach = 32.0F; // a point snaps to an edge within this
constexpr float kEdgeProbeDepth = 60.0F;
constexpr float kJoinReach = 24.0F; // a free chain end within this is joined

} // namespace
namespace internal {

// The chain graph of a family's nodes (as their links= are now): rails (RailNode), paths (Waypoint).
Graph graph_of(nodes::Family family)
{
    Graph graph;
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    for (const nodes::Elem& node : nodes) {
        if (node.family != family || (node.name == 0U) || node.hidden) {
            continue;
        }
        std::vector<uint32_t> links;
        for (const int link : node.links) {
            if (link >= 0 && std::cmp_less(link, nodes.size()) && core::at(nodes, link).family == family &&
                !core::at(nodes, link).hidden) {
                links.push_back(core::at(nodes, link).name);
            }
        }
        graph[node.name] = links;
    }
    return graph;
}

nodes::Family family_of_name(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 ? core::at(nodes::get_all(), index).family : nodes::Family::kOther;
}

} // namespace internal
namespace {

V3 place_of(uint32_t name)
{
    node_edit::Values values;
    if (node_edit::get_take_in(name, &values)) {
        return values.pos;
    }
    const int index = nodes::get_find(name);
    return index >= 0 ? core::at(nodes::get_all(), index).pos : V3{};
}

// A level node's links in the state: an edit, or none when they're as the level had them.
void set_link_edit(State* state, uint32_t name, const std::vector<uint32_t>& links)
{
    auto it = std::ranges::find_if(state->links, [&](const LinkEdit& link_edit) { return link_edit.name == name; });
    if (links == level_links_original(name)) {
        if (it != state->links.end()) {
            state->links.erase(it);
        }
    } else if (it != state->links.end()) {
        it->links = links;
    } else {
        state->links.push_back({.name = name, .links = links});
        sort_by_name(&state->links);
    }
}

// The graph's links put into the state: an added point's on its point, a level node's as a links edit (none when as
// the level had them).
void put_graph(State* state, const Graph& graph)
{
    for (const auto& entry : graph) {
        bool point = false;
        for (Point& chain_point : state->points) {
            if (name_hash(chain_point.name) == entry.first) {
                chain_point.links = entry.second;
                point = true;
            }
        }
        if (point) {
            continue;
        }
        // (a level node: only the links within the family are in the graph - others it has are kept)
        std::vector<uint32_t> links = entry.second;
        const nodes::Family family = family_of_name(entry.first);
        for (const uint32_t other : level_links_now(entry.first)) {
            if (family_of_name(other) != family && std::ranges::find(links, other) == links.end()) {
                links.push_back(other);
            }
        }
        set_link_edit(state, entry.first, links);
    }
}

// A new point's name: ed_rail01 / ed_path01... free in the level and in the state.
std::string fresh_point_name(nodes::Family family, const State& state, bool ledge = false)
{
    const char* stem = "ed_rail";
    if (family == nodes::Family::kRouteWay) {
        stem = "ed_path";
    } else if (ledge) {
        stem = "ed_ledge";
    } else if (family == nodes::Family::kLadder) {
        stem = "ed_ladder";
    }
    return node_create::fresh_name(stem, [&](const std::string& name) {
        for (const Point& chain_point : state.points) {
            if (same_text(chain_point.name, name.c_str())) {
                return true;
            }
        }
        return node_create::name_taken(name);
    });
}

// A new point drawn on from `from` (0: a new chain of `family`): its class, and the fields it copies.
Point append_rail_vertex(nodes::Family family, uint32_t from, V3 pos, const State& state)
{
    // (a ledge: drawn as one, or drawn on from one)
    const bool ledge = family == nodes::Family::kLadder &&
                       (level_editor_chains_state().draw_ == Blit::kLedge || ((from != 0U) && is_ledge_node(from)));
    Point chain_point;
    chain_point.name = fresh_point_name(family, state, ledge);
    if (family == nodes::Family::kRouteWay) {
        chain_point.cls = "Waypoint";
    } else if (family == nodes::Family::kLadder) {
        chain_point.cls = "ClimbingNode";
    } else {
        chain_point.cls = "RailNode";
    }
    chain_point.pos = pos;
    if (ledge) {
        chain_point.type = "Ledge";
    }
    if (from != 0U) {
        chain_point.like = from;
        bool added = false;
        for (const Point& source_point : state.points) {
            if (name_hash(source_point.name) == from) {
                // (an added point: its own template and terrain - the layout makes them in one go - its cluster and
                // whether it's grindable on flat ground; a ledge's hang side)
                chain_point.like = source_point.like;
                chain_point.terrain = source_point.terrain;
                chain_point.cluster = source_point.cluster;
                chain_point.grind_flat_ = source_point.grind_flat_;
                chain_point.hang = source_point.hang;
                added = true;
            }
        }
        if (!added && family == nodes::Family::kRail) {
            // (a level rail's point: its cluster when it's a trick object, as the level registers it)
            script::Struct* node = look_up_node_params(from);
            static const uint32_t kTrickObject = name_hash("TrickObject");
            static const uint32_t kCluster = name_hash("Cluster");
            if (node && node->has_entry_named(kTrickObject)) {
                chain_point.cluster = from;
                node->read_hash_value(kCluster, &chain_point.cluster, 0);
            }
            chain_point.grind_flat_ = std::ranges::find(state.grind_flat_, from) != state.grind_flat_.end();
        }
    } else if (family == nodes::Family::kRail) {
        // (a new rail: a trick object of the scenery piece the click hit, as the levels name their rails' clusters -
        // else of its own)
        chain_point.cluster = (level_editor_chains_state().snap_sector_ != 0U)
                                  ? level_editor_chains_state().snap_sector_
                                  : name_hash(chain_point.name);
    }
    return chain_point;
}

bool get_apply(const State& state, std::string* message, const char* done)
{
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    *message = done;
    return true;
}

// A ladder point's heading set in the state: an added point's facing, a level point's `facing` edit (none when as the
// level has it).
void set_facing(State* state, uint32_t name, float facing)
{
    for (Point& chain_point : state->points) {
        if (name_hash(chain_point.name) == name) {
            chain_point.facing = facing;
            return;
        }
    }
    auto it =
        std::ranges::find_if(state->facings, [&](const FacingEdit& facing_edit) { return facing_edit.name == name; });
    if (same_facing(facing, facing_original(name))) {
        if (it != state->facings.end()) {
            state->facings.erase(it);
        }
    } else if (it != state->facings.end()) {
        it->facing = facing;
    } else {
        state->facings.push_back({.name = name, .facing = facing});
        sort_by_name(&state->facings);
    }
}

// The operations on a ladder (its two points - or a level's climbing chain - together).
bool ladder_op(ChainOp op, uint32_t name, const nodes::Elem& node, uint32_t arg, std::string* message, uint32_t* select)
{
    if (node.hidden) {
        *message = "Deleted";
        return false;
    }
    State state = at_hand();
    std::vector<uint32_t> points = chain_of(name);
    if (node.added) {
        // (an added ladder: its points by their links too - the rail manager's chain may not have them yet)
        Graph graph;
        for (const Point& chain_point : state.points) {
            if (same_text(chain_point.cls, "ClimbingNode")) {
                graph[name_hash(chain_point.name)] = chain_point.links;
            }
        }
        for (const uint32_t point : get_connected(graph, name)) {
            if (std::ranges::find(points, point) == points.end()) {
                points.push_back(point);
            }
        }
    }
    switch (op) {
    case ChainOp::kDelete: {
        bool level = false;
        for (uint32_t point : points) {
            auto added = std::ranges::find_if(
                state.points, [&](const Point& chain_point) { return name_hash(chain_point.name) == point; });
            if (added != state.points.end()) {
                state.points.erase(added);
                continue;
            }
            level = true;
            if (std::ranges::find(state.dropped, point) == state.dropped.end()) {
                state.dropped.push_back(point);
            }
            std::erase_if(state.facings, [&](const FacingEdit& facing_edit) { return facing_edit.name == point; });
        }
        std::ranges::sort(state.dropped);
        return get_apply(state, message, level ? "Ladder hidden" : "Ladder deleted");
    }
    case ChainOp::kTurn: {
        const int notches = static_cast<int>(arg);
        const float facing =
            node_edit::wrap_angle(facing_now(name) + (static_cast<float>(notches) * (15.0F * 3.14159265F / 180.0F)));
        for (const uint32_t point : points) {
            set_facing(&state, point, facing);
        }
        *select = name;
        std::array<char, kFacingMessageSize> text{};
        text::format_to(text, "Ladder facing %.0f degrees",
                        static_cast<double>(node_edit::get_heading_degrees({.x = 0.0F, .y = facing, .z = 0.0F})));
        const std::string done = text.data();
        return get_apply(state, message, done.c_str());
    }
    case ChainOp::kFacingHome: {
        bool any = false;
        for (uint32_t point : points) {
            auto it = std::ranges::find_if(state.facings,
                                           [&](const FacingEdit& facing_edit) { return facing_edit.name == point; });
            if (it != state.facings.end()) {
                state.facings.erase(it);
                any = true;
            }
        }
        if (!any) {
            return false; // (nothing to put back - no message: Home's own says what it did)
        }
        *select = name;
        return get_apply(state, message, "Ladder facing as the level has it");
    }
    default:
        *message = "Ladder selected";
        return false;
    }
}

} // namespace

std::vector<uint32_t> chain_of(uint32_t name)
{
    const nodes::Family family = family_of_name(name);
    if (family == nodes::Family::kRail || family == nodes::Family::kLadder) {
        const std::vector<uint32_t> rail = scenery::rail_of(name);
        return rail.empty() ? std::vector<uint32_t>{name} : rail;
    }
    if (family == nodes::Family::kRouteWay) {
        return get_connected(graph_of(family), name);
    }
    return {name};
}

namespace {

// What a chain operation works on: the operation, the point (its node, family, whether a ledge's) and its
// argument; where its message and the point to select go.
struct ChainCall {
    ChainOp op{ChainOp::kCancel};
    uint32_t name{};
    uint32_t arg{};
    const nodes::Elem* node{};
    nodes::Family family{};
    bool ledge{};
    std::string* message{};
    uint32_t* select{};
};

// A point put halfway to the next one.
bool insert_point(const ChainCall& call, State& state, Graph& graph)
{
    const uint32_t name = call.name;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    const std::vector<uint32_t>& links = graph[name];
    if (links.empty()) {
        *message = "Last point: use Extend";
        return false;
    }
    const V3 point_a = place_of(name);
    const V3 point_b = place_of(links.front());
    const Point chain_point = append_rail_vertex(family, name, (point_a + point_b) * 0.5F, state);
    const uint32_t made = name_hash(chain_point.name);
    state.points.push_back(chain_point);
    insert_after(&graph, name, made);
    put_graph(&state, graph);
    *select = made;
    return get_apply(state, message, "A point put halfway to the next (Ctrl+Z takes it away)");
}

// The point deleted (an added one) or hidden (the level's).
bool delete_point(const ChainCall& call, State& state, Graph& graph)
{
    const uint32_t name = call.name;
    const nodes::Elem& node = *call.node;
    std::string* message = call.message;
    drop_out(&graph, name);
    put_graph(&state, graph);
    if (node.added) {
        std::erase_if(state.points, [&](const Point& chain_point) { return name_hash(chain_point.name) == name; });
    } else {
        state.dropped.push_back(name);
        std::ranges::sort(state.dropped);
        // (its own links stay as the level has them: it takes no part while dropped)
        std::erase_if(state.links, [&](const LinkEdit& link_edit) { return link_edit.name == name; });
    }
    return get_apply(state, message, node.added ? "Point deleted" : "Point hidden");
}

// The point unlinked from its next one: the chain split there.
bool unlink_point(const ChainCall& call, State& state, Graph& graph)
{
    const uint32_t name = call.name;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (!uncouple(&graph, name)) {
        *message = "It has no next point";
        return false;
    }
    put_graph(&state, graph);
    *select = name;
    return get_apply(state, message, "Unlinked from its next point: the chain is split there");
}

// The rail's terrain (every point of it).
bool set_terrain(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails have a terrain";
        return false;
    }
    std::string terrain;
    for (const std::string& text : grind_terrain_names()) {
        if (name_hash(text) == arg) {
            terrain = text;
        }
    }
    if (terrain.empty() || !known_terrain(arg)) {
        *message = "Unknown terrain";
        return false;
    }
    for (uint32_t point : chain_of(name)) {
        bool added = false;
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.terrain = terrain;
                added = true;
            }
        }
        if (added) {
            continue;
        }
        auto it = std::ranges::find_if(state.terrains,
                                       [&](const TerrainEdit& terrain_edit) { return terrain_edit.name == point; });
        if (it != state.terrains.end()) {
            it->terrain = terrain;
        } else {
            state.terrains.push_back({.name = point, .terrain = terrain});
        }
    }
    sort_by_name(&state.terrains);
    *select = name;
    return get_apply(state, message, ("The rail's terrain is " + terrain + " now").c_str());
}

// Drawing on from an end point.
bool extend_chain(const ChainCall& call, Graph& graph)
{
    const uint32_t name = call.name;
    const nodes::Family family = call.family;
    const bool ledge = call.ledge;
    std::string* message = call.message;
    if (!is_last(graph, name) && !is_first(graph, name)) {
        *message = "Select an end point to extend";
        return false;
    }
    if (family == nodes::Family::kRouteWay) {
        level_editor_chains_state().draw_ = Blit::kRouteWay;
    } else {
        level_editor_chains_state().draw_ = ledge ? Blit::kLedge : Blit::kRail;
    }
    level_editor_chains_state().draw_last_ = name;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    *message = "Click to add points (Enter: finish)";
    return false;
}

// The drawn points of the rail one trick object (a cluster) or none.
bool set_trick_object(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails can be trick objects";
        return false;
    }
    // (the drawn points of the rail: one cluster - the one they have, else the scenery piece's or the first's)
    std::vector<uint32_t> rail = chain_of(name);
    uint32_t cluster = 0;
    for (const Point& chain_point : state.points) {
        if (std::ranges::find(rail, name_hash(chain_point.name)) != rail.end() && (cluster == 0U)) {
            cluster = chain_point.cluster;
        }
    }
    bool any = false;
    for (Point& chain_point : state.points) {
        if (std::ranges::find(rail, name_hash(chain_point.name)) == rail.end()) {
            continue;
        }
        if (cluster == 0U) {
            cluster = name_hash(chain_point.name);
        }
        chain_point.cluster = (arg != 0U) ? cluster : 0;
        any = true;
    }
    if (!any) {
        *message = "Original rails keep their trick object setting";
        return false;
    }
    *select = name;
    return get_apply(state, message, (arg != 0U) ? "Trick object on" : "Trick object off");
}

// The rail grindable on flat ground, or not.
bool set_grind_flat(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Elem& node = *call.node;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails are ground";
        return false;
    }
    for (const uint32_t point : chain_of(name)) {
        bool added = false;
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.grind_flat_ = arg != 0;
                added = true;
            }
        }
        if (added) {
            continue;
        }
        auto it = std::ranges::find(state.grind_flat_, point);
        if ((arg != 0U) && it == state.grind_flat_.end()) {
            state.grind_flat_.push_back(point);
        } else if ((arg == 0U) && it != state.grind_flat_.end()) {
            state.grind_flat_.erase(it);
        }
    }
    std::ranges::sort(state.grind_flat_);
    *select = name;
    if (arg != 0U) {
        get_log("level editor: %s's rail grindable on flat ground - RailClear (0x0057C600) passes it as the park "
                "editor's level passes every rail; the game itself has no node flag for this",
                object_edit::caption(name).c_str());
    }
    return get_apply(state, message,
                     (arg != 0U) ? "Grind on flat ground on" : "Ground beside it checked again, as the game does");
}

// A segment's (or the whole rail's) speed-up.
bool set_speed(const ChainCall& call, State& state)
{
    const ChainOp op = call.op;
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails have speed-ups";
        return false;
    }
    const float rate = (arg != 0U) ? clamp_speed(static_cast<float>(arg) / 10000.0F) : 0.0F;
    const std::vector<uint32_t> points = op == ChainOp::kSpeedRail ? chain_of(name) : std::vector<uint32_t>{name};
    int changed = 0;
    for (uint32_t point : points) {
        const objects::GrindPoint* rail = rail_node_named(point);
        if (op == ChainOp::kSpeedRail && (!rail || !rail->succ())) {
            continue; // (a rail's last point has no segment of its own)
        }
        bool added = false;
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.speed = rate;
                added = true;
            }
        }
        if (!added) {
            auto it = std::ranges::find_if(state.speeds,
                                           [&](const SpeedEdit& speed_edit) { return speed_edit.name == point; });
            if (rate <= 0.0F && it != state.speeds.end()) {
                state.speeds.erase(it);
            } else if (rate > 0.0F && it != state.speeds.end()) {
                it->rate = rate;
            } else if (rate > 0.0F) {
                state.speeds.push_back({.name = point, .rate = rate});
            }
        }
        ++changed;
    }
    sort_by_name(&state.speeds);
    *select = name;
    std::array<char, kListTextSize> text{};
    if (rate > 0.0F) {
        text::format_to(text, "Speed-up on %d segment%s: %.1f%% per frame", changed, changed == 1 ? "" : "s",
                        rate * 100.0F);
    } else {
        text::format_to(text, "Speed-up removed from %d segment%s", changed, changed == 1 ? "" : "s");
    }
    return get_apply(state, message, text.data());
}

// The side a drawn ledge is hung from.
bool set_hang(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const bool ledge = call.ledge;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (!ledge) {
        *message = "Only ledges have a side to hang from";
        return false;
    }
    bool any = false;
    for (const uint32_t point : chain_of(name)) {
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.hang = static_cast<int>(arg) == kHangLeft || static_cast<int>(arg) == kHangRight
                                       ? static_cast<int>(arg)
                                       : 0;
                any = true;
            }
        }
    }
    if (!any) {
        *message = "Original ledges keep their hang side";
        return false;
    }
    *select = name;
    const char* applied = "The game decides the ledge's side";
    if (arg == static_cast<uint32_t>(kHangLeft)) {
        applied = "The ledge is hung from its left side";
    } else if (arg == static_cast<uint32_t>(kHangRight)) {
        applied = "The ledge is hung from its right side";
    }
    return get_apply(state, message, applied);
}

// Linking an end point: the next click picks the end it links to.
bool arm_link(const ChainCall& call, Graph& graph)
{
    const uint32_t name = call.name;
    std::string* message = call.message;
    if (!is_last(graph, name) && !is_first(graph, name)) {
        *message = "Select an end point to link";
        return false;
    }
    level_editor_chains_state().pick_ = ChainOp::kLinkArm;
    level_editor_chains_state().pick_from_ = name;
    *message = "Click the end to link to (Esc: cancel)";
    return false;
}

// A ped's or car's path: the next click picks its first waypoint (AssignArm), or its waypoint links taken away
// (Unassign: its links to anything but waypoints kept). Only peds and cars have paths.
bool assign_path(const ChainCall& call, bool object)
{
    const ChainOp op = call.op;
    const uint32_t name = call.name;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (!object) {
        *message = "Only peds and cars have paths";
        return false;
    }
    if (op == ChainOp::kAssignArm) {
        level_editor_chains_state().pick_ = ChainOp::kAssignArm;
        level_editor_chains_state().pick_from_ = name;
        *message = "Click the first waypoint (Esc: cancel)";
        return false;
    }
    // (its links to anything but waypoints kept)
    State state = at_hand();
    std::vector<uint32_t> links;
    for (const uint32_t other : level_links_now(name)) {
        if (family_of_name(other) != nodes::Family::kRouteWay) {
            links.push_back(other);
        }
    }
    set_link_edit(&state, name, links);
    *select = name;
    return get_apply(state, message, "No path now - it is made again without one");
}

} // namespace

bool apply_op(ChainOp op, uint32_t name, uint32_t arg, std::string* message, uint32_t* select)
{
    *select = 0;
    message->clear();
    if (op == ChainOp::kCancel) {
        stop_drawing();
        level_editor_chains_state().pick_ = ChainOp::kCancel;
        *message = "Stopped";
        return false;
    }
    if (!in_level()) {
        *message = "Not in a level";
        return false;
    }
    const int index = nodes::get_find(name);
    if (index < 0) {
        return false;
    }
    const nodes::Elem node = core::at(nodes::get_all(), index);
    const nodes::Family family = node.family;
    const bool object = node.cls == pedestrian_class() || node.cls == vehicle_class();
    if (op == ChainOp::kAssignArm || op == ChainOp::kUnassign) {
        return assign_path({.op = op, .name = name, .node = &node, .message = message, .select = select}, object);
    }
    const bool ledge = family == nodes::Family::kLadder && is_ledge_node(name);
    if (family == nodes::Family::kLadder && !ledge) {
        return ladder_op(op, name, node, arg, message, select);
    }
    if (family != nodes::Family::kRail && family != nodes::Family::kRouteWay && !ledge) {
        *message = "Not a rail, ladder or path point";
        return false;
    }
    if (node.hidden) {
        *message = "Deleted";
        return false;
    }
    State state = at_hand();
    Graph graph = graph_of(family);
    const ChainCall call{.op = op,
                         .name = name,
                         .arg = arg,
                         .node = &node,
                         .family = family,
                         .ledge = ledge,
                         .message = message,
                         .select = select};
    switch (op) {
    case ChainOp::kInsert:
        return insert_point(call, state, graph);
    case ChainOp::kDelete:
        return delete_point(call, state, graph);
    case ChainOp::kUnlink:
        return unlink_point(call, state, graph);
    case ChainOp::kTerrain:
        return set_terrain(call, state);
    case ChainOp::kExtend:
        return extend_chain(call, graph);
    case ChainOp::kTrickObject:
        return set_trick_object(call, state);
    case ChainOp::kGrindFlat:
        return set_grind_flat(call, state);
    case ChainOp::kSpeed:
    case ChainOp::kSpeedRail:
        return set_speed(call, state);
    case ChainOp::kHang:
        return set_hang(call, state);
    case ChainOp::kLinkArm:
        return arm_link(call, graph);
    default:
        return false;
    }
}

void start_drawing(Blit kind)
{
    level_editor_chains_state().draw_ = kind;
    level_editor_chains_state().draw_last_ = 0;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    level_editor_chains_state().ladder_have_foot_ = false;
}
Blit get_drawing() { return level_editor_chains_state().draw_; }
void stop_drawing()
{
    level_editor_chains_state().draw_ = Blit::kNone;
    level_editor_chains_state().draw_last_ = 0;
    level_editor_chains_state().ladder_have_foot_ = false;
}

namespace {

// The family of the chain being drawn (a ledge's points are a ladder's).
nodes::Family drawn_family()
{
    if (level_editor_chains_state().draw_ == Blit::kRouteWay) {
        return nodes::Family::kRouteWay;
    }
    if (level_editor_chains_state().draw_ == Blit::kLedge) {
        return nodes::Family::kLadder;
    }
    return nodes::Family::kRail;
}

} // namespace

bool draw_at(V3 at, uint32_t end, std::string* message, uint32_t* select)
{
    *select = 0;
    if (level_editor_chains_state().draw_ == Blit::kNone || !in_level()) {
        return false;
    }
    if ((end != 0U) && (level_editor_chains_state().draw_last_ == 0U)) {
        // (the first click on an existing end: drawing goes on from it)
        level_editor_chains_state().draw_last_ = end;
        *select = end;
        *message = "Click the next point";
        return false;
    }
    const nodes::Family family = drawn_family();
    State state = at_hand();
    Graph graph = graph_of(family);
    if ((level_editor_chains_state().draw_last_ != 0U) && !graph.contains(level_editor_chains_state().draw_last_)) {
        level_editor_chains_state().draw_last_ = 0; // (it went: a new chain)
    }
    if ((end != 0U) && (level_editor_chains_state().draw_last_ != 0U) &&
        end != level_editor_chains_state().draw_last_) {
        // (onto an existing end: linked, and the drawing done)
        if (!get_link(&graph, level_editor_chains_state().draw_last_, end)) {
            *message = "Can't link: no free end";
            return false;
        }
        put_graph(&state, graph);
        stop_drawing();
        *select = end;
        return get_apply(state, message, "Linked onto that end - drawing finished");
    }
    const Point chain_point = append_rail_vertex(family, level_editor_chains_state().draw_last_, at, state);
    const uint32_t made = name_hash(chain_point.name);
    state.points.push_back(chain_point);
    graph[made] = {};
    if ((level_editor_chains_state().draw_last_ != 0U) &&
        !extend(&graph, level_editor_chains_state().draw_last_, made)) {
        *message = "Not an end point: starting a new chain";
        level_editor_chains_state().draw_last_ = 0;
    }
    put_graph(&state, graph);
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    const bool first = level_editor_chains_state().draw_last_ == 0;
    level_editor_chains_state().draw_last_ = made;
    *select = made;
    if (message->empty()) {
        if (!first) {
            *message = "Point added - Enter or a right click finishes";
        } else if (family == nodes::Family::kRouteWay) {
            *message = "Path started (Enter: finish)";
        } else if (family == nodes::Family::kLadder) {
            *message = "Ledge started (Enter: finish)";
        } else {
            *message = "Rail started (Enter: finish)";
        }
    }
    return true;
}

bool add_rail(const std::vector<V3>& points, uint32_t cluster, std::string* message, uint32_t* select)
{
    *select = 0;
    if (points.size() < 2 || !in_level()) {
        *message = points.size() < 2 ? "A rail needs two points" : "Not in a level";
        return false;
    }
    const Blit drawing = level_editor_chains_state().draw_;
    level_editor_chains_state().draw_ = Blit::kNone; // (append_rail_vertex: a plain rail, whatever is being drawn)
    State state = at_hand();
    Graph graph = graph_of(nodes::Family::kRail);
    uint32_t last = 0;
    uint32_t first = 0;
    for (const V3 at : points) {
        Point chain_point = append_rail_vertex(nodes::Family::kRail, last, at, state);
        const uint32_t made = name_hash(chain_point.name);
        if (last == 0U) {
            chain_point.cluster = (cluster != 0U) ? cluster : made;
        }
        state.points.push_back(chain_point);
        graph[made] = {};
        if (last != 0U) {
            extend(&graph, last, made);
        } else {
            first = made;
        }
        last = made;
    }
    level_editor_chains_state().draw_ = drawing;
    put_graph(&state, graph);
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    *select = first;
    return true;
}

namespace {

// The game's collision ray (as level_editor.cpp's: a ProbeRay, 0x00417760 / 0x00417710; the hit's normal at +0x24, its
// point at +0x34, the collision object hit at +0x54 and its checksum - the scenery piece's - at +0x58, as
// level_editor_scenery.cpp's cast_static reads them).
bool cast_ray(V3 from, V3 to, SurfaceOut surface, uint32_t* sector = nullptr)
{
    auto* const hit = surface.point;
    auto* const normal = surface.normal;
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& hit_normal = ray->hit().normal;
    const auto& hit_point = ray->hit().point;
    *normal = {.x = hit_normal[0], .y = hit_normal[1], .z = hit_normal[2]};
    *hit = {.x = hit_point[0], .y = hit_point[1], .z = hit_point[2]};
    if (sector) {
        *sector = ray->hit().object ? ray->hit().object_checksum_ : 0;
    }
    return true;
}

// The ground's height below a point (within 70).
bool ground_below(V3 at, float* ground_y)
{
    V3 hit_point{};
    V3 normal{};
    if (!cast_ray(at, {.x = at.x, .y = at.y - kDropProbeDepth, .z = at.z}, {.point = &hit_point, .normal = &normal})) {
        return false;
    }
    *ground_y = hit_point.y;
    return true;
}

} // namespace

bool ladder_click(V3 from, V3 dir, float max_t, std::string* message, uint32_t* select)
{
    *select = 0;
    if (level_editor_chains_state().draw_ != Blit::kLadder || !in_level()) {
        return false;
    }
    if (!level_editor_chains_state().ladder_have_foot_) {
        V3 hit{};
        V3 normal{};
        if (!cast_ray(from, from + dir * max_t, {.point = &hit, .normal = &normal})) {
            *message = "Click on a wall";
            return false;
        }
        V3 foot{};
        if (!ladder_foot(hit, normal, kLadderOut, &ground_below, &foot)) {
            *message = "Click a wall to place the ladder's foot";
            return false;
        }
        level_editor_chains_state().ladder_foot_ = foot;
        level_editor_chains_state().ladder_facing_ = facing_into_wall(normal);
        level_editor_chains_state().ladder_have_foot_ = true;
        *message = "Click to place the top";
        return false;
    }
    V3 top{};
    if (!ladder_top(level_editor_chains_state().ladder_foot_, from, dir, &top)) {
        *message = "Top must be at least 24 units above the foot";
        return false;
    }
    // The ladder: its foot linked to its top, both facing into the wall (as THUG2's own ladders).
    State state = at_hand();
    Point foot = append_rail_vertex(nodes::Family::kLadder, 0, level_editor_chains_state().ladder_foot_, state);
    foot.facing = level_editor_chains_state().ladder_facing_;
    state.points.push_back(foot);
    Point upper = append_rail_vertex(nodes::Family::kLadder, 0, top, state);
    upper.facing = level_editor_chains_state().ladder_facing_;
    state.points.back().links = {name_hash(upper.name)};
    state.points.push_back(upper);
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    stop_drawing();
    *select = name_hash(foot.name);
    std::array<char, kMessageTextSize> text{};
    text::format_to(text, "Ladder added (%.0f high)", static_cast<double>(top.y - level_editor_chains_state().ladder_foot_.y));
    *message = text.data();
    get_log("level editor: added %s / %s (a ladder, facing %.0f degrees)", foot.name.c_str(), upper.name.c_str(),
            static_cast<double>(node_edit::get_heading_degrees(
                {.x = 0.0F, .y = level_editor_chains_state().ladder_facing_, .z = 0.0F})));
    return true;
}

bool magnet_point(V3 from, V3 dir, float max_t, bool free, V3* point, uint32_t* end)
{
    *end = 0;
    V3 hit{};
    V3 normal{};
    if (!cast_ray(from, from + dir * max_t, {.point = &hit, .normal = &normal},
                  &level_editor_chains_state().snap_sector_)) {
        return false;
    }
    *point = hit;
    if (!free) {
        bool snapped = false;
        *point = snap_to_edge(
            hit, normal, kEdgeSnapReach,
            [](V3 at, float* ground_y) {
                V3 hit_point{};
                V3 ray_normal{};
                if (!cast_ray(at, {.x = at.x, .y = at.y - kEdgeProbeDepth, .z = at.z},
                              {.point = &hit_point, .normal = &ray_normal})) {
                    return false;
                }
                *ground_y = hit_point.y;
                return true;
            },
            &snapped);
    }
    // A free end of a chain of the kind drawn, within 24: taken as it is.
    if (level_editor_chains_state().draw_ != Blit::kNone) {
        const nodes::Family family = drawn_family();
        const Graph graph = graph_of(family);
        float best = kJoinReach * kJoinReach;
        for (const auto& entry : graph) {
            if (entry.first == level_editor_chains_state().draw_last_ ||
                (!is_last(graph, entry.first) && !is_first(graph, entry.first))) {
                continue;
            }
            if (level_editor_chains_state().draw_ == Blit::kLedge && !is_ledge_node(entry.first)) {
                continue; // (a ledge links onto ledges, not ladders)
            }
            const V3 at = place_of(entry.first);
            const V3 delta = at - *point;
            const float distance = get_dot(delta, delta);
            if (distance < best) {
                best = distance;
                *end = entry.first;
            }
        }
        if (*end != 0U) {
            *point = place_of(*end);
        }
    }
    return true;
}

bool wants_pick() { return level_editor_chains_state().pick_ == ChainOp::kLinkArm || level_editor_chains_state().pick_ == ChainOp::kAssignArm; }

bool get_picked(uint32_t name, std::string* message, uint32_t* select)
{
    *select = 0;
    const ChainOp pick = level_editor_chains_state().pick_;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    const uint32_t from = level_editor_chains_state().pick_from_;
    if (pick == ChainOp::kLinkArm) {
        const nodes::Family family = family_of_name(from);
        if (family_of_name(name) != family) {
            *message = "Can only link chains of the same kind";
            return false;
        }
        State state = at_hand();
        Graph graph = graph_of(family);
        if (!get_link(&graph, from, name)) {
            *message = "Can't link: no free end";
            return false;
        }
        put_graph(&state, graph);
        *select = from;
        return get_apply(state, message, "Linked");
    }
    if (pick == ChainOp::kAssignArm) {
        if (family_of_name(name) != nodes::Family::kRouteWay) {
            *message = "Click a path's waypoint";
            return false;
        }
        State state = at_hand();
        std::vector<uint32_t> links = {name};
        for (const uint32_t other : level_links_now(from)) {
            if (family_of_name(other) != nodes::Family::kRouteWay && other != name) {
                links.push_back(other);
            }
        }
        set_link_edit(&state, from, links);
        *select = from;
        return get_apply(state, message, "Path assigned");
    }
    return false;
}

const char* get_waiting()
{
    if (level_editor_chains_state().pick_ == ChainOp::kLinkArm) {
        return "Click the end to link to (Esc: cancel)";
    }
    if (level_editor_chains_state().pick_ == ChainOp::kAssignArm) {
        return "Click the first waypoint (Esc: cancel)";
    }
    if (level_editor_chains_state().draw_ == Blit::kRail) {
        return (level_editor_chains_state().draw_last_ != 0U) ? "Rail: click the next point (Enter: finish)"
                                                              : "Rail: click the first point";
    }
    if (level_editor_chains_state().draw_ == Blit::kRouteWay) {
        return (level_editor_chains_state().draw_last_ != 0U) ? "Path: click the next waypoint (Enter: finish)"
                                                              : "Path: click the first waypoint";
    }
    if (level_editor_chains_state().draw_ == Blit::kLedge) {
        return (level_editor_chains_state().draw_last_ != 0U) ? "Ledge: click the next point (Enter: finish)"
                                                              : "Ledge: click the first point on an edge";
    }
    if (level_editor_chains_state().draw_ == Blit::kLadder) {
        return level_editor_chains_state().ladder_have_foot_ ? "Ladder: click to place the top" : "Ladder: click a wall to place the foot";
    }
    return nullptr;
}

} // namespace frontend::level_editor::chains
