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

#include "editor/level_editor.h"
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

namespace frontend::level_editor::nodes {

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
    const int i = static_cast<int>(family);
    return i >= 0 && i < kFamilies ? core::at(kNames, i) : "?";
}

Family get_classify(uint32_t cls, uint32_t type)
{
    static const uint32_t kRestart = name_hash("Restart"), kGenericNode = name_hash("GenericNode"),
                          kGameObject = name_hash("GameObject"), kRailNode = name_hash("RailNode"),
                          kClimbingNode = name_hash("ClimbingNode"), kWaypoint = name_hash("Waypoint"),
                          kLevelObject = name_hash("LevelObject"), kPedestrian = name_hash("Pedestrian"),
                          kVehicle = name_hash("Vehicle"), kLevelGeometry = name_hash("LevelGeometry"),
                          kLevelLight = name_hash("LevelLight"), kEmitterObject = name_hash("EmitterObject"),
                          kParticleObject = name_hash("ParticleObject"), kProximNode = name_hash("ProximNode"),
                          kCrown = name_hash("Crown"),
                          kEditorTrigger =
                              name_hash("ed_trigger"); // (level_editor_triggers.h: a trigger volume's node)
    static const auto kFlags = std::to_array<uint32_t>({name_hash("Flag_Red"),        name_hash("Flag_Blue"),
                                      name_hash("Flag_Green"),      name_hash("Flag_Yellow"),
                                      name_hash("Flag_Red_Base"),   name_hash("Flag_Blue_Base"),
                                      name_hash("Flag_Green_Base"), name_hash("Flag_Yellow_Base")});
    if (cls == kRestart)
        return Family::kSpawn;
    if (cls == kGenericNode)
        return type == kCrown ? Family::kCrown : type == kEditorTrigger ? Family::kFireOff : Family::kOther;
    if (cls == kGameObject) {
        for (const uint32_t flag : kFlags) {
            if (type == flag)
                return Family::kBitMark;
        }
        return Family::kObject;
    }
    if (cls == kRailNode)
        return Family::kRail;
    if (cls == kClimbingNode)
        return Family::kLadder;
    if (cls == kWaypoint)
        return Family::kRouteWay;
    if (cls == kLevelObject || cls == kPedestrian || cls == kVehicle)
        return Family::kObject;
    if (cls == kLevelGeometry)
        return Family::kScenery;
    if (cls == kLevelLight)
        return Family::kLight;
    if (cls == kEmitterObject)
        return Family::kSound;
    if (cls == kParticleObject)
        return Family::kParticle;
    if (cls == kProximNode)
        return Family::kFireOff;
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

uint32_t restart_type_bit(uint32_t type)
{
    struct TypeBit {
        uint32_t checksum, bit;
    };
    static const auto kTypes = std::to_array<TypeBit>({
        {.checksum = name_hash("Player1"), .bit = kPlayer1},
        {.checksum = name_hash("Player2"), .bit = kPlayer2},
        {.checksum = name_hash("Multiplayer"), .bit = kMultiplayer},
        {.checksum = name_hash("Horse"), .bit = kHorse},
        {.checksum = name_hash("CTF"), .bit = kCtf},
        {.checksum = name_hash("CTF_1"), .bit = kCtf},
        {.checksum = name_hash("CTF_2"), .bit = kCtf},
        {.checksum = name_hash("CTF_3"), .bit = kCtf},
        {.checksum = name_hash("CTF_4"), .bit = kCtf},
        {.checksum = name_hash("Team"), .bit = kTeam},
        {.checksum = name_hash("KingOfTheHill"), .bit = kKing},
        {.checksum = name_hash("Generic"), .bit = kGeneric},
    });
    for (const auto& t : kTypes) {
        if (t.checksum == type)
            return t.bit;
    }
    return 0;
}

uint32_t team_colour(const char* text)
{
    if (!text)
        return 0;
    std::string lower = text;
    std::ranges::transform(lower, lower.begin(),
                           [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    auto has = [&](const char* colour) {
        // (a whole word: _red, _red_base, _red01 - not "_reduce")
        for (size_t at = lower.find(colour); at != std::string::npos; at = lower.find(colour, at + 1)) {
            const size_t end = at + std::strlen(colour);
            if (end == lower.size() || core::at(lower, end) == '_' || (core::at(lower, end) >= '0' && core::at(lower, end) <= '9'))
                return true;
        }
        return false;
    };
    if (has("_red"))
        return kRed;
    if (has("_blue"))
        return kBlue;
    if (has("_green"))
        return kGreen;
    if (has("_yellow"))
        return kYellow;
    return 0;
}

uint32_t spawn_colour(uint32_t types, uint32_t team)
{
    if (team && (types & (kCtf | kTeam)))
        return team;
    if (types & kMultiplayer)
        return 0xFF4F9DFF;
    if (types & kPlayer1)
        return 0xFF5BD47A;
    if (types & kPlayer2)
        return 0xFFB5E36A;
    if (types & kHorse)
        return 0xFFF0943C;
    return 0xFFB8BCC2;
}

// --- the scan ---

namespace {

constexpr uint32_t kClass = 0x12B4E660;  // class
constexpr uint32_t kType = 0x7321A8D6;   // type
constexpr uint32_t kName = 0xA1DC81F9;   // name
constexpr uint32_t kLinks = 0x2E7D5EE7;  // links
constexpr uint32_t kPos = 0x7F261953;    // pos
constexpr uint32_t kAngles = 0x9D2D0915; // angles

std::vector<Elem> g_nodes;
std::unordered_map<uint32_t, int>& by_name()
{
    static std::unordered_map<uint32_t, int> value;
    return value;
}
std::array<int, kFamilies> g_counts;
// The node arrays as last scanned: each one's name, size and script array.
struct ArraySignature {
    uint32_t label;
    uint32_t extent;
    const script::Array* nodes;
    bool operator==(const ArraySignature&) const = default;
};
std::vector<ArraySignature> g_signature;
int g_walked_crowns, g_walked_spawns;
uint32_t g_generation;

std::vector<ArraySignature> get_signature()
{
    std::vector<ArraySignature> signature;
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager)
        return signature;
    for (uint32_t i = 0; i < manager->num_ && i < level_nodes::NodeManager::kMaxArrays; ++i) {
        const level_nodes::NodeArrayEntry& entry = core::at(manager->arrays_, i);
        signature.push_back({.label = entry.label_, .extent = entry.node_entries_ ? entry.node_entries_->extent_ : 0, .nodes = entry.node_entries_});
    }
    return signature;
}

script::Struct* get_node_at(script::Array* nodes, uint32_t index)
{
    if (!nodes || index >= nodes->extent_ || nodes->kind_tag_ != script::kQbvalStruct)
        return nullptr;
    return nodes->struct_at(index);
}

// The heading the game gives a node of these angles (0x0041A730: the identity turned by them - the skater's restart,
// GameObjects::Rider's reset; not rewritten yet): its at row, flattened.
V3 get_heading_of(V3 angles)
{
    maths::Mat4 matrix{};
    core::at(matrix.row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(matrix.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(matrix.row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(matrix.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    const maths::Vec4 vector = {.x = angles.x, .y = angles.y, .z = angles.z, .w = 1.0f};
    matrix.init_from_angles(vector);
    const V3 at = {.x = core::at(matrix.row, 2).x, .y = 0.0f, .z = core::at(matrix.row, 2).z};
    const float length = std::sqrt(at.x * at.x + at.z * at.z);
    return length > 1e-4f ? V3{.x = at.x / length, .y = 0.0f, .z = at.z / length} : V3{.x = 0.0f, .y = 0.0f, .z = 1.0f};
}

void get_scan()
{
    g_nodes.clear();
    by_name().clear();
    std::memset(g_counts.data(), 0, sizeof(g_counts));
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager)
        return;
    static const uint32_t kRestartTypes = name_hash("restart_types"), kColor = name_hash("Color"),
                          kOuterRadius = name_hash("OuterRadius"), kInnerRadius = name_hash("InnerRadius"),
                          kRadius = name_hash("Radius"), kSurfaceKindKey = name_hash("TerrainType"),
                          kTrickObject = name_hash("TrickObject"), kTriggerScript = name_hash("TriggerScript");
    int crowns = 0, restarts = 0;
    ++g_generation;
    for (uint32_t a = 0; a < manager->num_ && a < level_nodes::NodeManager::kMaxArrays; ++a) {
        script::Array* nodes = core::at(manager->arrays_, a).node_entries_;
        if (!nodes || nodes->kind_tag_ != script::kQbvalStruct)
            continue;
        for (uint32_t i = 0; i < nodes->extent_; ++i) {
            script::Struct* s = get_node_at(nodes, i);
            if (!s)
                continue;
            Elem node;
            node.array = static_cast<int>(a);
            node.index = static_cast<int>(i);
            s->read_hash_value(kName, &node.name, 0);
            s->read_hash_value(kClass, &node.cls, 0);
            s->read_hash_value(kType, &node.type, 0);
            node.added = core::at(manager->arrays_, a).label_ == overlay_array();
            if (node.cls == hidden_marker()) {
                // (hidden by the editor: its own class and type kept beside the marker)
                node.hidden = true;
                node.cls = node.type = 0;
                s->read_hash_value(hidden_class_key(), &node.cls, 0);
                s->read_hash_value(hidden_type_key(), &node.type, 0);
            }
            node.family = get_classify(node.cls, node.type);
            script::VectorValue v = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            if (s->fetch_vec(kPos, &v, 0)) {
                node.has_pos_ = true;
                node.pos = {.x = v.x, .y = v.y, .z = v.z};
            }
            script::VectorValue angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            if (s->fetch_vec(kAngles, &angles, 0)) {
                node.angles = {.x = angles.x, .y = angles.y, .z = angles.z};
                if (shape_of(node.family) == Shape::kPoint)
                    node.forward = get_heading_of(node.angles);
            }
            const char* name = object_edit::get_name_of(node.name);
            switch (node.family) {
            case Family::kSpawn: {
                node.order = node.hidden ? -1 : restarts++; // (the game's walks skip a hidden one)
                script::Array* types = nullptr;
                if (s->fetch_list(kRestartTypes, &types, 0) && types) {
                    for (uint32_t t = 0; t < types->extent_; ++t)
                        node.restart_types_ |= restart_type_bit(types->fetch_part(t));
                }
                node.colour = spawn_colour(node.restart_types_, team_colour(name));
                break;
            }
            case Family::kCrown:
                node.order = node.hidden ? -1 : crowns++;
                node.colour = 0xFFFFC233;
                break;
            case Family::kBitMark: {
                const uint32_t team = team_colour(object_edit::get_name_of(node.type));
                node.colour = team ? team : team_colour(name);
                if (!node.colour)
                    node.colour = 0xFFE0E0E0;
                break;
            }
            case Family::kRail:
            case Family::kLadder: {
                s->read_hash_value(kSurfaceKindKey, &node.terrain, 0);
                node.trick_object_ = s->has_bare_flag(kTrickObject);
                static const uint32_t kMetal = name_hash("Metal"), kWood = name_hash("Wood"),
                                      kConcrete = name_hash("Concrete");
                node.colour = node.family == Family::kLadder ? 0xFFB98CFF
                              : node.type == kMetal          ? 0xFF7FB2D9
                              : node.type == kWood           ? 0xFFC9935A
                              : node.type == kConcrete       ? 0xFFC8C8C8
                                                             : 0xFF9FE0E0;
                break;
            }
            case Family::kRouteWay:
                node.colour = 0xFF8FD18F;
                break;
            case Family::kLight: {
                script::Array* colour = nullptr;
                if (s->fetch_list(kColor, &colour, 0) && colour && colour->extent_ >= 3) {
                    auto c = [&](uint32_t k) {
                        const int value = colour->kind_tag_ == script::kQbvalFloat
                                              ? static_cast<int>(colour->fetch_float(k))
                                              : static_cast<int>(colour->fetch_part(k));
                        return static_cast<uint32_t>(std::max(0, std::min(255, value)));
                    };
                    node.colour = 0xFF000000u | (c(0) << 16) | (c(1) << 8) | c(2);
                } else {
                    node.colour = 0xFFFFE9A8;
                }
                s->fetch_float(kOuterRadius, &node.radius, 0);
                s->fetch_float(kInnerRadius, &node.inner_radius_, 0);
                break;
            }
            case Family::kSound:
                node.colour = 0xFF4FD1C5;
                break;
            case Family::kParticle:
                node.colour = 0xFFE07BE0;
                break;
            case Family::kFireOff:
                node.colour = 0xFFF08A3C;
                s->fetch_float(kRadius, &node.radius, 0);
                break;
            case Family::kObject:
                node.colour = 0xFFA8B0BA;
                break;
            default:
                node.colour = 0xFF9AA0A6;
                break;
            }
            node.trigger_script_ = s->has_entry_named(kTriggerScript);
            ++core::at(g_counts, static_cast<int>(node.family));
            if (node.name)
                by_name().emplace(node.name, static_cast<int>(g_nodes.size()));
            g_nodes.push_back(std::move(node));
        }
    }
    g_walked_crowns = crowns;
    g_walked_spawns = restarts;
    // The links, as indices (links= holds the linked nodes' names once the array is added - resolve_links).
    for (Elem& node : g_nodes) {
        script::Struct* s = get_node_at(core::at(manager->arrays_, node.array).node_entries_, static_cast<uint32_t>(node.index));
        script::Array* links = nullptr;
        if (!s || !s->fetch_list(kLinks, &links, 0) || !links)
            continue;
        for (uint32_t l = 0; l < links->extent_; ++l) {
            auto it = by_name().find(links->fetch_part(l));
            if (it != by_name().end())
                node.links.push_back(it->second);
        }
    }
}

// Rails and ladders where the rail manager has them now: a sector moved takes its rails' points with it, and a rail
// point moves by itself (level_editor_node_edit.h), in the rail manager only - the node array keeps the level's pos=.
// Skaters grind the rail manager's points, so the gizmos and the Inspector show those (read again whenever rail
// points move: Scenery::rail_generation).
uint32_t g_rail_generation;

void sync_rails()
{
    g_rail_generation = scenery::rail_generation();
    std::unordered_map<uint32_t, V3> points;
    scenery::for_each_rail_point(
        [](void* context, uint32_t name, V3 pos) {
            static_cast<std::unordered_map<uint32_t, V3>*>(context)->emplace(name, pos);
        },
        &points);
    if (points.empty())
        return;
    for (Elem& node : g_nodes) {
        if (node.family != Family::kRail && node.family != Family::kLadder)
            continue;
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
    if (signature != g_signature) {
        g_signature = signature;
        get_scan();
        sync_rails();
    } else if (scenery::rail_generation() != g_rail_generation) {
        sync_rails();
    }
}

} // namespace

const std::vector<Elem>& get_all()
{
    get_refresh();
    return g_nodes;
}

const std::array<int, kFamilies>& get_counts()
{
    get_refresh();
    return g_counts;
}

int walked(Family family)
{
    get_refresh();
    return family == Family::kCrown   ? g_walked_crowns
           : family == Family::kSpawn ? g_walked_spawns
                                      : core::at(g_counts, static_cast<int>(family));
}

uint32_t get_generation()
{
    get_refresh();
    return g_generation;
}

void mark_stale() { g_signature.clear(); }

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
    if (!manager || node.array < 0 || ints::as_unsigned(node.array) >= manager->num_)
        return nullptr;
    return get_node_at(core::at(manager->arrays_, node.array).node_entries_, static_cast<uint32_t>(node.index));
}

void reread(int index)
{
    if (index < 0 || std::cmp_greater_equal(index, g_nodes.size()))
        return;
    Elem& node = core::at(g_nodes, index);
    script::Struct* s = struct_of(node);
    if (!s)
        return;
    script::VectorValue v = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (s->fetch_vec(kPos, &v, 0)) {
        node.has_pos_ = true;
        node.pos = {.x = v.x, .y = v.y, .z = v.z};
    }
    script::VectorValue angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    s->fetch_vec(kAngles, &angles, 0);
    node.angles = {.x = angles.x, .y = angles.y, .z = angles.z};
    if (shape_of(node.family) == Shape::kPoint)
        node.forward = get_heading_of(node.angles);
    // (a rail / ladder point: where the rail manager has it - see sync_rails)
    V3 point{};
    if ((node.family == Family::kRail || node.family == Family::kLadder) &&
        scenery::get_rail_point(node.name, &point, true))
        node.pos = point;
}

uint32_t get_array_name(const Elem& node)
{
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager || node.array < 0 || ints::as_unsigned(node.array) >= manager->num_)
        return 0;
    return core::at(manager->arrays_, node.array).label_;
}

std::string caption(uint32_t checksum) { return object_edit::caption(checksum); }

namespace {

std::string get_name_text(uint32_t checksum)
{
    if (const char* name = object_edit::get_name_of(checksum))
        return name;
    std::array<char, 16> hex{};
    text::format_to(hex, "#%08X", checksum);
    return hex.data();
}

std::string value_text(script::Component* c, int depth);

std::string array_text(script::Array* array, int depth)
{
    if (!array)
        return "[]";
    std::string text = "[";
    const uint32_t shown = std::min<uint32_t>(array->extent_, 12);
    for (uint32_t i = 0; i < shown; ++i) {
        if (i)
            text += " ";
        std::array<char, 48> buffer{};
        switch (array->kind_tag_) {
        case script::kQbvalInteger:
            text::format_to(buffer, "%d", static_cast<int>(array->fetch_part(i)));
            text += buffer.data();
            break;
        case script::kQbvalFloat:
            text::format_to(buffer, "%g", array->fetch_float(i));
            text += buffer.data();
            break;
        case script::kQbvalName:
            text += get_name_text(array->fetch_part(i));
            break;
        case script::kQbvalString:
            text += std::string("\"") + (array->fetch_text(i) ? array->fetch_text(i) : "") + "\"";
            break;
        case script::kQbvalStruct:
            text += depth < 2 ? "{...}" : "{}";
            break;
        default:
            text += "?";
            break;
        }
    }
    if (array->extent_ > shown)
        text += " ... (" + std::to_string(array->extent_) + ")";
    return text + "]";
}

std::string value_text(script::Component* c, int depth)
{
    std::array<char, 96> buffer{};
    switch (c->kind_tag()) {
    case script::kQbvalInteger:
        text::format_to(buffer, "%d", c->value.integer());
        return buffer.data();
    case script::kQbvalFloat:
        text::format_to(buffer, "%g", c->value.real());
        return buffer.data();
    case script::kQbvalString:
    case script::kQbvalInlineText:
        return std::string("\"") + (c->value.text() ? c->value.text() : "") + "\"";
    case script::kQbvalPair:
        if (c->value.pair())
            text::format_to(buffer, "(%g, %g)", c->value.pair()->x, c->value.pair()->y);
        return c->value.pair() ? buffer.data() : "()";
    case script::kQbvalVector:
        if (c->value.vector())
            text::format_to(buffer, "(%.2f, %.2f, %.2f)", c->value.vector()->x, c->value.vector()->y,
                            c->value.vector()->z);
        return c->value.vector() ? buffer.data() : "()";
    case script::kQbvalName:
        return get_name_text(c->value.word());
    case script::kQbvalArray:
    case script::kEsymboltypeArraypointer:
        return array_text(c->value.array(), depth + 1);
    case script::kQbvalStruct:
    case script::kQbvalStructRef:
        return "{...}";
    case script::kQbvalScriptBody:
        return "(a script)";
    default:
        return "?";
    }
}

void add_fields(script::Struct* s, bool included, int depth, std::vector<Prop>* out)
{
    if (!s || depth > 3)
        return;
    for (script::Component* c = s->head_; c; c = c->next) {
        if (c->name == 0 && c->kind_tag() == script::kQbvalName) {
            // A bare name: a flag, or an included structure (ncomp_<class>_<n>) - expanded.
            const script::SymbolTableEntry* symbol = script::resolve_symbol(c->value.word());
            if (symbol && symbol->type == script::kQbvalStruct && symbol->value.structure()) {
                add_fields(symbol->value.structure(), true, depth + 1, out);
                continue;
            }
            out->push_back({.name = get_name_text(c->value.word()), .value = "(flag)", .included = included});
            continue;
        }
        out->push_back({.name = c->name ? get_name_text(c->name) : "(unnamed)", .value = value_text(c, depth), .included = included});
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
    const modules::Skate* skate = modules::g_skate_module.get(); // the skate module
    if (!skate)
        return false;
    const uint32_t level = skate->level_; // its level's load script
    if (!level)
        return false;
    struct LevelList {
        const char* list;
        const char* game;
    };
    static constexpr auto kLists = std::to_array<LevelList>({
        {.list = "level_select_menu_thps3_level_info", .game = "THPS3"}, {.list = "level_select_menu_thps4_level_info", .game = "THPS4"},
        {.list = "level_select_menu_thug1_level_info", .game = "THUG1"}, {.list = "level_select_menu_thaw_level_info", .game = "THAW"},
        {.list = "level_select_menu_misc_level_info", .game = nullptr}, // (skate shops, tutorials: the entry's game_tag names the game)
    });
    static const uint32_t kLevel = name_hash("level"), kGameTag = name_hash("game_tag");
    for (const auto& entry : kLists) {
        script::Array* levels = script::fetch_list(entry.list);
        if (!levels || levels->kind_tag_ != script::kQbvalStruct)
            continue;
        for (uint32_t i = 0; i < levels->extent_; ++i) {
            auto* info = levels->struct_at(i);
            uint32_t load = 0;
            if (info && info->read_hash_value(kLevel, &load, 0) && load == level) {
                const char* tag = entry.game;
                if (!tag && !info->fetch_text(kGameTag, &tag, 0))
                    break; // (a THUG2 or THP8 level of the miscellaneous list: not shared with another game's)
                if (game)
                    *game = tag;
                return true;
            }
        }
    }
    return false;
}

int pick_nearest(const std::vector<Pickable>& pickables, float x, float y, float radius, const Occluder& occluder)
{
    int best = -1;
    float best_distance = 0.0f, best_depth = 0.0f;
    bool best_point = false;
    for (const Pickable& p : pickables) {
        const float d = p.segment ? distance_to_segment(x, y, p.ax, p.ay, p.bx, p.by)
                                  : std::sqrt((x - p.ax) * (x - p.ax) + (y - p.ay) * (y - p.ay));
        if (d > radius)
            continue;
        if (occluder.blocked) {
            // (where a segment passes nearest: its depth there, perspective-correct - 1 / depth is linear on screen)
            float depth = p.depth;
            if (p.segment && p.depth_a_ > 0.0f && p.depth_b_ > 0.0f) {
                const float dx = p.bx - p.ax;
                const float dy = p.by - p.ay;
                const float length2 = dx * dx + dy * dy;
                const float s = length2 > 0.0f
                                    ? std::max(0.0f, std::min(1.0f, ((x - p.ax) * dx + (y - p.ay) * dy) / length2))
                                    : 0.0f;
                depth = 1.0f / ((1.0f - s) / p.depth_a_ + s / p.depth_b_);
            }
            if (!picking::gizmo_in_front(depth, true, occluder.depth,
                                         picking::gizmo_slack(depth, occluder.units_per_pixel_, occluder.pixels)))
                continue;
        }
        const bool point = !p.segment;
        bool better = false;
        if (best < 0)
            better = true;
        else if (point != best_point)
            better = point; // (a point within reach beats any segment)
        else if (std::fabs(d - best_distance) > 0.5f)
            better = d < best_distance;
        else
            better = p.depth < best_depth;
        if (better) {
            best = p.node;
            best_distance = d;
            best_depth = p.depth;
            best_point = point;
        }
    }
    return best;
}

} // namespace frontend::level_editor::nodes
