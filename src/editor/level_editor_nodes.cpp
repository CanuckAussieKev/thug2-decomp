// NOT ORIGINAL - the level editor's node layer (ours). See level_editor_nodes.h.

#include "editor/level_editor_nodes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/enum_ops.h"
#include "editor/level_editor.h"
#include "engine/maths/vec2.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "engine/maths/mat4.h"
#include "engine/script/node_manager.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/vec4.h"

namespace frontend::level_editor::nodes {

namespace {

constexpr std::uint32_t kMultiplayerSpawnColour = 0xFF4F9DFF; // spawn node colours, by who they are for
constexpr std::uint32_t kPlayer1SpawnColour = 0xFF5BD47A;
constexpr std::uint32_t kPlayer2SpawnColour = 0xFFB5E36A;
constexpr std::uint32_t kHorseSpawnColour = 0xFFF0943C;
constexpr std::uint32_t kOtherSpawnColour = 0xFFB8BCC2;
constexpr float kMinHeadingLength = 1e-4F;         // a heading shorter than this has no direction
constexpr std::uint32_t kCrownColour = 0xFFFFC233; // node colours, by family
constexpr std::uint32_t kFlagFallbackColour = 0xFFE0E0E0; // a flag of no known team
constexpr std::uint32_t kLadderColour = 0xFFB98CFF;
constexpr std::uint32_t kMetalRailColour = 0xFF7FB2D9;
constexpr std::uint32_t kWoodRailColour = 0xFFC9935A;
constexpr std::uint32_t kConcreteRailColour = 0xFFC8C8C8;
constexpr std::uint32_t kOtherRailColour = 0xFF9FE0E0;
constexpr std::uint32_t kRouteWayColour = 0xFF8FD18F;
constexpr int kChannelMax = 255;
constexpr std::uint32_t kDefaultLightColour = 0xFFFFE9A8; // a light with no colour of its own
constexpr std::uint32_t kSoundColour = 0xFF4FD1C5;
constexpr std::uint32_t kParticleColour = 0xFFE07BE0;
constexpr std::uint32_t kFireOffColour = 0xFFF08A3C;
constexpr std::uint32_t kObjectColour = 0xFFA8B0BA;
constexpr std::uint32_t kOtherNodeColour = 0xFF9AA0A6;
constexpr std::size_t kHexTextSize = 16;
constexpr std::size_t kNameBufferSize = 48;
constexpr std::size_t kValueBufferSize = 96;
constexpr float kDistanceTolerance = 0.5F; // two picks this close in depth count as at the same distance

} // namespace

namespace {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// Colours (ARGB).
constexpr uint32_t kRed = 0xFFE5484D, kBlue = 0xFF3E8BFF, kGreen = 0xFF46C46E, kYellow = 0xFFF2C94C;

} // namespace

const char* family_name(Family family)
{
    static const std::array<const char*, kFamilies> kNames = {{"Spawns",    "Crowns",   "Flags",   "Rails",  "Ladders",
                                                  "Paths",     "Objects",  "Scenery", "Lights", "Sounds",
                                                  "Particles", "Triggers", "Other"}};
    const int index = static_cast<int>(family);
    return index >= 0 && index < kFamilies ? core::at(kNames, index) : "?";
}

Family get_classify(NodeClass node_class)
{
    const auto cls = node_class.cls;
    const auto type = node_class.type;
    static const uint32_t kRestart = name_hash("Restart");
    static const uint32_t kGenericNode = name_hash("GenericNode");
    static const uint32_t kGameObject = name_hash("GameObject");
    static const uint32_t kRailNode = name_hash("RailNode");
    static const uint32_t kClimbingNode = name_hash("ClimbingNode");
    static const uint32_t kWaypoint = name_hash("Waypoint");
    static const uint32_t kLevelObject = name_hash("LevelObject");
    static const uint32_t kPedestrian = name_hash("Pedestrian");
    static const uint32_t kVehicle = name_hash("Vehicle");
    static const uint32_t kLevelGeometry = name_hash("LevelGeometry");
    static const uint32_t kLevelLight = name_hash("LevelLight");
    static const uint32_t kEmitterObject = name_hash("EmitterObject");
    static const uint32_t kParticleObject = name_hash("ParticleObject");
    static const uint32_t kProximNode = name_hash("ProximNode");
    static const uint32_t kCrown = name_hash("Crown");
    static const uint32_t kEditorTrigger =
        name_hash("ed_trigger"); // (level_editor_triggers.h: a trigger volume's node)
    static const auto kFlags = std::to_array<uint32_t>({name_hash("Flag_Red"),        name_hash("Flag_Blue"),
                                      name_hash("Flag_Green"),      name_hash("Flag_Yellow"),
                                      name_hash("Flag_Red_Base"),   name_hash("Flag_Blue_Base"),
                                      name_hash("Flag_Green_Base"), name_hash("Flag_Yellow_Base")});
    if (cls == kRestart) {
        return Family::kSpawn;
    }
    if (cls == kGenericNode) {
        if (type == kCrown) {
            return Family::kCrown;
        }
        return type == kEditorTrigger ? Family::kFireOff : Family::kOther;
    }
    if (cls == kGameObject) {
        for (const uint32_t flag : kFlags) {
            if (type == flag) {
                return Family::kBitMark;
            }
        }
        return Family::kObject;
    }
    if (cls == kRailNode) {
        return Family::kRail;
    }
    if (cls == kClimbingNode) {
        return Family::kLadder;
    }
    if (cls == kWaypoint) {
        return Family::kRouteWay;
    }
    if (cls == kLevelObject || cls == kPedestrian || cls == kVehicle) {
        return Family::kObject;
    }
    if (cls == kLevelGeometry) {
        return Family::kScenery;
    }
    if (cls == kLevelLight) {
        return Family::kLight;
    }
    if (cls == kEmitterObject) {
        return Family::kSound;
    }
    if (cls == kParticleObject) {
        return Family::kParticle;
    }
    if (cls == kProximNode) {
        return Family::kFireOff;
    }
    return Family::kOther;
}

Shape shape_of(Family family)
{
    switch (family) {
    case Family::kRail:
    case Family::kLadder:
    case Family::kRouteWay:
        return Shape::kChain;
    case Family::kFireOff:
    case Family::kLight:
        return Shape::kLoudness;
    case Family::kScenery:
        return Shape::kNone;
    default:
        return Shape::kPoint;
    }
}

SpawnPointKind restart_type_bit(uint32_t type)
{
    struct TypeBit {
        uint32_t checksum;
        SpawnPointKind bit;
    };
    static const auto kTypes = std::to_array<TypeBit>({
        {.checksum = name_hash("Player1"), .bit = SpawnPointKind::kPlayer1},
        {.checksum = name_hash("Player2"), .bit = SpawnPointKind::kPlayer2},
        {.checksum = name_hash("Multiplayer"), .bit = SpawnPointKind::kMultiplayer},
        {.checksum = name_hash("Horse"), .bit = SpawnPointKind::kHorse},
        {.checksum = name_hash("CTF"), .bit = SpawnPointKind::kCtf},
        {.checksum = name_hash("CTF_1"), .bit = SpawnPointKind::kCtf},
        {.checksum = name_hash("CTF_2"), .bit = SpawnPointKind::kCtf},
        {.checksum = name_hash("CTF_3"), .bit = SpawnPointKind::kCtf},
        {.checksum = name_hash("CTF_4"), .bit = SpawnPointKind::kCtf},
        {.checksum = name_hash("Team"), .bit = SpawnPointKind::kTeam},
        {.checksum = name_hash("KingOfTheHill"), .bit = SpawnPointKind::kKing},
        {.checksum = name_hash("Generic"), .bit = SpawnPointKind::kGeneric},
    });
    for (const auto& type_entry : kTypes) {
        if (type_entry.checksum == type) {
            return type_entry.bit;
        }
    }
    return SpawnPointKind::kNone;
}

uint32_t team_colour(const char* text)
{
    if (!text) {
        return 0;
    }
    std::string lower = text;
    std::ranges::transform(lower, lower.begin(), [](char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
    });
    auto has = [&](const char* colour) {
        // (a whole word: _red, _red_base, _red01 - not "_reduce")
        for (size_t at = lower.find(colour); at != std::string::npos; at = lower.find(colour, at + 1)) {
            const size_t end = at + std::strlen(colour);
            if (end == lower.size() || core::at(lower, end) == '_' ||
                (core::at(lower, end) >= '0' && core::at(lower, end) <= '9')) {
                return true;
            }
        }
        return false;
    };
    if (has("_red")) {
        return kRed;
    }
    if (has("_blue")) {
        return kBlue;
    }
    if (has("_green")) {
        return kGreen;
    }
    if (has("_yellow")) {
        return kYellow;
    }
    return 0;
}

uint32_t spawn_colour(SpawnPointKind types, uint32_t team)
{
    if ((team != 0U) && core::any(types & (SpawnPointKind::kCtf | SpawnPointKind::kTeam))) {
        return team;
    }
    if (core::any(types & SpawnPointKind::kMultiplayer)) {
        return kMultiplayerSpawnColour;
    }
    if (core::any(types & SpawnPointKind::kPlayer1)) {
        return kPlayer1SpawnColour;
    }
    if (core::any(types & SpawnPointKind::kPlayer2)) {
        return kPlayer2SpawnColour;
    }
    if (core::any(types & SpawnPointKind::kHorse)) {
        return kHorseSpawnColour;
    }
    return kOtherSpawnColour;
}

// --- the scan ---

namespace {

constexpr uint32_t kClass = 0x12B4E660;  // class
constexpr uint32_t kType = 0x7321A8D6;   // type
constexpr uint32_t kName = 0xA1DC81F9;   // name
constexpr uint32_t kLinks = 0x2E7D5EE7;  // links
constexpr uint32_t kPos = 0x7F261953;    // pos
constexpr uint32_t kAngles = 0x9D2D0915; // angles

// NOT ORIGINAL grouping - level_editor_nodes.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_nodes_state()).
struct LevelEditorNodesState {
    std::vector<Elem> nodes_;
    std::array<int, kFamilies> counts_{};
};

LevelEditorNodesState& level_editor_nodes_state()
{
    static core::NoDestroy<LevelEditorNodesState> state;
    return state.get();
}

std::unordered_map<uint32_t, int>& by_name()
{
    static std::unordered_map<uint32_t, int> value;
    return value;
}
// The node arrays as last scanned: each one's name, size and script array.
struct ArraySignature {
    uint32_t label;
    uint32_t extent;
    const script::Array* nodes;
    bool operator==(const ArraySignature&) const = default;
};
// NOT ORIGINAL grouping - the nodes' signature, the crowns and spawns walked and the generation (were globals). Made on
// first use, never destroyed (node_walk()).
struct NodeWalk {
    std::vector<ArraySignature> signature_;
    int walked_crowns_{};
    int walked_spawns_{};
    uint32_t generation_{};
};

NodeWalk& node_walk()
{
    static core::NoDestroy<NodeWalk> state;
    return state.get();
}

std::vector<ArraySignature> get_signature()
{
    std::vector<ArraySignature> signature;
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager) {
        return signature;
    }
    for (uint32_t i = 0; i < manager->num() && i < level_nodes::NodeManager::kMaxArrays; ++i) {
        const level_nodes::NodeArrayEntry& entry = core::at(manager->arrays(), i);
        signature.push_back({.label = entry.label_, .extent = entry.node_entries_ ? entry.node_entries_->extent() : 0, .nodes = entry.node_entries_});
    }
    return signature;
}

script::Struct* get_node_at(script::Array* nodes, uint32_t index)
{
    if (!nodes || index >= nodes->extent() || nodes->kind_tag() != script::QbValueKind::kStruct) {
        return nullptr;
    }
    return nodes->struct_at(index);
}

// The heading the game gives a node of these angles (0x0041A730: the identity turned by them - the skater's restart,
// GameObjects::Rider's reset; not rewritten yet): its at row, flattened.
V3 get_heading_of(V3 angles)
{
    maths::Mat4 matrix{};
    core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    const maths::Vec4 vector = {.x = angles.x, .y = angles.y, .z = angles.z, .w = 1.0F};
    matrix.init_from_angles(vector);
    const V3 at = {.x = core::at(matrix.row, 2).x, .y = 0.0F, .z = core::at(matrix.row, 2).z};
    const float length = std::sqrt((at.x * at.x) + (at.z * at.z));
    return length > kMinHeadingLength ? V3{.x = at.x / length, .y = 0.0F, .z = at.z / length}
                                      : V3{.x = 0.0F, .y = 0.0F, .z = 1.0F};
}

void get_scan()
{
    level_editor_nodes_state().nodes_.clear();
    by_name().clear();
    std::memset(level_editor_nodes_state().counts_.data(), 0, sizeof(level_editor_nodes_state().counts_));
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager) {
        return;
    }
    static const uint32_t kRestartTypes = name_hash("restart_types");
    static const uint32_t kColor = name_hash("Color");
    static const uint32_t kOuterRadius = name_hash("OuterRadius");
    static const uint32_t kInnerRadius = name_hash("InnerRadius");
    static const uint32_t kRadius = name_hash("Radius");
    static const uint32_t kSurfaceKindKey = name_hash("TerrainType");
    static const uint32_t kTrickObject = name_hash("TrickObject");
    static const uint32_t kTriggerScript = name_hash("TriggerScript");
    int crowns = 0;
    int restarts = 0;
    ++node_walk().generation_;
    for (uint32_t array_index = 0; array_index < manager->num() && array_index < level_nodes::NodeManager::kMaxArrays;
         ++array_index) {
        script::Array* nodes = core::at(manager->arrays(), array_index).node_entries_;
        if (!nodes || nodes->kind_tag() != script::QbValueKind::kStruct) {
            continue;
        }
        for (uint32_t i = 0; i < nodes->extent(); ++i) {
            script::Struct* structure = get_node_at(nodes, i);
            if (!structure) {
                continue;
            }
            Elem node;
            node.array = static_cast<int>(array_index);
            node.index = static_cast<int>(i);
            structure->read_hash_value(kName, &node.name, 0);
            structure->read_hash_value(kClass, &node.cls, 0);
            structure->read_hash_value(kType, &node.type, 0);
            node.added = core::at(manager->arrays(), array_index).label_ == overlay_array();
            if (node.cls == hidden_marker()) {
                // (hidden by the editor: its own class and type kept beside the marker)
                node.hidden = true;
                node.cls = node.type = 0;
                structure->read_hash_value(hidden_class_key(), &node.cls, 0);
                structure->read_hash_value(hidden_type_key(), &node.type, 0);
            }
            node.family = get_classify({.cls = node.cls, .type = node.type});
            script::VectorValue v = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            if (structure->fetch_vec(kPos, &v, 0)) {
                node.has_pos_ = true;
                node.pos = {.x = v.x, .y = v.y, .z = v.z};
            }
            script::VectorValue angles = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            if (structure->fetch_vec(kAngles, &angles, 0)) {
                node.angles = {.x = angles.x, .y = angles.y, .z = angles.z};
                if (shape_of(node.family) == Shape::kPoint) {
                    node.forward = get_heading_of(node.angles);
                }
            }
            const char* name = object_edit::get_name_of(node.name);
            switch (node.family) {
            case Family::kSpawn: {
                node.order = node.hidden ? -1 : restarts++; // (the game's walks skip a hidden one)
                script::Array* types = nullptr;
                if (structure->fetch_list(kRestartTypes, &types, 0) && types) {
                    for (uint32_t type_index = 0; type_index < types->extent(); ++type_index) {
                        node.restart_types_ |= restart_type_bit(types->fetch_part(type_index));
                    }
                }
                node.colour = spawn_colour(node.restart_types_, team_colour(name));
                break;
            }
            case Family::kCrown:
                node.order = node.hidden ? -1 : crowns++;
                node.colour = kCrownColour;
                break;
            case Family::kBitMark: {
                const uint32_t team = team_colour(object_edit::get_name_of(node.type));
                node.colour = (team != 0U) ? team : team_colour(name);
                if (node.colour == 0U) {
                    node.colour = kFlagFallbackColour;
                }
                break;
            }
            case Family::kRail:
            case Family::kLadder: {
                structure->read_hash_value(kSurfaceKindKey, &node.terrain, 0);
                node.trick_object_ = structure->has_bare_flag(kTrickObject);
                static const uint32_t kMetal = name_hash("Metal");
                static const uint32_t kWood = name_hash("Wood");
                static const uint32_t kConcrete = name_hash("Concrete");
                if (node.family == Family::kLadder) {
                    node.colour = kLadderColour;
                } else if (node.type == kMetal) {
                    node.colour = kMetalRailColour;
                } else if (node.type == kWood) {
                    node.colour = kWoodRailColour;
                } else if (node.type == kConcrete) {
                    node.colour = kConcreteRailColour;
                } else {
                    node.colour = kOtherRailColour;
                }
                break;
            }
            case Family::kRouteWay:
                node.colour = kRouteWayColour;
                break;
            case Family::kLight: {
                script::Array* colour = nullptr;
                if (structure->fetch_list(kColor, &colour, 0) && colour && colour->extent() >= 3) {
                    auto getter = [&](uint32_t index) {
                        const int value = colour->kind_tag() == script::QbValueKind::kFloat
                                              ? static_cast<int>(colour->fetch_float(index))
                                              : static_cast<int>(colour->fetch_part(index));
                        return static_cast<uint32_t>(std::max(0, std::min(kChannelMax, value)));
                    };
                    node.colour = kOpaqueAlpha | (getter(0) << kRedShift) | (getter(1) << kGreenShift) | getter(2);
                } else {
                    node.colour = kDefaultLightColour;
                }
                structure->fetch_float(kOuterRadius, &node.radius, 0);
                structure->fetch_float(kInnerRadius, &node.inner_radius_, 0);
                break;
            }
            case Family::kSound:
                node.colour = kSoundColour;
                break;
            case Family::kParticle:
                node.colour = kParticleColour;
                break;
            case Family::kFireOff:
                node.colour = kFireOffColour;
                structure->fetch_float(kRadius, &node.radius, 0);
                break;
            case Family::kObject:
                node.colour = kObjectColour;
                break;
            default:
                node.colour = kOtherNodeColour;
                break;
            }
            node.trigger_script_ = structure->has_entry_named(kTriggerScript);
            ++core::at(level_editor_nodes_state().counts_, static_cast<int>(node.family));
            if (node.name != 0U) {
                by_name().emplace(node.name, static_cast<int>(level_editor_nodes_state().nodes_.size()));
            }
            level_editor_nodes_state().nodes_.push_back(std::move(node));
        }
    }
    node_walk().walked_crowns_ = crowns;
    node_walk().walked_spawns_ = restarts;
    // The links, as indices (links= holds the linked nodes' names once the array is added - resolve_links).
    for (Elem& node : level_editor_nodes_state().nodes_) {
        script::Struct* structure =
            get_node_at(core::at(manager->arrays(), node.array).node_entries_, static_cast<uint32_t>(node.index));
        script::Array* links = nullptr;
        if (!structure || !structure->fetch_list(kLinks, &links, 0) || !links) {
            continue;
        }
        for (uint32_t link_index = 0; link_index < links->extent(); ++link_index) {
            auto it = by_name().find(links->fetch_part(link_index));
            if (it != by_name().end()) {
                node.links.push_back(it->second);
            }
        }
    }
}

// Rails and ladders where the rail manager has them now: a sector moved takes its rails' points with it, and a rail
// point moves by itself (level_editor_node_edit.h), in the rail manager only - the node array keeps the level's pos=.
// Skaters grind the rail manager's points, so the gizmos and the Inspector show those (read again whenever rail
// points move: Scenery::rail_generation).
uint32_t& stored_rail_generation()
{
    static uint32_t rail_generation{};
    return rail_generation;
}

void sync_rails()
{
    stored_rail_generation() = scenery::rail_generation();
    std::unordered_map<uint32_t, V3> points;
    scenery::for_each_rail_point(
        [](void* context, uint32_t name, V3 pos) {
            static_cast<std::unordered_map<uint32_t, V3>*>(context)->emplace(name, pos);
        },
        &points);
    if (points.empty()) {
        return;
    }
    for (Elem& node : level_editor_nodes_state().nodes_) {
        if (node.family != Family::kRail && node.family != Family::kLadder) {
            continue;
        }
        auto it = points.find(node.name);
        if (it != points.end()) {
            node.pos = it->second;
            node.has_pos_ = true;
        }
    }
}

void get_refresh()
{
    const std::vector<ArraySignature> signature = get_signature();
    if (signature != node_walk().signature_) {
        node_walk().signature_ = signature;
        get_scan();
        sync_rails();
    } else if (scenery::rail_generation() != stored_rail_generation()) {
        sync_rails();
    }
}

} // namespace

const std::vector<Elem>& get_all()
{
    get_refresh();
    return level_editor_nodes_state().nodes_;
}

const std::array<int, kFamilies>& get_counts()
{
    get_refresh();
    return level_editor_nodes_state().counts_;
}

int walked(Family family)
{
    get_refresh();
    if (family == Family::kCrown) {
        return node_walk().walked_crowns_;
    }
    if (family == Family::kSpawn) {
        return node_walk().walked_spawns_;
    }
    return core::at(level_editor_nodes_state().counts_, static_cast<int>(family));
}

uint32_t get_generation()
{
    get_refresh();
    return node_walk().generation_;
}

void mark_stale() { node_walk().signature_.clear(); }

const char* const kOverlayArrayName = "thug2_decomp_editor_nodes";
uint32_t overlay_array()
{
    static const uint32_t kName = name_hash(kOverlayArrayName);
    return kName;
}
uint32_t hidden_marker()
{
    static const uint32_t kName = name_hash("ed_hidden");
    return kName;
}
uint32_t hidden_class_key()
{
    static const uint32_t kName = name_hash("ed_hidden_class");
    return kName;
}
uint32_t hidden_type_key()
{
    static const uint32_t kName = name_hash("ed_hidden_type");
    return kName;
}

int get_find(uint32_t name)
{
    get_refresh();
    auto it = by_name().find(name);
    return it == by_name().end() ? -1 : it->second;
}

script::Struct* struct_of(const Elem& node)
{
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager || node.array < 0 || ints::as_unsigned(node.array) >= manager->num()) {
        return nullptr;
    }
    return get_node_at(core::at(manager->arrays(), node.array).node_entries_, static_cast<uint32_t>(node.index));
}

void reread(int index)
{
    if (index < 0 || std::cmp_greater_equal(index, level_editor_nodes_state().nodes_.size())) {
        return;
    }
    Elem& node = core::at(level_editor_nodes_state().nodes_, index);
    script::Struct* structure = struct_of(node);
    if (!structure) {
        return;
    }
    script::VectorValue v = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (structure->fetch_vec(kPos, &v, 0)) {
        node.has_pos_ = true;
        node.pos = {.x = v.x, .y = v.y, .z = v.z};
    }
    script::VectorValue angles = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    structure->fetch_vec(kAngles, &angles, 0);
    node.angles = {.x = angles.x, .y = angles.y, .z = angles.z};
    if (shape_of(node.family) == Shape::kPoint) {
        node.forward = get_heading_of(node.angles);
    }
    // (a rail / ladder point: where the rail manager has it - see sync_rails)
    V3 point{};
    if ((node.family == Family::kRail || node.family == Family::kLadder) &&
        scenery::get_rail_point(node.name, &point, true)) {
        node.pos = point;
    }
}

uint32_t get_array_name(const Elem& node)
{
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager || node.array < 0 || ints::as_unsigned(node.array) >= manager->num()) {
        return 0;
    }
    return core::at(manager->arrays(), node.array).label_;
}

std::string caption(uint32_t checksum) { return object_edit::caption(checksum); }

namespace {

std::string get_name_text(uint32_t checksum)
{
    if (const char* name = object_edit::get_name_of(checksum)) {
        return name;
    }
    std::array<char, kHexTextSize> hex{};
    text::format_to(hex, "#%08X", checksum);
    return hex.data();
}

std::string value_text(script::Component* component, int depth);

std::string array_text(script::Array* array, int depth)
{
    if (!array) {
        return "[]";
    }
    std::string text = "[";
    const uint32_t shown = std::min<uint32_t>(array->extent(), 12);
    for (uint32_t i = 0; i < shown; ++i) {
        if (i != 0U) {
            text += " ";
        }
        std::array<char, kNameBufferSize> buffer{};
        switch (array->kind_tag()) {
        case script::QbValueKind::kInteger:
            text::format_to(buffer, "%d", static_cast<int>(array->fetch_part(i)));
            text += buffer.data();
            break;
        case script::QbValueKind::kFloat:
            text::format_to(buffer, "%g", array->fetch_float(i));
            text += buffer.data();
            break;
        case script::QbValueKind::kName:
            text += get_name_text(array->fetch_part(i));
            break;
        case script::QbValueKind::kString:
            text += std::string("\"") + (array->fetch_text(i) ? array->fetch_text(i) : "") + "\"";
            break;
        case script::QbValueKind::kStruct:
            text += depth < 2 ? "{...}" : "{}";
            break;
        default:
            text += "?";
            break;
        }
    }
    if (array->extent() > shown) {
        text += " ... (" + std::to_string(array->extent()) + ")";
    }
    return text + "]";
}

std::string value_text(script::Component* component, int depth)
{
    std::array<char, kValueBufferSize> buffer{};
    switch (value_kind(*component)) {
    case script::QbValueKind::kInteger:
        text::format_to(buffer, "%d", component->value.integer());
        return buffer.data();
    case script::QbValueKind::kFloat:
        text::format_to(buffer, "%g", component->value.real());
        return buffer.data();
    case script::QbValueKind::kString:
    case script::QbValueKind::kInlineText:
        return std::string("\"") + (component->value.text() ? component->value.text() : "") + "\"";
    case script::QbValueKind::kPair:
        if (component->value.pair()) {
            text::format_to(buffer, "(%g, %g)", component->value.pair()->x(), component->value.pair()->y());
        }
        return component->value.pair() ? buffer.data() : "()";
    case script::QbValueKind::kVector:
        if (component->value.vector()) {
            text::format_to(buffer, "(%.2f, %.2f, %.2f)", component->value.vector()->x(),
                            component->value.vector()->y(), component->value.vector()->z());
        }
        return component->value.vector() ? buffer.data() : "()";
    case script::QbValueKind::kName:
        return get_name_text(component->value.word());
    case script::QbValueKind::kArray:
    case script::QbValueKind::kArrayRef:
        return array_text(component->value.array(), depth + 1);
    case script::QbValueKind::kStruct:
    case script::QbValueKind::kStructRef:
        return "{...}";
    case script::QbValueKind::kScriptBody:
        return "(a script)";
    default:
        return "?";
    }
}

void add_fields(script::Struct* structure, bool included, int depth, std::vector<Prop>* out)
{
    if (!structure || depth > 3) {
        return;
    }
    for (script::Component* component = structure->head(); component; component = component->next) {
        if (component->name == 0 && value_kind(*component) == script::QbValueKind::kName) {
            // A bare name: a flag, or an included structure (ncomp_<class>_<n>) - expanded.
            const script::SymbolTableEntry* symbol = script::resolve_symbol(component->value.word());
            if (symbol && symbol->type() == script::QbValueKind::kStruct && symbol->value().structure()) {
                add_fields(symbol->value().structure(), true, depth + 1, out);
                continue;
            }
            out->push_back({.name = get_name_text(component->value.word()), .value = "(flag)", .included = included});
            continue;
        }
        out->push_back({.name = (component->name != 0U) ? get_name_text(component->name) : "(unnamed)",
                        .value = value_text(component, depth),
                        .included = included});
    }
}

} // namespace

std::vector<Prop> get_fields(const Elem& node)
{
    std::vector<Prop> fields;
    add_fields(struct_of(node), false, 0, &fields);
    return fields;
}

bool level_is_shared(const char** game)
{
    const modules::Skate* skate = modules::Skate::sole(); // the skate module
    if (!skate) {
        return false;
    }
    const uint32_t level = skate->level(); // its level's load script
    if (level == 0U) {
        return false;
    }
    struct LevelList {
        const char* list;
        const char* game;
    };
    static constexpr auto kLists = std::to_array<LevelList>({
        {.list = "level_select_menu_thps3_level_info", .game = "THPS3"}, {.list = "level_select_menu_thps4_level_info", .game = "THPS4"},
        {.list = "level_select_menu_thug1_level_info", .game = "THUG1"}, {.list = "level_select_menu_thaw_level_info", .game = "THAW"},
        {.list = "level_select_menu_misc_level_info", .game = nullptr}, // (skate shops, tutorials: the entry's game_tag names the game)
    });
    static const uint32_t kLevel = name_hash("level");
    static const uint32_t kGameTag = name_hash("game_tag");
    for (const auto& entry : kLists) {
        script::Array* levels = script::fetch_list(entry.list);
        if (!levels || levels->kind_tag() != script::QbValueKind::kStruct) {
            continue;
        }
        for (uint32_t i = 0; i < levels->extent(); ++i) {
            auto* info = levels->struct_at(i);
            uint32_t load = 0;
            if (info && info->read_hash_value(kLevel, &load, 0) && load == level) {
                const char* tag = entry.game;
                if (!tag && !info->fetch_text(kGameTag, &tag, 0)) {
                    break; // (a THUG2 or THP8 level of the miscellaneous list: not shared with another game's)
                }
                if (game) {
                    *game = tag;
                }
                return true;
            }
        }
    }
    return false;
}

int pick_nearest(const std::vector<Pickable>& pickables, maths::Vec2 at, float radius, const Occluder& occluder)
{
    const float x = at.x;
    const float y = at.y;
    int best = -1;
    float best_distance = 0.0F;
    float best_depth = 0.0F;
    bool best_point = false;
    for (const Pickable& pickable : pickables) {
        const float distance =
            pickable.segment
                ? distance_to_segment(at, {.x = pickable.ax, .y = pickable.ay}, {.x = pickable.bx, .y = pickable.by})
                : std::sqrt(((x - pickable.ax) * (x - pickable.ax)) + ((y - pickable.ay) * (y - pickable.ay)));
        if (distance > radius) {
            continue;
        }
        if (occluder.blocked) {
            // (where a segment passes nearest: its depth there, perspective-correct - 1 / depth is linear on screen)
            float depth = pickable.depth;
            if (pickable.segment && pickable.depth_a_ > 0.0F && pickable.depth_b_ > 0.0F) {
                const float dx = pickable.bx - pickable.ax;
                const float dy = pickable.by - pickable.ay;
                const float length2 = (dx * dx) + (dy * dy);
                const float scale =
                    length2 > 0.0F
                        ? std::max(0.0F,
                                   std::min(1.0F, (((x - pickable.ax) * dx) + ((y - pickable.ay) * dy)) / length2))
                        : 0.0F;
                depth = 1.0F / (((1.0F - scale) / pickable.depth_a_) + (scale / pickable.depth_b_));
            }
            if (!picking::gizmo_in_front(depth, true, occluder.depth,
                                         picking::gizmo_slack(depth, occluder.units_per_pixel_, occluder.pixels))) {
                continue;
            }
        }
        const bool point = !pickable.segment;
        bool better = false;
        if (best < 0) {
            better = true;
        } else if (point != best_point) {
            better = point; // (a point within reach beats any segment)
        } else if (std::fabs(distance - best_distance) > kDistanceTolerance) {
            better = distance < best_distance;
        } else {
            better = pickable.depth < best_depth;
        }
        if (better) {
            best = pickable.node;
            best_distance = distance;
            best_depth = pickable.depth;
            best_point = point;
        }
    }
    return best;
}

} // namespace frontend::level_editor::nodes
