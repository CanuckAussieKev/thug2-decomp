// NOT ORIGINAL - the level editor's chains: grind rails, ladders and paths (ours). See level_editor_chains.h.
// This file holds the graph, the level's nodes and the chains' state, the per-frame tick and the view; the pieces
// (chains_records / chains_restore) are level_editor_chains_<name>.cpp, sharing level_editor_chains_internal.h.

#include "editor/level_editor_chains.h"
#include <algorithm>
#include <cmath>
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
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor.h"
#include "base/core/checksums.h"
#include "engine/memory/mem.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "engine/lists/key_map.h"
#include "base/maths/mat4.h"
#include "base/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "game/world/trick_objects.h"
#include "game/commands/game_cfuncs.h"
#include "engine/script/load.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "platform/run_log.h"
#include "game/skate/skate_module.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "engine/script/qb.h"
#include "editor/level_editor_chains_internal.h"

namespace frontend::level_editor::chains {
namespace {

constexpr std::size_t kFacingMessageSize = 96;
constexpr std::size_t kListTextSize = 160;
constexpr std::size_t kMessageTextSize = 128;

constexpr float kMinFlatNormalY = 0.7F; // a surface normal this upright is floor, not wall
constexpr float kTinyLength = 1e-6F;
constexpr float kGroundProbeRise = 4.0F;   // the ground under a ladder foot is looked for from this far above ...
constexpr float kGroundProbeDepth = 64.0F; // ... down to this far below it
constexpr float kParallelEpsilon = 1e-6F;  // lines this close to parallel have no nearest point
constexpr float kFacingEpsilon = 1e-4F;    // two facings closer than this are the same
constexpr float kSameSegmentTolerance = 0.01F;
constexpr float kDropProbeDepth = 70.0F;
constexpr float kEdgeSnapReach = 32.0F; // a point snaps to an edge within this
constexpr float kEdgeProbeDepth = 60.0F;
constexpr float kJoinReach = 24.0F; // a free chain end within this is joined

} // namespace

std::vector<uint32_t> predecessors(const Graph& graph, uint32_t name)
{
    std::vector<uint32_t> found;
    for (const auto& entry : graph) {
        if (entry.first != name && std::ranges::find(entry.second, name) != entry.second.end()) {
            found.push_back(entry.first);
        }
    }
    return found;
}

namespace {

const std::vector<uint32_t>& links_of(const Graph& graph, uint32_t name)
{
    static const std::vector<uint32_t> kNone;
    auto it = graph.find(name);
    return it == graph.end() ? kNone : it->second;
}

} // namespace

bool is_last(const Graph& graph, uint32_t name) { return links_of(graph, name).empty(); }
bool is_first(const Graph& graph, uint32_t name) { return predecessors(graph, name).empty(); }

bool extend(Graph* graph, uint32_t from, uint32_t point)
{
    if (from == point || graph->contains(point)) {
        return false;
    }
    if (is_last(*graph, from)) {
        (*graph)[from] = {point};
        (*graph)[point] = {};
        return true;
    }
    if (is_first(*graph, from)) {
        (*graph)[point] = {from};
        return true;
    }
    return false;
}

bool insert_after(Graph* graph, uint32_t from, uint32_t point)
{
    auto it = graph->find(from);
    if (it == graph->end() || it->second.empty() || from == point || graph->contains(point)) {
        return false;
    }
    const uint32_t next = it->second.front();
    it->second.front() = point;
    (*graph)[point] = {next};
    return true;
}

void drop_out(Graph* graph, uint32_t name)
{
    std::vector<uint32_t> own = links_of(*graph, name);
    const uint32_t next = own.empty() ? 0 : own.front();
    for (auto& entry : *graph) {
        if (entry.first == name) {
            continue;
        }
        std::vector<uint32_t>& links = entry.second;
        auto at = std::ranges::find(links, name);
        if (at == links.end()) {
            continue;
        }
        const bool already = (next != 0U) && std::ranges::find(links, next) != links.end();
        if ((next != 0U) && !already && next != entry.first) {
            *at = next;
        } else {
            links.erase(at);
        }
    }
    (*graph)[name] = {};
}

bool get_link(Graph* graph, uint32_t a, uint32_t b)
{
    if (a == b) {
        return false;
    }
    if (is_last(*graph, a)) {
        (*graph)[a] = {b};
        if (!graph->contains(b)) {
            (*graph)[b] = {};
        }
        return true;
    }
    if (is_last(*graph, b)) {
        (*graph)[b] = {a};
        return true;
    }
    return false;
}

bool uncouple(Graph* graph, uint32_t name)
{
    auto it = graph->find(name);
    if (it == graph->end() || it->second.empty()) {
        return false;
    }
    it->second.erase(it->second.begin());
    return true;
}

std::vector<uint32_t> get_connected(const Graph& graph, uint32_t name)
{
    std::vector<uint32_t> found = {name};
    for (size_t i = 0; i < found.size(); ++i) {
        const uint32_t at = core::at(found, i);
        std::vector<uint32_t> near = links_of(graph, at);
        for (const uint32_t predecessor : predecessors(graph, at)) {
            near.push_back(predecessor);
        }
        for (const uint32_t count : near) {
            if (std::ranges::find(found, count) == found.end()) {
                found.push_back(count);
            }
        }
    }
    return found;
}

V3 snap_to_edge(V3 hit, V3 normal, float reach, const std::function<bool(V3 from, float* ground_y)>& ground_at,
                bool* snapped)
{
    *snapped = false;
    if (normal.y < kMinFlatNormalY) {
        return hit;
    }
    constexpr float kStep = 4.0F;
    constexpr float kDrop = 8.0F;
    float best = reach + 1.0F;
    V3 best_at = hit;
    constexpr float kHalf = 0.70710678F;
    static const std::array<V3, 8> kDirections = {{{.x = 0.0F, .y = 0.0F, .z = 1.0F},
                                                   {.x = kHalf, .y = 0.0F, .z = kHalf},
                                                   {.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                                   {.x = kHalf, .y = 0.0F, .z = -kHalf},
                                                   {.x = 0.0F, .y = 0.0F, .z = -1.0F},
                                                   {.x = -kHalf, .y = 0.0F, .z = -kHalf},
                                                   {.x = -1.0F, .y = 0.0F, .z = 0.0F},
                                                   {.x = -kHalf, .y = 0.0F, .z = kHalf}}};
    for (const V3& dir : kDirections) {
        for (int step = 1;; ++step) {
            const float fraction = static_cast<float>(step) * kStep;
            if (!(fraction <= reach)) {
                break;
            }
            const V3 probe = hit + dir * fraction;
            float ground = 0.0F;
            const bool have = ground_at({.x = probe.x, .y = hit.y + 20.0F, .z = probe.z}, &ground);
            if (!have || ground < hit.y - kDrop) {
                const float edge = fraction - (kStep * 0.5F);
                if (edge < best) {
                    best = edge;
                    best_at = hit + dir * edge;
                }
                break;
            }
        }
    }
    if (best > reach) {
        return hit;
    }
    *snapped = true;
    return {.x = best_at.x, .y = hit.y, .z = best_at.z};
}

const std::vector<std::string>& grind_terrain_names()
{
    // (scripts\game\terrain.qb's Terrain_Types, the TERRAIN_GRIND... entries in its order - the names only: the game
    // checks them against its own table before one is used, TerrainChecksums)
    static const std::vector<std::string> kNames = {
        "TERRAIN_GRINDTRAIN",     "TERRAIN_GRINDROPE",           "TERRAIN_GRINDWIRE",
        "TERRAIN_GRINDCONC",      "TERRAIN_GRINDROUNDMETALPOLE", "TERRAIN_GRINDCHAINLINK",
        "TERRAIN_GRINDMETAL",     "TERRAIN_GRINDWOODRAILING",    "TERRAIN_GRINDWOODLOG",
        "TERRAIN_GRINDWOOD",      "TERRAIN_GRINDPLASTIC",        "TERRAIN_GRINDELECTRICWIRE",
        "TERRAIN_GRINDCABLE",     "TERRAIN_GRINDCHAIN",          "TERRAIN_GRINDPLASTICBARRIER",
        "TERRAIN_GRINDNEONLIGHT", "TERRAIN_GRINDGLASSMONSTER",   "TERRAIN_GRINDBANYONTREE",
        "TERRAIN_GRINDBRASSRAIL", "TERRAIN_GRINDCATWALK",        "TERRAIN_GRINDTANKTURRET",
    };
    return kNames;
}

float facing_into_wall(V3 normal)
{
    const float length = std::sqrt((normal.x * normal.x) + (normal.z * normal.z));
    if (!(length > kTinyLength)) {
        return 0.0F;
    }
    return std::atan2(-normal.x / length, -normal.z / length);
}

V3 facing_direction(float facing) { return {.x = std::sin(facing), .y = 0.0F, .z = std::cos(facing)}; }

bool ladder_foot(V3 hit, V3 normal, float out, const std::function<bool(V3 from, float* ground_y)>& ground_at, V3* foot)
{
    if (!(std::fabs(normal.y) < kMinFlatNormalY)) {
        return false;
    }
    const float length = std::sqrt((normal.x * normal.x) + (normal.z * normal.z));
    if (!(length > kTinyLength)) {
        return false;
    }
    V3 at = {.x = hit.x + (normal.x / length * out), .y = hit.y, .z = hit.z + (normal.z / length * out)};
    float ground = 0.0F;
    if (ground_at({.x = at.x, .y = at.y + kGroundProbeRise, .z = at.z}, &ground) && ground <= at.y + kGroundProbeRise &&
        ground >= at.y - kGroundProbeDepth) {
        at.y = ground;
    }
    *foot = at;
    return true;
}

bool ladder_top(V3 foot, V3 from, V3 dir, V3* top)
{
    // The closest points of the line foot + t (0, 1, 0) and the ray from + s dir.
    const V3 w = foot - from;
    const float dir_y = dir.y;
    const float dir_dir = get_dot(dir, dir);
    const float w_y = w.y;
    const float dir_w = get_dot(dir, w);
    const float denominator = dir_dir - (dir_y * dir_y);
    if (!(denominator > kParallelEpsilon * dir_dir)) {
        return false; // (looking straight up or down the line)
    }
    const float fraction = ((dir_y * dir_w) - (dir_dir * w_y)) / denominator;
    if (!(fraction >= kLadderMin)) {
        return false;
    }
    *top = {.x = foot.x, .y = foot.y + fraction, .z = foot.z};
    return true;
}

bool same_segment(V3 a, V3 b, V3 other_a, V3 other_b, float tolerance)
{
    auto near = [tolerance](V3 x, V3 y) {
        return std::fabs(x.x - y.x) <= tolerance && std::fabs(x.y - y.y) <= tolerance &&
               std::fabs(x.z - y.z) <= tolerance;
    };
    return (near(a, other_a) && near(b, other_b)) || (near(a, other_b) && near(b, other_a));
}

namespace {

constexpr uint32_t kClass = 0x12B4E660;          // class
constexpr uint32_t kLinks = 0x2E7D5EE7;          // links
constexpr uint32_t kPos = 0x7F261953;            // pos

} // namespace
namespace internal {

uint32_t name_hash(const std::string& text) { return checksums::hash_from_text(text.c_str()); }

bool in_level() { return level_nodes::node_manager_instance() != nullptr && scenery::main_scene() != nullptr; }

script::Struct* look_up_node_params(uint32_t name)
{
    return (name != 0U) && level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
}

// The level's rail manager (modules::Skate +0x24; null when there is none or its rails ride an object).
objects::GrindRailHub* get_rails()
{
    const modules::Skate* skate = modules::Skate::sole();
    auto* manager = skate ? skate->rail_manager().get() : nullptr;
    return manager && !manager->on_object() ? manager : nullptr;
}

objects::GrindPoint* rail_node_named(uint32_t name)
{
    const objects::GrindRailHub* rails = get_rails();
    if (!rails || !rails->nodes()) {
        return nullptr;
    }
    for (int i = 0; i < rails->num(); ++i) {
        if (core::at(rails->node_run(), i).label() == name) {
            return &core::at(rails->node_run(), i);
        }
    }
    return nullptr;
}

// A structure's own links= (not an included structure's) as names.
std::vector<uint32_t> own_links(script::Struct* node)
{
    std::vector<uint32_t> links;
    if (!node) {
        return links;
    }
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kLinks &&
            (value_kind(*component) == script::QbValueKind::kArray ||
             value_kind(*component) == script::QbValueKind::kArrayRef) &&
            component->value.array()) {
            links.clear();
            for (uint32_t i = 0; i < component->value.array()->extent(); ++i) {
                links.push_back(component->value.array()->fetch_part(i));
            }
        }
    }
    return links;
}

// A level structure's own links= set (in place when it has one; removed for none).
void set_own_links(script::Struct* node, const std::vector<uint32_t>& links)
{
    const script::Component* own = nullptr;
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kLinks &&
            (value_kind(*component) == script::QbValueKind::kArray ||
             value_kind(*component) == script::QbValueKind::kArrayRef) &&
            component->value.array()) {
            own = component;
        }
    }
    if (links.empty()) {
        if (own) {
            node->erase_module(kLinks);
        }
        return;
    }
    if (own) {
        own->value.array()->configure_size_kind(static_cast<uint32_t>(links.size()), script::QbValueKind::kName);
        for (size_t i = 0; i < links.size(); ++i) {
            own->value.array()->set_element(static_cast<uint32_t>(i), core::at(links, i));
        }
        return;
    }
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (!array) {
        return;
    }
    new (array) script::Array;
    array->configure_size_kind(static_cast<uint32_t>(links.size()), script::QbValueKind::kName);
    for (size_t i = 0; i < links.size(); ++i) {
        array->set_element(static_cast<uint32_t>(i), core::at(links, i));
    }
    node->append_array_ref(kLinks, array);
}

// A structure's own pos= set (the one the game reads: the last).
void write_pos(script::Struct* node, V3 pos)
{
    const script::Component* own = nullptr;
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kPos && value_kind(*component) == script::QbValueKind::kVector &&
            component->value.vector()) {
            own = component;
        }
    }
    if (own) {
        own->value.vector()->x() = pos.x;
        own->value.vector()->y() = pos.y;
        own->value.vector()->z() = pos.z;
    } else {
        node->put_vec3(checksums::Name{kPos}, {.x = pos.x, .y = pos.y, .z = pos.z});
    }
}

// A structure's own angles= set (as write_pos).
void write_angles(script::Struct* node, V3 angles)
{
    constexpr uint32_t kAngles = 0x9D2D0915; // angles
    const script::Component* own = nullptr;
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kAngles && value_kind(*component) == script::QbValueKind::kVector &&
            component->value.vector()) {
            own = component;
        }
    }
    if (own) {
        own->value.vector()->x() = angles.x;
        own->value.vector()->y() = angles.y;
        own->value.vector()->z() = angles.z;
    } else {
        node->put_vec3(checksums::Name{kAngles}, {.x = angles.x, .y = angles.y, .z = angles.z});
    }
}

uint32_t class_of(script::Struct* node)
{
    uint32_t cls = 0;
    if (node) {
        node->read_hash_value(kClass, &cls, 0);
    }
    return cls;
}

uint32_t rail_node_class()
{
    static const uint32_t kC = name_hash("RailNode");
    return kC;
}
uint32_t climbing_node_class()
{
    static const uint32_t kC = name_hash("ClimbingNode");
    return kC;
}

} // namespace internal
namespace {

uint32_t waypoint_class()
{
    static const uint32_t kC = name_hash("Waypoint");
    return kC;
}

} // namespace
namespace internal {

uint32_t pedestrian_class()
{
    static const uint32_t kC = name_hash("Pedestrian");
    return kC;
}
uint32_t vehicle_class()
{
    static const uint32_t kC = name_hash("Vehicle");
    return kC;
}

// The terrain checksums the game knows (Terrain_Types' checksum=): only those are looked up (0x004442C0 reads through
// a null entry for an unknown one).
bool known_terrain(uint32_t terrain)
{
    static const uint32_t kChecksumKey = name_hash("checksum");
    script::Array* types = script::fetch_list("Terrain_Types");
    if (!types || types->kind_tag() != script::QbValueKind::kStruct) {
        return false;
    }
    for (uint32_t i = 0; i < types->extent(); ++i) {
        auto* entry = types->struct_at(i);
        uint32_t value = 0;
        if (entry && entry->read_hash_value(kChecksumKey, &value, 0) && value == terrain) {
            return true;
        }
    }
    return false;
}
bool terrain_byte(const std::string& name, uint8_t* value)
{
    const uint32_t terrain = name_hash(name);
    if (!known_terrain(terrain)) {
        return false;
    }
    *value = surface_kind::get_terrain_index(terrain);
    return true;
}

std::map<uint32_t, OriginalLinks>& original_links()
{
    static std::map<uint32_t, OriginalLinks> value;
    return value;
}
std::vector<uint32_t>& stored_dropped()
{
    static core::NoDestroy<std::vector<uint32_t>> dropped;
    return dropped.get();
}
std::map<uint32_t, bool>& drop_active()
{
    static std::map<uint32_t, bool> value;
    return value;
}

std::map<uint32_t, TerrainSet>& terrains_state()
{
    static std::map<uint32_t, TerrainSet> value;
    return value;
}

LevelEditorChainsState& level_editor_chains_state()
{
    static core::NoDestroy<LevelEditorChainsState> state;
    return state.get();
}

// Level rail points given a speed-up (their rates).
std::map<uint32_t, float>& level_speeds()
{
    static std::map<uint32_t, float> value;
    return value;
}

std::map<uint32_t, SpeedApplied>& speed_applied()
{
    static std::map<uint32_t, SpeedApplied> value;
    return value;
}

} // namespace internal
namespace {

std::vector<uint8_t>& speed_defined()
{
    static core::NoDestroy<std::vector<uint8_t>> speed_defined;
    return speed_defined.get();
}

} // namespace
namespace internal {

LevelSpeedCache& level_speed_cache()
{
    static core::NoDestroy<LevelSpeedCache> level_speed_cache;
    return level_speed_cache.get();
}
std::unordered_map<uint32_t, Extras>& extras_state()
{
    static std::unordered_map<uint32_t, Extras> value;
    return value;
}

std::map<uint32_t, FacingSet>& facings_state()
{
    static std::map<uint32_t, FacingSet> value;
    return value;
}
// Level rail points marked grindable on flat ground.
std::vector<uint32_t>& level_flat()
{
    static core::NoDestroy<std::vector<uint32_t>> level_flat;
    return level_flat.get();
}
// The trick objects the editor registered (name -> cluster).
std::map<uint32_t, uint32_t>& trick_state()
{
    static std::map<uint32_t, uint32_t> value;
    return value;
}

FlatChains& flat_chains()
{
    static core::NoDestroy<FlatChains> state;
    return state.get();
}

std::vector<Point> added_points()
{
    std::vector<Point> points;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (!is_chain_class(spec.cls)) {
            continue;
        }
        Point chain_point;
        chain_point.name = spec.name;
        chain_point.cls = spec.cls;
        chain_point.pos = spec.pos;
        chain_point.like = spec.like;
        chain_point.terrain = spec.terrain;
        chain_point.links = spec.links;
        if (same_text(spec.cls, "ClimbingNode")) {
            chain_point.facing = spec.angles.y; // (a ladder's Angles: (0, facing, 0))
            if (same_text(spec.type, "Ledge")) {
                chain_point.type = "Ledge";
                for (const node_create::Prop& prop : spec.extra) {
                    if (prop.type == node_create::Prop::kFlag && same_text(prop.text, "HangLeft")) {
                        chain_point.hang = kHangLeft;
                    }
                    if (prop.type == node_create::Prop::kFlag && same_text(prop.text, "HangRight")) {
                        chain_point.hang = kHangRight;
                    }
                }
            }
        }
        auto extras = extras_state().find(name_hash(spec.name));
        if (extras != extras_state().end()) {
            chain_point.cluster = extras->second.cluster;
            chain_point.grind_flat_ = extras->second.grind_flat_;
            chain_point.speed = extras->second.speed;
        }
        // (a rail point: where the rail manager has it - a sector moved with it takes it along)
        if (const objects::GrindPoint* node = rail_node_named(name_hash(spec.name))) {
            chain_point.pos = {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z};
        }
        points.push_back(chain_point);
    }
    return points;
}

node_create::Spec spec_of(const Point& chain_point)
{
    node_create::Spec spec;
    spec.name = chain_point.name;
    spec.cls = chain_point.cls;
    spec.pos = chain_point.pos;
    spec.like = chain_point.like;
    spec.terrain = chain_point.terrain;
    spec.links = chain_point.links;
    if (same_text(chain_point.cls, "ClimbingNode")) {
        spec.type = "Ladder";
        spec.angles = {.x = 0.0F, .y = chain_point.facing, .z = 0.0F};
        if (is_ledge(chain_point)) {
            // (THUG2's own ledges: Type=Ledge, CreatedAtStart, a side to hang from when the level gives one)
            spec.type = "Ledge";
            if (chain_point.hang != 0) {
                node_create::Prop flag = {.type = node_create::Prop::kFlag, .key = ""};
                flag.text = chain_point.hang == kHangLeft ? "HangLeft" : "HangRight";
                spec.extra.push_back(flag);
            }
        }
    }
    return spec;
}

// A climbing node's orientation (the rail manager's quaternion, as append_vertex makes it from Angles) for a heading.
maths::Vec4 climb_of(float facing)
{
    maths::Mat4 matrix;
    core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    const maths::Vec4 angles = {.x = 0.0F, .y = facing, .z = 0.0F, .w = 1.0F};
    const maths::Mat4* built = &matrix.init_from_angles(angles);
    return maths::quat_from_matrix(*built);
}

} // namespace internal
namespace {

// The heading of a climbing node's orientation: its at row, flat.
float heading_of_climb(const maths::Vec4& climb)
{
    maths::Mat4 matrix;
    maths::quat_to_matrix(climb, &matrix);
    return std::atan2(core::at(matrix.row, 2).x, core::at(matrix.row, 2).z);
}

// A ladder point's heading now (the rail manager's; 0: not one).
float facing_now(uint32_t name)
{
    const objects::GrindPoint* node = rail_node_named(name);
    return node && ((node->flag_bits() & objects::kRailFlagClimbing) != 0U) ? heading_of_climb(node->climb()) : 0.0F;
}

// A level ladder point's heading as the level has it.
float facing_original(uint32_t name)
{
    auto it = facings_state().find(name);
    return it != facings_state().end() ? heading_of_climb(it->second.original) : facing_now(name);
}

bool same_facing(float a, float b) { return std::fabs(node_edit::wrap_angle(a - b)) < kFacingEpsilon; }

} // namespace
namespace internal {

std::vector<uint32_t> level_links_now(uint32_t name) { return own_links(look_up_node_params(name)); }
std::vector<uint32_t> level_links_original(uint32_t name)
{
    auto it = original_links().find(name);
    return it != original_links().end() ? it->second.links : level_links_now(name);
}

void set_level_links(uint32_t name, const std::vector<uint32_t>& links)
{
    script::Struct* node = look_up_node_params(name);
    if (!node || node_create::is_added(name)) {
        return;
    }
    if (!original_links().contains(name)) {
        original_links()[name] = {.node = node, .links = own_links(node)};
    }
    if (own_links(node) != links) {
        set_own_links(node, links);
    }
}

// The rail manager node's box: to its next node, or its own point.
void cuboid(objects::GrindPoint* node)
{
    const maths::Vec4 here = node->at_point();
    const maths::Vec4 there = node->succ() ? node->succ()->at_point() : node->at_point();
    objects::box_of(&here, &there, &node->lower_limit(), &node->upper_limit());
}

void get_remake(uint32_t name)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
    if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
        object->flag_as_dead();
    }
    if (std::ranges::find(level_editor_chains_state().remake_, name) == level_editor_chains_state().remake_.end()) {
        level_editor_chains_state().remake_.push_back(name);
    }
    get_log("level editor: %s's path changed - it is made again from its node", object_edit::caption(name).c_str());
}

// The drawn rails' trick objects registered with the trick object manager (0x007CE500) as add_from_node_array registers
// the level's TrickObject rails (0x005896D0 the cluster, 0x00589080 the node's name mapped to it); those no longer
// wanted (or of another cluster now) taken out of its name map (the manager's object_clusters_).
void register_trick_objects()
{
    objects::TrickSpotHub* manager = objects::trick_spot_hub();
    if (!manager) {
        return;
    }
    std::map<uint32_t, uint32_t> want;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        const uint32_t name = name_hash(spec.name);
        auto extras = extras_state().find(name);
        if (same_text(spec.cls, "RailNode") && extras != extras_state().end() && (extras->second.cluster != 0U) &&
            (rail_node_named(name) != nullptr)) {
            want[name] = extras->second.cluster;
        }
    }
    lists::KeyMap* objects = &manager->object_clusters();
    for (const auto& entry : trick_state()) {
        auto it = want.find(entry.first);
        if (it == want.end() || it->second != entry.second) {
            objects->erase_entry(entry.first);
        }
    }
    for (const auto& entry : want) {
        auto it = trick_state().find(entry.first);
        if (it != trick_state().end() && it->second == entry.second) {
            continue;
        }
        manager->add_cluster(entry.second); // (false: there already)
        manager->add_object_to_cluster(entry.first, entry.second);
    }
    if (debug_enabled() && want != trick_state()) {
        get_log("level editor: %u drawn rail points are trick objects", static_cast<unsigned>(want.size()));
    }
    trick_state() = want;
}

// The rebuilt rail nodes indexed by name and switched on / off: dropped points off (their state noted first),
// shown ones back as they were.
void set_dropped_states(std::span<objects::GrindPoint> nodes, std::unordered_map<uint32_t, int>& index)
{
    const int count = static_cast<int>(nodes.size());
    for (int i = 0; i < count; ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        index.emplace(node->label(), i);
        node->set_succ(node->last_value() = nullptr);
        const bool dropped = std::ranges::find(stored_dropped(), node->label()) != stored_dropped().end();
        auto was = drop_active().find(node->label());
        if (dropped) {
            if (was == drop_active().end()) {
                drop_active()[node->label()] = (node->flag_bits() & objects::kRailFlagActive) != 0;
            }
            node->flag_bits() &= ~objects::kRailFlagActive;
        } else if (was != drop_active().end()) {
            node->flag_bits() = was->second ? node->flag_bits() | objects::kRailFlagActive
                                            : node->flag_bits() & ~objects::kRailFlagActive;
            drop_active().erase(was);
        }
    }
}

// The links, as add_from_node_array makes them: a node's next is its first link (or a DefaultLine one), the linked
// node's previous likewise. Dropped points take no part.
void link_rail_nodes(std::span<objects::GrindPoint> nodes, const std::unordered_map<uint32_t, int>& index)
{
    const int count = static_cast<int>(nodes.size());
    auto is_dropped = [&](uint32_t name) {
        return std::ranges::find(stored_dropped(), name) != stored_dropped().end();
    };
    for (int i = 0; i < count; ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        if (is_dropped(node->label())) {
            continue;
        }
        for (const uint32_t link : own_links(look_up_node_params(node->label()))) {
            auto it = index.find(link);
            if (it == index.end() || is_dropped(link)) {
                continue;
            }
            objects::GrindPoint* other = &core::at(nodes, it->second);
            if (!node->succ() || ((other->flag_bits() & objects::kRailFlagDefaultLine) != 0U)) {
                node->set_succ(other);
            }
            if (!other->last_value() || ((node->flag_bits() & objects::kRailFlagDefaultLine) != 0U)) {
                other->last_value() = node;
            }
        }
    }
}

} // namespace internal

bool is_ledge_node(uint32_t name)
{
    static const uint32_t kLedge = name_hash("Ledge");
    const int index = nodes::get_find(name);
    return index >= 0 && core::at(nodes::get_all(), index).cls == climbing_node_class() && core::at(nodes::get_all(), index).type == kLedge;
}

void apply_speeds()
{
    if (!level_nodes::node_manager_instance() ||
        (speed_applied().empty() && extras_state().empty() && level_speeds().empty())) {
        return;
    }
    // What's wanted (none in a network session: the edited level's changes are offline only).
    std::map<uint32_t, float> want;
    if (!layout::get_refused()) {
        for (const auto& entry : extras_state()) {
            if (entry.second.speed > 0.0F) {
                want[entry.first] = entry.second.speed;
            }
        }
        for (const auto& entry : level_speeds()) {
            want[entry.first] = entry.second;
        }
    }
    // Those no longer wanted put back; a node whose structure was made again (the overlay registered again) forgotten
    // (its new structure has no TriggerScript of ours).
    for (auto it = speed_applied().begin(); it != speed_applied().end();) {
        script::Struct* node = look_up_node_params(it->first);
        if (node != it->second.node) {
            it = speed_applied().erase(it);
            continue;
        }
        if (!want.contains(it->first)) {
            fields::put_in(node, kTriggerScriptKey, false, it->second.original);
            it = speed_applied().erase(it);
            continue;
        }
        ++it;
    }
    std::vector<SpeedScript> scripts;
    for (const auto& w : want) {
        script::Struct* node = look_up_node_params(w.first);
        if (!node) {
            continue;
        }
        const uint32_t ours = name_hash(speed_script_name(w.first));
        auto it = speed_applied().find(w.first);
        if (it == speed_applied().end()) {
            SpeedApplied applied;
            applied.node = node;
            applied.original = fields::own_in(node, kTriggerScriptKey, false);
            node->read_hash_value(kTriggerScriptKey, &applied.original_script_, 0);
            if (applied.original_script_ == ours) {
                applied.original_script_ = 0;
            }
            it = speed_applied().emplace(w.first, applied).first;
        }
        scripts.push_back({.node = w.first, .original = it->second.original_script_, .rate = w.second});
    }
    if (scripts.empty()) {
        return;
    }
    // The scripts: the fixed two (once - they stay defined), the points' when they change.
    static const uint32_t kRailSpeed = name_hash("ed_RailSpeed");
    const script::SymbolTableEntry* fixed = script::resolve_symbol(kRailSpeed);
    if (!fixed || fixed->type() != script::QbValueKind::kScriptBody) {
        std::vector<uint8_t> qb = rail_speed_qb(); // (the loader may relocate a pre-compiled file in place)
        script::load_qb_data("editor\\thug2_decomp_railspeed.qb", qb, 0, 0, 0);
    }
    std::vector<uint8_t> file = speed_qb_file(scripts);
    if (file != speed_defined()) {
        script::load_qb_data("editor\\thug2_decomp_railspeeds.qb", file, 0, 0, 0);
        speed_defined() = file;
        if (debug_enabled()) {
            get_log("level editor: %u rail speed-up scripts defined", static_cast<unsigned>(scripts.size()));
        }
    }
    // Each node's TriggerScript ours (its own component, after any shared one: it counts).
    for (const SpeedScript& speed_script : scripts) {
        script::Struct* node = look_up_node_params(speed_script.node);
        const uint32_t ours = name_hash(speed_script_name(speed_script.node));
        uint32_t now = 0;
        if (!node || (node->read_hash_value(kTriggerScriptKey, &now, 0) && now == ours)) {
            continue;
        }
        fields::Own own;
        own.present = true;
        own.value.kind = fields::Kind::kName;
        own.value.checksum = ours;
        own.index = -1;
        fields::put_in(node, kTriggerScriptKey, false, own);
    }
}

std::vector<SpeedSegment> speed_segments(V3 near, float radius)
{
    std::vector<SpeedSegment> out;
    const objects::GrindRailHub* rails = get_rails();
    if (!rails || !rails->nodes() || !level_nodes::node_manager_instance()) {
        return out;
    }
    // (the level's own RailAccelerate points: found once for each rail manager array)
    if (level_speed_cache().nodes != rails->nodes() || level_speed_cache().count != rails->num()) {
        level_speed_cache().nodes = rails->nodes();
        level_speed_cache().count = rails->num();
        level_speed_cache().found.clear();
        for (int i = 0; i < rails->num(); ++i) {
            const objects::GrindPoint* node = &core::at(rails->node_run(), i);
            script::Struct* params = node->succ() ? look_up_node_params(node->label()) : nullptr;
            uint32_t script = 0;
            if (!params || !params->read_hash_value(kTriggerScriptKey, &script, 0) || (script == 0U)) {
                continue;
            }
            auto applied = speed_applied().find(node->label());
            if (applied != speed_applied().end()) {
                script = applied->second.original_script_; // (the level's own script, under ours)
            }
            const triggers::Calls calls = triggers::script_calls(script);
            if (calls.rail_speed_) {
                level_speed_cache().found.emplace_back(node->label(), calls.rail_rate_);
            }
        }
    }
    float r2 = radius * radius;
    auto add = [&](const objects::GrindPoint* node, float rate, bool level) {
        if (!node || !node->succ() || (node->flag_bits() & objects::kRailFlagClimbing)) {
            return;
        }
        const V3 point_a = {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z};
        const V3 point_b = {
            .x = node->succ()->at_point().x, .y = node->succ()->at_point().y, .z = node->succ()->at_point().z};
        const V3 da = point_a - near;
        const V3 db = point_b - near;
        if (get_dot(da, da) > r2 && get_dot(db, db) > r2) {
            return;
        }
        out.push_back({.a = point_a, .b = point_b, .rate = rate, .level = level});
    };
    for (const auto& entry : speed_applied()) {
        auto extras = extras_state().find(entry.first);
        auto level = level_speeds().find(entry.first);
        float rate = 0.0F;
        if (extras != extras_state().end()) {
            rate = extras->second.speed;
        } else if (level != level_speeds().end()) {
            rate = level->second;
        }
        if (rate > 0.0F) {
            add(rail_node_named(entry.first), rate, false);
        }
    }
    for (const auto& found : level_speed_cache().found) {
        if (!speed_applied().contains(found.first)) {
            add(rail_node_named(found.first), found.second, true);
        }
    }
    return out;
}

bool flat_grindable(V3 a, V3 b)
{
    objects::GrindRailHub* rails = get_rails();
    if (!rails) {
        return false;
    }
    refresh_flat(rails);
    return std::ranges::any_of(flat_chains().flat_cache_.marked, [&](const objects::GrindPoint* node) {
        const objects::GrindPoint* next = node->succ();
        return next != nullptr &&
               same_segment(a, b, {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z},
                            {.x = next->at_point().x, .y = next->at_point().y, .z = next->at_point().z},
                            kSameSegmentTolerance);
    });
}

void tick()
{
    static bool hooked;
    if (!hooked) {
        objects::set_rail_clear_override(&flat_hook); // (RailClear asks it - see the top)
        hooked = true;
    }
    // The rail manager's old node arrays: freed once the level's rails are no longer the ones built here (the game
    // made a new level's - its Cleanup freed ours - or there are none).
    if (!level_editor_chains_state().retired_.empty()) {
        const objects::GrindRailHub* rails = get_rails();
        if (!rails || rails->nodes() != level_editor_chains_state().built_) {
            for (void* nodes : level_editor_chains_state().retired_) {
                memory::free_malloc(nodes);
            }
            level_editor_chains_state().retired_.clear();
            level_editor_chains_state().built_ = nullptr;
        }
    }
    apply_speeds();
    if (level_editor_chains_state().remake_.empty()) {
        return;
    }
    if (!in_level()) {
        level_editor_chains_state().remake_.clear();
        return;
    }
    for (auto it = level_editor_chains_state().remake_.begin(); it != level_editor_chains_state().remake_.end();) {
        if (objects::find_object(*it)) {
            ++it; // (not removed yet: the game deletes dead objects in its update)
            continue;
        }
        if (script::Struct* node = look_up_node_params(*it)) {
            commands::create_from_node(node);
        }
        get_log("level editor: %s made again (its path from its links)", object_edit::caption(*it).c_str());
        it = level_editor_chains_state().remake_.erase(it);
    }
}

bool is_chain_node(uint32_t name)
{
    const int index = nodes::get_find(name);
    if (index < 0) {
        return false;
    }
    const nodes::Family family = core::at(nodes::get_all(), index).family;
    return family == nodes::Family::kRail || family == nodes::Family::kLadder || family == nodes::Family::kRouteWay;
}
bool is_path(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kRouteWay;
}
bool is_ladder(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kLadder;
}

bool is_added_point(uint32_t name)
{
    if (!node_create::is_added(name)) {
        return false;
    }
    const int index = nodes::get_find(name);
    return index >= 0 && (core::at(nodes::get_all(), index).family == nodes::Family::kRail ||
                          core::at(nodes::get_all(), index).family == nodes::Family::kLadder ||
                          core::at(nodes::get_all(), index).family == nodes::Family::kRouteWay);
}

bool move_added_point(uint32_t name, V3 pos)
{
    script::Struct* node = node_create::is_added(name) ? look_up_node_params(name) : nullptr;
    if (!node) {
        return false;
    }
    maths::Vec4 now;
    level_nodes::fetch_place(node, &now);
    if (now.x != pos.x || now.y != pos.y || now.z != pos.z) {
        write_pos(node, pos);
    }
    if (objects::GrindPoint* rail = rail_node_named(name)) {
        if (rail->at_point().x != pos.x || rail->at_point().y != pos.y || rail->at_point().z != pos.z) {
            rail->at_point() = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0F};
            const objects::GrindRailHub* rails = get_rails();
            for (int i = 0; i < rails->num(); ++i) {
                if (&core::at(rails->node_run(), i) == rail || core::at(rails->node_run(), i).succ() == rail) {
                    cuboid(&core::at(rails->node_run(), i));
                }
            }
            scenery::rails_rebuilt(); // (the node layer reads the places again)
        }
    }
    const int index = nodes::get_find(name);
    if (index >= 0) {
        nodes::reread(index);
    }
    return true;
}

namespace {

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

namespace {

// A ledge's hang side: an added point's own, else the node's HangLeft / HangRight.
void read_ledge_hang(const nodes::Elem& node, uint32_t name, View& view)
{
    if (node.added) {
        for (const Point& chain_point : added_points()) {
            if (name_hash(chain_point.name) == name) {
                view.hang = chain_point.hang;
            }
        }
    } else if (script::Struct* params = look_up_node_params(name)) {
        static const uint32_t kLeft = name_hash("HangLeft");
        static const uint32_t kRight = name_hash("HangRight");
        if (params->has_bare_flag(kLeft)) {
            view.hang = kHangLeft;
        } else if (params->has_bare_flag(kRight)) {
            view.hang = kHangRight;
        } else {
            view.hang = 0;
        }
    }
}

// A rail's settings: its speed-up (the editor's, the level's), its own script's speed, its trick-object cluster,
// flat grinding and terrain.
void read_rail_view(const nodes::Elem& node, uint32_t name, View& view)
{
    auto speed = extras_state().find(name);
    auto level_speed = level_speeds().find(name);
    if (speed != extras_state().end()) {
        view.speed = speed->second.speed;
    } else if (level_speed != level_speeds().end()) {
        view.speed = level_speed->second;
    } else {
        view.speed = 0.0F;
    }
    script::Struct* own = look_up_node_params(name);
    uint32_t script = 0;
    auto applied = speed_applied().find(name);
    if (applied != speed_applied().end()) {
        script = applied->second.original_script_;
    } else if (own) {
        own->read_hash_value(kTriggerScriptKey, &script, 0);
    }
    if (script != 0U) {
        const triggers::Calls calls = triggers::script_calls(script);
        view.level_speed_ = calls.rail_speed_;
        view.level_rate_ = calls.rail_rate_;
    }
    auto extras = extras_state().find(name);
    if (node.added) {
        view.can_trick_object_ = true;
        view.trick_object_ = extras != extras_state().end() && (extras->second.cluster != 0U);
        if (view.trick_object_) {
            view.cluster = object_edit::caption(extras->second.cluster);
        }
        view.grind_flat_ = extras != extras_state().end() && extras->second.grind_flat_;
    } else {
        view.trick_object_ = node.trick_object_;
        static const uint32_t kCluster = name_hash("Cluster");
        static const uint32_t kFlag = name_hash("ed_grind_flat");
        script::Struct* params = look_up_node_params(name);
        uint32_t cluster = name;
        if (params) {
            params->read_hash_value(kCluster, &cluster, 0);
        }
        if (view.trick_object_) {
            view.cluster = object_edit::caption(cluster);
        }
        view.grind_flat_ = std::ranges::find(level_flat(), name) != level_flat().end() ||
                           ((params != nullptr) && params->has_bare_flag(kFlag));
    }
    if (const objects::GrindPoint* rail = rail_node_named(name)) {
        for (size_t i = 0; i < view.terrains.size(); ++i) {
            uint8_t value = 0;
            if (terrain_byte(core::at(view.terrains, i), &value) && value == rail->surface_type()) {
                view.terrain = static_cast<int>(i);
                break;
            }
        }
    }
}

} // namespace

View view_of(uint32_t name)
{
    View view;
    view.waiting = get_waiting();
    const int index = nodes::get_find(name);
    if (index < 0 || !in_level()) {
        return view;
    }
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    view.terrains = grind_terrain_names();
    if (node.cls == pedestrian_class() || node.cls == vehicle_class()) {
        view.show = view.object = true;
        for (const uint32_t link : level_links_now(name)) {
            if (family_of_name(link) == nodes::Family::kRouteWay) {
                view.path_start_ = object_edit::caption(link);
            }
        }
        return view;
    }
    view.rail = node.family == nodes::Family::kRail;
    view.ledge = node.family == nodes::Family::kLadder && is_ledge_node(name);
    view.ladder = node.family == nodes::Family::kLadder && !view.ledge;
    view.path = node.family == nodes::Family::kRouteWay;
    if (!view.rail && !view.ladder && !view.path && !view.ledge) {
        return view;
    }
    view.show = true;
    view.added = node.added;
    view.points = static_cast<int>(chain_of(name).size());
    if (node.hidden) {
        view.why = "Deleted";
        return view;
    }
    if (view.ladder) {
        view.can_turn_ = view.can_delete_ = true;
        view.facing_degrees_ = node_edit::get_heading_degrees({.x = 0.0F, .y = facing_now(name), .z = 0.0F});
        view.turned = !node.added && facings_state().contains(name);
        return view;
    }
    Graph graph = graph_of(node.family);
    const std::vector<uint32_t>& links = graph[name];
    std::vector<uint32_t> before = predecessors(graph, name);
    if (!links.empty()) {
        view.next = object_edit::caption(links.front());
    }
    if (!before.empty()) {
        view.prev = object_edit::caption(before.front());
    }
    view.can_insert_ = !links.empty();
    view.can_unlink_ = !links.empty();
    view.can_extend_ = is_last(graph, name) || is_first(graph, name);
    view.can_link_ = view.can_extend_;
    view.can_delete_ = true;
    if (view.ledge) {
        read_ledge_hang(node, name, view);
    }
    if (view.rail) {
        read_rail_view(node, name, view);
    }
    return view;
}

} // namespace frontend::level_editor::chains
