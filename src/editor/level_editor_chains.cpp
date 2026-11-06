// NOT ORIGINAL - the level editor's chains: grind rails, ladders and paths (ours). See level_editor_chains.h.
// This file holds the graph, the level's nodes and the chains' state, the per-frame tick and the view; the pieces
// (chains_records / chains_restore / chains_ops) are level_editor_chains_<name>.cpp, sharing level_editor_chains_internal.h.

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
#include "base/maths/trig.h"

namespace frontend::level_editor::chains {
// What the source file's anonymous namespace held that its pieces share (level_editor_chains_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::chains

namespace frontend::level_editor::chains {
namespace {

constexpr float kMinFlatNormalY = 0.7F; // a surface normal this upright is floor, not wall
constexpr float kTinyLength = 1e-6F;
constexpr float kGroundProbeRise = 4.0F;   // the ground under a ladder foot is looked for from this far above ...
constexpr float kGroundProbeDepth = 64.0F; // ... down to this far below it
constexpr float kParallelEpsilon = 1e-6F;  // lines this close to parallel have no nearest point
constexpr float kFacingEpsilon = 1e-4F;    // two facings closer than this are the same
constexpr float kSameSegmentTolerance = 0.01F;

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
    return maths::runtime32::atan2(-normal.x / length, -normal.z / length);
}

V3 facing_direction(float facing) { return {.x = maths::runtime32::sin(facing), .y = 0.0F, .z = maths::runtime32::cos(facing)}; }

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

RebuiltRails& rebuilt_rails()
{
    static core::NoDestroy<RebuiltRails> rails;
    return rails.get();
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
    return maths::runtime32::atan2(core::at(matrix.row, 2).x, core::at(matrix.row, 2).z);
}

} // namespace
namespace internal {

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
    if (std::ranges::find(rebuilt_rails().remake_, name) == rebuilt_rails().remake_.end()) {
        rebuilt_rails().remake_.push_back(name);
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
    if (!rebuilt_rails().retired_.empty()) {
        const objects::GrindRailHub* rails = get_rails();
        if (!rails || rails->nodes() != rebuilt_rails().built_) {
            for (void* nodes : rebuilt_rails().retired_) {
                memory::free_malloc(nodes);
            }
            rebuilt_rails().retired_.clear();
            rebuilt_rails().built_ = nullptr;
        }
    }
    apply_speeds();
    if (rebuilt_rails().remake_.empty()) {
        return;
    }
    if (!in_level()) {
        rebuilt_rails().remake_.clear();
        return;
    }
    for (auto it = rebuilt_rails().remake_.begin(); it != rebuilt_rails().remake_.end();) {
        if (objects::find_object(*it)) {
            ++it; // (not removed yet: the game deletes dead objects in its update)
            continue;
        }
        if (script::Struct* node = look_up_node_params(*it)) {
            commands::create_from_node(node);
        }
        get_log("level editor: %s made again (its path from its links)", object_edit::caption(*it).c_str());
        it = rebuilt_rails().remake_.erase(it);
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
