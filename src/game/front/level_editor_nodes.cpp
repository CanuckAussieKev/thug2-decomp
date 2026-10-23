// NOT ORIGINAL - the level editor's node layer (ours). See level_editor_nodes.h.

#include "game/front/level_editor_nodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scenery.h"
#include "game/mth/matrix.h"
#include "game/script/names.h"
#include "game/script/node_manager.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game.h"

namespace Front::LevelEditor::Nodes {

namespace {

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

// Colours (ARGB).
constexpr uint32_t kRed = 0xFFE5484D, kBlue = 0xFF3E8BFF, kGreen = 0xFF46C46E, kYellow = 0xFFF2C94C;

} // namespace

const char* FamilyName(Family family)
{
    static const char* const kNames[kFamilies] = {"Spawns", "Crowns",  "Flags",     "Rails",    "Ladders",
                                                  "Paths",  "Objects", "Scenery",   "Lights",   "Sounds",
                                                  "Particles", "Triggers", "Other"};
    int i = static_cast<int>(family);
    return i >= 0 && i < kFamilies ? kNames[i] : "?";
}

Family Classify(uint32_t cls, uint32_t type)
{
    static const uint32_t kRestart = Checksum("Restart"), kGenericNode = Checksum("GenericNode"),
                          kGameObject = Checksum("GameObject"), kRailNode = Checksum("RailNode"),
                          kClimbingNode = Checksum("ClimbingNode"), kWaypoint = Checksum("Waypoint"),
                          kLevelObject = Checksum("LevelObject"), kPedestrian = Checksum("Pedestrian"),
                          kVehicle = Checksum("Vehicle"), kLevelGeometry = Checksum("LevelGeometry"),
                          kLevelLight = Checksum("LevelLight"), kEmitterObject = Checksum("EmitterObject"),
                          kParticleObject = Checksum("ParticleObject"), kProximNode = Checksum("ProximNode"),
                          kCrown = Checksum("Crown"),
                          kEditorTrigger = Checksum("ed_trigger"); // (level_editor_triggers.h: a trigger volume's node)
    static const uint32_t kFlags[] = {Checksum("Flag_Red"),       Checksum("Flag_Blue"),       Checksum("Flag_Green"),
                                      Checksum("Flag_Yellow"),    Checksum("Flag_Red_Base"),   Checksum("Flag_Blue_Base"),
                                      Checksum("Flag_Green_Base"), Checksum("Flag_Yellow_Base")};
    if (cls == kRestart)
        return Family::Spawn;
    if (cls == kGenericNode)
        return type == kCrown ? Family::Crown : type == kEditorTrigger ? Family::Trigger : Family::Other;
    if (cls == kGameObject) {
        for (uint32_t flag : kFlags) {
            if (type == flag)
                return Family::Flag;
        }
        return Family::Object;
    }
    if (cls == kRailNode)
        return Family::Rail;
    if (cls == kClimbingNode)
        return Family::Ladder;
    if (cls == kWaypoint)
        return Family::Path;
    if (cls == kLevelObject || cls == kPedestrian || cls == kVehicle)
        return Family::Object;
    if (cls == kLevelGeometry)
        return Family::Scenery;
    if (cls == kLevelLight)
        return Family::Light;
    if (cls == kEmitterObject)
        return Family::Sound;
    if (cls == kParticleObject)
        return Family::Particle;
    if (cls == kProximNode)
        return Family::Trigger;
    return Family::Other;
}

Shape ShapeOf(Family family)
{
    switch (family) {
    case Family::Rail:
    case Family::Ladder:
    case Family::Path:
        return Shape::Chain;
    case Family::Trigger:
    case Family::Light:
        return Shape::Volume;
    case Family::Scenery:
        return Shape::None;
    default:
        return Shape::Point;
    }
}

uint32_t RestartTypeBit(uint32_t type)
{
    static const struct {
        uint32_t checksum, bit;
    } kTypes[] = {
        {Checksum("Player1"), kPlayer1},        {Checksum("Player2"), kPlayer2},  {Checksum("Multiplayer"), kMultiplayer},
        {Checksum("Horse"), kHorse},            {Checksum("CTF"), kCtf},          {Checksum("CTF_1"), kCtf},
        {Checksum("CTF_2"), kCtf},              {Checksum("CTF_3"), kCtf},        {Checksum("CTF_4"), kCtf},
        {Checksum("Team"), kTeam},              {Checksum("KingOfTheHill"), kKing}, {Checksum("Generic"), kGeneric},
    };
    for (const auto& t : kTypes) {
        if (t.checksum == type)
            return t.bit;
    }
    return 0;
}

uint32_t TeamColour(const char* text)
{
    if (!text)
        return 0;
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    auto has = [&](const char* colour) {
        // (a whole word: _red, _red_base, _red01 - not "_reduce")
        for (size_t at = lower.find(colour); at != std::string::npos; at = lower.find(colour, at + 1)) {
            size_t end = at + std::strlen(colour);
            if (end == lower.size() || lower[end] == '_' || (lower[end] >= '0' && lower[end] <= '9'))
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

uint32_t SpawnColour(uint32_t types, uint32_t team)
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

constexpr uint32_t kClass = 0x12B4E660;   // class
constexpr uint32_t kType = 0x7321A8D6;    // type
constexpr uint32_t kName = 0xA1DC81F9;    // name
constexpr uint32_t kLinks = 0x2E7D5EE7;   // links
constexpr uint32_t kPos = 0x7F261953;     // pos
constexpr uint32_t kAngles = 0x9D2D0915;  // angles

std::vector<Node> g_nodes;
std::unordered_map<uint32_t, int> g_byName;
int g_counts[kFamilies];
std::vector<uint32_t> g_signature; // (the arrays' names and sizes when scanned)
int g_walkedCrowns, g_walkedSpawns;
uint32_t g_generation;

std::vector<uint32_t> Signature()
{
    std::vector<uint32_t> signature;
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (!manager)
        return signature;
    for (uint32_t i = 0; i < manager->m_count && i < SkateScript::CNodeManager::kMaxArrays; ++i) {
        const SkateScript::NodeArrayEntry& entry = manager->m_arrays[i];
        signature.push_back(entry.m_name);
        signature.push_back(entry.mp_nodes ? entry.mp_nodes->m_size : 0);
        signature.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry.mp_nodes)));
    }
    return signature;
}

Script::CStruct* NodeAt(Script::CArray* nodes, uint32_t index)
{
    if (!nodes || index >= nodes->m_size || nodes->m_type != Script::ESYMBOLTYPE_STRUCTURE)
        return nullptr;
    return reinterpret_cast<Script::CStruct*>(nodes->GetElement(index));
}

// The heading the game gives a node of these angles (0x0041A730: the identity turned by them - the skater's restart,
// Obj::CSkater's reset; not rewritten yet): its at row, flattened.
V3 HeadingOf(V3 angles)
{
    Mth::Matrix matrix;
    matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector vector = {angles.x, angles.y, angles.z, 1.0f};
    matrix.InitFromAngles(vector);
    V3 at = {matrix.row[2].x, 0.0f, matrix.row[2].z};
    float length = std::sqrt(at.x * at.x + at.z * at.z);
    return length > 1e-4f ? V3{at.x / length, 0.0f, at.z / length} : V3{0.0f, 0.0f, 1.0f};
}

void Scan()
{
    g_nodes.clear();
    g_byName.clear();
    std::memset(g_counts, 0, sizeof(g_counts));
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (!manager)
        return;
    static const uint32_t kRestartTypes = Checksum("restart_types"), kColor = Checksum("Color"),
                          kOuterRadius = Checksum("OuterRadius"), kInnerRadius = Checksum("InnerRadius"),
                          kRadius = Checksum("Radius"), kTerrainType = Checksum("TerrainType"),
                          kTrickObject = Checksum("TrickObject"), kTriggerScript = Checksum("TriggerScript");
    int crowns = 0, restarts = 0;
    ++g_generation;
    for (uint32_t a = 0; a < manager->m_count && a < SkateScript::CNodeManager::kMaxArrays; ++a) {
        Script::CArray* nodes = manager->m_arrays[a].mp_nodes;
        if (!nodes || nodes->m_type != Script::ESYMBOLTYPE_STRUCTURE)
            continue;
        for (uint32_t i = 0; i < nodes->m_size; ++i) {
            Script::CStruct* s = NodeAt(nodes, i);
            if (!s)
                continue;
            Node node;
            node.array = static_cast<int>(a);
            node.index = static_cast<int>(i);
            s->GetChecksum(kName, &node.name, 0);
            s->GetChecksum(kClass, &node.cls, 0);
            s->GetChecksum(kType, &node.type, 0);
            node.added = manager->m_arrays[a].m_name == OverlayArray();
            if (node.cls == HiddenMarker()) {
                // (hidden by the editor: its own class and type kept beside the marker)
                node.hidden = true;
                node.cls = node.type = 0;
                s->GetChecksum(HiddenClassKey(), &node.cls, 0);
                s->GetChecksum(HiddenTypeKey(), &node.type, 0);
            }
            node.family = Classify(node.cls, node.type);
            Script::VectorValue v = {0.0f, 0.0f, 0.0f, 1.0f};
            if (s->GetVector(kPos, &v, 0)) {
                node.hasPos = true;
                node.pos = {v.x, v.y, v.z};
            }
            Script::VectorValue angles = {0.0f, 0.0f, 0.0f, 1.0f};
            if (s->GetVector(kAngles, &angles, 0)) {
                node.angles = {angles.x, angles.y, angles.z};
                if (ShapeOf(node.family) == Shape::Point)
                    node.forward = HeadingOf(node.angles);
            }
            const char* name = Objects::NameOf(node.name);
            switch (node.family) {
            case Family::Spawn: {
                node.order = node.hidden ? -1 : restarts++; // (the game's walks skip a hidden one)
                Script::CArray* types = nullptr;
                if (s->GetArray(kRestartTypes, &types, 0) && types) {
                    for (uint32_t t = 0; t < types->m_size; ++t)
                        node.restartTypes |= RestartTypeBit(types->GetElement(t));
                }
                node.colour = SpawnColour(node.restartTypes, TeamColour(name));
                break;
            }
            case Family::Crown:
                node.order = node.hidden ? -1 : crowns++;
                node.colour = 0xFFFFC233;
                break;
            case Family::Flag: {
                uint32_t team = TeamColour(Objects::NameOf(node.type));
                node.colour = team ? team : TeamColour(name);
                if (!node.colour)
                    node.colour = 0xFFE0E0E0;
                break;
            }
            case Family::Rail:
            case Family::Ladder: {
                s->GetChecksum(kTerrainType, &node.terrain, 0);
                node.trickObject = s->ContainsFlag(kTrickObject);
                static const uint32_t kMetal = Checksum("Metal"), kWood = Checksum("Wood"),
                                      kConcrete = Checksum("Concrete");
                node.colour = node.family == Family::Ladder ? 0xFFB98CFF
                              : node.type == kMetal         ? 0xFF7FB2D9
                              : node.type == kWood          ? 0xFFC9935A
                              : node.type == kConcrete      ? 0xFFC8C8C8
                                                            : 0xFF9FE0E0;
                break;
            }
            case Family::Path:
                node.colour = 0xFF8FD18F;
                break;
            case Family::Light: {
                Script::CArray* colour = nullptr;
                if (s->GetArray(kColor, &colour, 0) && colour && colour->m_size >= 3) {
                    auto c = [&](uint32_t k) {
                        int value = colour->m_type == Script::ESYMBOLTYPE_FLOAT ? static_cast<int>(colour->GetFloat(k))
                                                                                 : static_cast<int>(colour->GetElement(k));
                        return static_cast<uint32_t>(std::max(0, std::min(255, value)));
                    };
                    node.colour = 0xFF000000u | (c(0) << 16) | (c(1) << 8) | c(2);
                } else {
                    node.colour = 0xFFFFE9A8;
                }
                s->GetFloat(kOuterRadius, &node.radius, 0);
                s->GetFloat(kInnerRadius, &node.innerRadius, 0);
                break;
            }
            case Family::Sound:
                node.colour = 0xFF4FD1C5;
                break;
            case Family::Particle:
                node.colour = 0xFFE07BE0;
                break;
            case Family::Trigger:
                node.colour = 0xFFF08A3C;
                s->GetFloat(kRadius, &node.radius, 0);
                break;
            case Family::Object:
                node.colour = 0xFFA8B0BA;
                break;
            default:
                node.colour = 0xFF9AA0A6;
                break;
            }
            node.triggerScript = s->ContainsComponentNamed(kTriggerScript);
            ++g_counts[static_cast<int>(node.family)];
            if (node.name)
                g_byName.emplace(node.name, static_cast<int>(g_nodes.size()));
            g_nodes.push_back(std::move(node));
        }
    }
    g_walkedCrowns = crowns;
    g_walkedSpawns = restarts;
    // The links, as indices (links= holds the linked nodes' names once the array is added - ResolveLinks).
    for (Node& node : g_nodes) {
        Script::CStruct* s = NodeAt(manager->m_arrays[node.array].mp_nodes, static_cast<uint32_t>(node.index));
        Script::CArray* links = nullptr;
        if (!s || !s->GetArray(kLinks, &links, 0) || !links)
            continue;
        for (uint32_t l = 0; l < links->m_size; ++l) {
            auto it = g_byName.find(links->GetElement(l));
            if (it != g_byName.end())
                node.links.push_back(it->second);
        }
    }
}

// Rails and ladders where the rail manager has them now: a sector moved takes its rails' points with it, and a rail
// point moves by itself (level_editor_node_edit.h), in the rail manager only - the node array keeps the level's pos=.
// Skaters grind the rail manager's points, so the gizmos and the Inspector show those (read again whenever rail
// points move: Scenery::RailGeneration).
uint32_t g_railGeneration;

void SyncRails()
{
    g_railGeneration = Scenery::RailGeneration();
    std::unordered_map<uint32_t, V3> points;
    Scenery::ForEachRailPoint(
        [](void* context, uint32_t name, V3 pos) { static_cast<std::unordered_map<uint32_t, V3>*>(context)->emplace(name, pos); },
        &points);
    if (points.empty())
        return;
    for (Node& node : g_nodes) {
        if (node.family != Family::Rail && node.family != Family::Ladder)
            continue;
        auto it = points.find(node.name);
        if (it != points.end()) {
            node.pos = it->second;
            node.hasPos = true;
        }
    }
}

void Refresh()
{
    std::vector<uint32_t> signature = Signature();
    if (signature != g_signature) {
        g_signature = signature;
        Scan();
        SyncRails();
    } else if (Scenery::RailGeneration() != g_railGeneration) {
        SyncRails();
    }
}

} // namespace

const std::vector<Node>& All()
{
    Refresh();
    return g_nodes;
}

const int* Counts()
{
    Refresh();
    return g_counts;
}

int Walked(Family family)
{
    Refresh();
    return family == Family::Crown ? g_walkedCrowns : family == Family::Spawn ? g_walkedSpawns : g_counts[static_cast<int>(family)];
}

uint32_t Generation()
{
    Refresh();
    return g_generation;
}

void Invalidate() { g_signature.clear(); }

const char* const kOverlayArrayName = "thug2_decomp_editor_nodes";
uint32_t OverlayArray()
{
    static const uint32_t name = Checksum(kOverlayArrayName);
    return name;
}
uint32_t HiddenMarker()
{
    static const uint32_t name = Checksum("ed_hidden");
    return name;
}
uint32_t HiddenClassKey()
{
    static const uint32_t name = Checksum("ed_hidden_class");
    return name;
}
uint32_t HiddenTypeKey()
{
    static const uint32_t name = Checksum("ed_hidden_type");
    return name;
}

int Find(uint32_t name)
{
    Refresh();
    auto it = g_byName.find(name);
    return it == g_byName.end() ? -1 : it->second;
}

Script::CStruct* StructOf(const Node& node)
{
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (!manager || node.array < 0 || static_cast<uint32_t>(node.array) >= manager->m_count)
        return nullptr;
    return NodeAt(manager->m_arrays[node.array].mp_nodes, static_cast<uint32_t>(node.index));
}

void Reread(int index)
{
    if (index < 0 || index >= static_cast<int>(g_nodes.size()))
        return;
    Node& node = g_nodes[index];
    Script::CStruct* s = StructOf(node);
    if (!s)
        return;
    Script::VectorValue v = {0.0f, 0.0f, 0.0f, 1.0f};
    if (s->GetVector(kPos, &v, 0)) {
        node.hasPos = true;
        node.pos = {v.x, v.y, v.z};
    }
    Script::VectorValue angles = {0.0f, 0.0f, 0.0f, 1.0f};
    s->GetVector(kAngles, &angles, 0);
    node.angles = {angles.x, angles.y, angles.z};
    if (ShapeOf(node.family) == Shape::Point)
        node.forward = HeadingOf(node.angles);
    // (a rail / ladder point: where the rail manager has it - see SyncRails)
    V3 point;
    if ((node.family == Family::Rail || node.family == Family::Ladder) && Scenery::RailPoint(node.name, &point, true))
        node.pos = point;
}

uint32_t ArrayName(const Node& node)
{
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (!manager || node.array < 0 || static_cast<uint32_t>(node.array) >= manager->m_count)
        return 0;
    return manager->m_arrays[node.array].m_name;
}

std::string Label(uint32_t checksum) { return Objects::Label(checksum); }

namespace {

std::string NameText(uint32_t checksum)
{
    if (const char* name = Objects::NameOf(checksum))
        return name;
    char hex[16];
    std::snprintf(hex, sizeof(hex), "#%08X", checksum);
    return hex;
}

std::string ValueText(Script::CComponent* c, int depth);

std::string ArrayText(Script::CArray* array, int depth)
{
    if (!array)
        return "[]";
    std::string text = "[";
    uint32_t shown = std::min<uint32_t>(array->m_size, 12);
    for (uint32_t i = 0; i < shown; ++i) {
        if (i)
            text += " ";
        char buffer[48];
        switch (array->m_type) {
        case Script::ESYMBOLTYPE_INTEGER:
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(array->GetElement(i)));
            text += buffer;
            break;
        case Script::ESYMBOLTYPE_FLOAT:
            std::snprintf(buffer, sizeof(buffer), "%g", array->GetFloat(i));
            text += buffer;
            break;
        case Script::ESYMBOLTYPE_NAME:
            text += NameText(array->GetElement(i));
            break;
        case Script::ESYMBOLTYPE_STRING:
            text += std::string("\"") + (array->GetString(i) ? array->GetString(i) : "") + "\"";
            break;
        case Script::ESYMBOLTYPE_STRUCTURE:
            text += depth < 2 ? "{...}" : "{}";
            break;
        default:
            text += "?";
            break;
        }
    }
    if (array->m_size > shown)
        text += " ... (" + std::to_string(array->m_size) + ")";
    return text + "]";
}

std::string ValueText(Script::CComponent* c, int depth)
{
    char buffer[96];
    switch (c->Type()) {
    case Script::ESYMBOLTYPE_INTEGER:
        std::snprintf(buffer, sizeof(buffer), "%d", c->integer);
        return buffer;
    case Script::ESYMBOLTYPE_FLOAT:
        std::snprintf(buffer, sizeof(buffer), "%g", c->floatValue);
        return buffer;
    case Script::ESYMBOLTYPE_STRING:
    case Script::ESYMBOLTYPE_LOCALSTRING:
        return std::string("\"") + (c->string ? c->string : "") + "\"";
    case Script::ESYMBOLTYPE_PAIR:
        if (c->pair)
            std::snprintf(buffer, sizeof(buffer), "(%g, %g)", c->pair->x, c->pair->y);
        return c->pair ? buffer : "()";
    case Script::ESYMBOLTYPE_VECTOR:
        if (c->vector)
            std::snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f)", c->vector->x, c->vector->y, c->vector->z);
        return c->vector ? buffer : "()";
    case Script::ESYMBOLTYPE_NAME:
        return NameText(c->checksum);
    case Script::ESYMBOLTYPE_ARRAY:
    case Script::ESYMBOLTYPE_ARRAYPOINTER:
        return ArrayText(c->array, depth + 1);
    case Script::ESYMBOLTYPE_STRUCTURE:
    case Script::ESYMBOLTYPE_STRUCTUREPOINTER:
        return "{...}";
    case Script::ESYMBOLTYPE_QSCRIPT:
        return "(a script)";
    default:
        return "?";
    }
}

void AddFields(Script::CStruct* s, bool included, int depth, std::vector<Field>* out)
{
    if (!s || depth > 3)
        return;
    for (Script::CComponent* c = s->m_head; c; c = c->next) {
        if (c->name == 0 && c->Type() == Script::ESYMBOLTYPE_NAME) {
            // A bare name: a flag, or an included structure (ncomp_<class>_<n>) - expanded.
            Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(c->checksum);
            if (symbol && symbol->type == Script::ESYMBOLTYPE_STRUCTURE && symbol->structure) {
                AddFields(symbol->structure, true, depth + 1, out);
                continue;
            }
            out->push_back({NameText(c->checksum), "(flag)", included});
            continue;
        }
        out->push_back({c->name ? NameText(c->name) : "(unnamed)", ValueText(c, depth), included});
    }
}

} // namespace

std::vector<Field> Fields(const Node& node)
{
    std::vector<Field> fields;
    AddFields(StructOf(node), false, 0, &fields);
    return fields;
}

bool LevelIsShared(const char** game)
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478); // the skate module
    if (!skate)
        return false;
    uint32_t level = *reinterpret_cast<uint32_t*>(skate + 0x378); // its level's load script
    if (!level)
        return false;
    static const struct {
        const char* list;
        const char* game;
    } kLists[] = {
        {"level_select_menu_thps3_level_info", "THPS3"},
        {"level_select_menu_thps4_level_info", "THPS4"},
        {"level_select_menu_thug1_level_info", "THUG1"},
        {"level_select_menu_thaw_level_info", "THAW"},
    };
    static const uint32_t kLevel = Checksum("level");
    for (const auto& entry : kLists) {
        Script::CArray* levels = Script::GetArray(entry.list);
        if (!levels || levels->m_type != Script::ESYMBOLTYPE_STRUCTURE)
            continue;
        for (uint32_t i = 0; i < levels->m_size; ++i) {
            auto* info = reinterpret_cast<Script::CStruct*>(levels->GetElement(i));
            uint32_t load = 0;
            if (info && info->GetChecksum(kLevel, &load, 0) && load == level) {
                if (game)
                    *game = entry.game;
                return true;
            }
        }
    }
    return false;
}

int PickNearest(const std::vector<Pickable>& pickables, float x, float y, float radius, const Occluder& occluder)
{
    int best = -1;
    float bestDistance = 0.0f, bestDepth = 0.0f;
    bool bestPoint = false;
    for (const Pickable& p : pickables) {
        float d = p.segment ? DistanceToSegment(x, y, p.ax, p.ay, p.bx, p.by)
                            : std::sqrt((x - p.ax) * (x - p.ax) + (y - p.ay) * (y - p.ay));
        if (d > radius)
            continue;
        if (occluder.blocked) {
            // (where a segment passes nearest: its depth there, perspective-correct - 1 / depth is linear on screen)
            float depth = p.depth;
            if (p.segment && p.depthA > 0.0f && p.depthB > 0.0f) {
                float dx = p.bx - p.ax, dy = p.by - p.ay, length2 = dx * dx + dy * dy;
                float s = length2 > 0.0f ? std::max(0.0f, std::min(1.0f, ((x - p.ax) * dx + (y - p.ay) * dy) / length2))
                                         : 0.0f;
                depth = 1.0f / ((1.0f - s) / p.depthA + s / p.depthB);
            }
            if (!Picking::GizmoInFront(depth, true, occluder.depth,
                                       Picking::GizmoSlack(depth, occluder.unitsPerPixel, occluder.pixels)))
                continue;
        }
        bool point = !p.segment;
        bool better;
        if (best < 0)
            better = true;
        else if (point != bestPoint)
            better = point; // (a point within reach beats any segment)
        else if (std::fabs(d - bestDistance) > 0.5f)
            better = d < bestDistance;
        else
            better = p.depth < bestDepth;
        if (better) {
            best = p.node;
            bestDistance = d;
            bestDepth = p.depth;
            bestPoint = point;
        }
    }
    return best;
}

} // namespace Front::LevelEditor::Nodes
