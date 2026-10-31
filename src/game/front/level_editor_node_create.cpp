// NOT ORIGINAL - the level editor's added and hidden nodes (ours). See level_editor_node_create.h.

#include "game/front/level_editor_node_create.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_objects.h"
#include "game/gfx/scene_lights.h"
#include "game/gfx/screen.h"
#include "game/mth/matrix.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sys/log.h"
#include "game/gfx/light_manager.h"

namespace Front::LevelEditor::NodeCreate {

namespace {

bool SameText(const std::string& a, const char* b)
{
    if (a.size() != std::strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}
bool StartsWith(const std::string& text, const char* prefix)
{
    size_t n = std::strlen(prefix);
    return text.size() >= n && SameText(text.substr(0, n), prefix);
}
bool EndsWith(const std::string& text, const char* suffix)
{
    size_t n = std::strlen(suffix);
    return text.size() >= n && SameText(text.substr(text.size() - n), suffix);
}

const char* const kTeamNames[kTeams] = {"Red", "Blue", "Green", "Yellow"};

// The team a name-driven node's name or type ends in ("Red"...; -1: none).
int TeamOf(const std::string& text)
{
    std::string t = text;
    if (EndsWith(t, "_Base"))
        t.resize(t.size() - 5);
    for (int i = 0; i < kTeams; ++i) {
        std::string suffix = std::string("_") + kTeamNames[i];
        if (EndsWith(t, suffix.c_str()))
            return i;
    }
    return -1;
}

std::string Lower(std::string text)
{
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

constexpr float kTwoPi = 6.28318531f;

float Wrapped(float yaw)
{
    while (yaw > 3.14159265f)
        yaw -= kTwoPi;
    while (yaw <= -3.14159265f)
        yaw += kTwoPi;
    return yaw;
}

} // namespace

const char* KindName(Kind kind)
{
    switch (kind) {
    case Kind::Spawn: return "Multiplayer spawn";
    case Kind::TeamSpawn: return "Team spawn";
    case Kind::Crown: return "Crown";
    case Kind::FlagSet: return "CTF flag set";
    case Kind::Light: return "Level light";
    case Kind::Sound: return "Sound emitter";
    case Kind::Proxim: return "Proximity node (sphere)";
    }
    return "?";
}

const char* TeamName(Team team)
{
    int i = static_cast<int>(team);
    return i >= 0 && i < kTeams ? kTeamNames[i] : "?";
}

bool SameSpec(const Spec& a, const Spec& b)
{
    return a.name == b.name && a.cls == b.cls && a.type == b.type && a.pos.x == b.pos.x && a.pos.y == b.pos.y &&
           a.pos.z == b.pos.z && a.angles.x == b.angles.x && a.angles.y == b.angles.y && a.angles.z == b.angles.z &&
           a.types == b.types && a.colour[0] == b.colour[0] && a.colour[1] == b.colour[1] &&
           a.colour[2] == b.colour[2] && a.brightness == b.brightness && a.inner == b.inner && a.outer == b.outer &&
           a.radius == b.radius && a.like == b.like && a.terrain == b.terrain && a.links == b.links &&
           a.owned == b.owned && a.extra.size() == b.extra.size() &&
           std::equal(a.extra.begin(), a.extra.end(), b.extra.begin(), [](const Field& x, const Field& y) {
               return x.type == y.type && x.key == y.key && x.text == y.text && x.value[0] == y.value[0] &&
                      x.value[1] == y.value[1] && x.value[2] == y.value[2] && x.integer == y.integer &&
                      x.list == y.list && x.integers == y.integers;
           });
}

bool KindOf(const Spec& spec, Kind* kind)
{
    if (spec.owned)
        return false; // (another module's node, its fields its own: e.g. a goal's restart - level_editor_goals.h)
    if (SameText(spec.cls, "Restart"))
        *kind = StartsWith(spec.name, "TRG_CTF_Restart_") ? Kind::TeamSpawn : Kind::Spawn;
    else if (SameText(spec.cls, "GenericNode") && SameText(spec.type, "Crown"))
        *kind = Kind::Crown;
    else if (SameText(spec.cls, "GameObject") && StartsWith(spec.type, "Flag_") && TeamOf(spec.type) >= 0)
        *kind = Kind::FlagSet;
    else if (SameText(spec.cls, "LevelLight"))
        *kind = Kind::Light;
    else if (SameText(spec.cls, "EmitterObject"))
        *kind = Kind::Sound;
    else if (SameText(spec.cls, "ProximNode"))
        *kind = Kind::Proxim;
    else
        return false;
    return true;
}

Spec Default(Kind kind, Team team, V3 pos, float yaw, const std::string& name)
{
    Spec s;
    s.name = name;
    s.pos = pos;
    s.angles = {0.0f, Wrapped(yaw), 0.0f};
    std::string colour = TeamName(team);
    switch (kind) {
    case Kind::Spawn:
        s.cls = "Restart";
        s.type = "MultiPlayer";
        s.types = {"MultiPlayer", "Horse"};
        break;
    case Kind::TeamSpawn:
        s.cls = "Restart";
        s.type = "CTF";
        s.types = {"CTF", "MultiPlayer", "Horse"};
        break;
    case Kind::Crown:
        s.cls = "GenericNode";
        s.type = "Crown";
        break;
    case Kind::FlagSet:
        s.cls = "GameObject";
        s.type = "Flag_" + colour;
        break;
    case Kind::Light:
        s.cls = "LevelLight";
        s.colour[0] = 255;
        s.colour[1] = 214;
        s.colour[2] = 170;
        s.brightness = 100.0f;
        s.inner = 150.0f;
        s.outer = 400.0f;
        break;
    case Kind::Sound:
        s.cls = "EmitterObject";
        s.radius = 300.0f;
        break;
    case Kind::Proxim:
        s.cls = "ProximNode";
        s.radius = 200.0f;
        break;
    }
    return s;
}

Spec Base(const Spec& flag)
{
    Spec base = flag;
    base.name = flag.name + "_Base";
    base.type = flag.type + "_Base";
    return base;
}

std::string FixedName(Kind kind, Team team)
{
    if (kind == Kind::TeamSpawn)
        return std::string("TRG_CTF_Restart_") + TeamName(team);
    if (kind == Kind::FlagSet)
        return std::string("TRG_CTF_") + TeamName(team);
    return "";
}

std::string LobbyFlagName(Team team) { return std::string("TRG_Flag_") + TeamName(team); }

bool IsLobbyFlag(const std::string& name) { return StartsWith(name, "TRG_Flag_"); }

const char* NameStem(Kind kind)
{
    switch (kind) {
    case Kind::Spawn:
    case Kind::TeamSpawn: return "ed_restart";
    case Kind::Crown: return "ed_crown";
    case Kind::FlagSet: return "ed_flag";
    case Kind::Light: return "ed_light";
    case Kind::Sound: return "ed_sound";
    case Kind::Proxim: return "ed_proxim";
    }
    return "ed_node";
}

std::string FreshName(const std::string& stem, const std::function<bool(const std::string&)>& taken)
{
    for (int n = 1; n < 100000; ++n) {
        char number[16];
        std::snprintf(number, sizeof(number), "%02d", n);
        std::string name = stem + number;
        if (!taken(name))
            return name;
    }
    return "";
}

bool ValidName(const std::string& name)
{
    if (name.empty() || name.size() > 63)
        return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

std::vector<Field> Fields(const Spec& spec)
{
    std::vector<Field> fields;
    auto name = [&](const char* key, const std::string& value) {
        Field f = {Field::kName, key};
        f.text = value;
        fields.push_back(f);
    };
    auto flag = [&](const char* value) {
        Field f = {Field::kFlag, ""};
        f.text = value;
        fields.push_back(f);
    };
    auto vector = [&](const char* key, V3 v) {
        Field f = {Field::kVector, key};
        f.value[0] = v.x;
        f.value[1] = v.y;
        f.value[2] = v.z;
        fields.push_back(f);
    };
    auto number = [&](const char* key, float v) {
        Field f = {Field::kFloat, key};
        f.value[0] = v;
        fields.push_back(f);
    };
    auto integer = [&](const char* key, int v) {
        Field f = {Field::kInteger, key};
        f.integer = v;
        fields.push_back(f);
    };
    auto string = [&](const char* key, const std::string& v) {
        Field f = {Field::kString, key};
        f.text = v;
        fields.push_back(f);
    };
    vector("Pos", spec.pos);
    vector("Angles", spec.angles);
    name("Name", spec.name);
    name("Class", spec.cls);
    if (!spec.type.empty())
        name("Type", spec.type);
    // Chain points (level_editor_chains.h): made at the start; a rail's terrain (the rail manager reads it - an
    // unknown one would crash 0x004442C0, so a known one always: the editor's default, a grind metal).
    if (SameText(spec.cls, "RailNode") || SameText(spec.cls, "Waypoint")) {
        flag("CreatedAtStart");
        if (SameText(spec.cls, "RailNode"))
            name("TerrainType", spec.terrain.empty() ? std::string("TERRAIN_GRINDMETAL") : spec.terrain);
        return fields;
    }
    if (SameText(spec.cls, "ClimbingNode")) {
        flag("CreatedAtStart"); // (a ladder's point - its Type Ladder above: as the levels' ladders)
        fields.insert(fields.end(), spec.extra.begin(), spec.extra.end()); // (a ledge's HangLeft / HangRight)
        return fields;
    }
    Kind kind;
    if (!KindOf(spec, &kind)) {
        fields.insert(fields.end(), spec.extra.begin(), spec.extra.end()); // (another module's node)
        return fields;
    }
    int team = TeamOf(kind == Kind::FlagSet ? spec.type : spec.name);
    std::string colour = team >= 0 ? kTeamNames[team] : "";
    switch (kind) {
    case Kind::Spawn:
    case Kind::TeamSpawn: {
        flag("CreatedAtStart");
        string("RestartName", kind == Kind::TeamSpawn ? "Team: CTF " + colour : std::string("Multi: Restart"));
        Field types = {Field::kNames, "restart_types"};
        types.list = spec.types;
        fields.push_back(types);
        break;
    }
    case Kind::Crown:
        flag("CreatedAtStart");
        break;
    case Kind::FlagSet: {
        bool base = EndsWith(spec.type, "_Base");
        std::string model = "flag_" + Lower(colour) + (base ? "_base" : "");
        flag("NeverSuspend");
        string("Model", "gameobjects\\flags\\" + model + "\\" + model + ".mdl");
        integer("lod_dist1", 800);
        integer("lod_dist2", 801);
        // (a team lobby flag, TRG_Flag_<Colour>: its level's TRG_Flag_<Colour>Script - SK4_Scripts' Create_Team_Flags
        // makes it by name in a lobby; a CTF flag's TRG_CTF_<Colour>Script)
        if (!base)
            name("TriggerScript", (IsLobbyFlag(spec.name) ? "TRG_Flag_" : "TRG_CTF_") + colour + "Script");
        break;
    }
    case Kind::Light: {
        flag("CreatedAtStart");
        number("Brightness", spec.brightness);
        number("InnerRadius", spec.inner);
        number("OuterRadius", spec.outer);
        Field c = {Field::kIntegers, "Color"};
        c.integers = {spec.colour[0], spec.colour[1], spec.colour[2]};
        fields.push_back(c);
        break;
    }
    case Kind::Sound:
    case Kind::Proxim:
        flag("CreatedAtStart");
        name("Shape", "Sphere");
        number("Radius", spec.radius);
        break;
    }
    fields.insert(fields.end(), spec.extra.begin(), spec.extra.end());
    return fields;
}

const char* LimitMessage(Kind kind, const Counts& counts)
{
    switch (kind) {
    case Kind::Spawn:
    case Kind::TeamSpawn:
        return counts.restarts >= kMaxRestarts ? "Restart limit reached (256)"
                                               : nullptr;
    case Kind::Crown:
        return counts.crowns >= kMaxCrowns ? "Crown limit reached (10)"
                                           : nullptr;
    case Kind::Light:
        if (counts.dynamicLights >= kMaxDynamicLights)
            return "The game's 256 dynamic lights are all in use";
        return counts.levelLights >= kMaxLevelLights ? "The game's 150 level lights are all in use" : nullptr;
    default:
        return nullptr;
    }
}

bool HideLeavesNoRestart(uint32_t types, uint32_t others)
{
    for (uint32_t bit : {static_cast<uint32_t>(Nodes::kPlayer1), static_cast<uint32_t>(Nodes::kMultiplayer)}) {
        if ((types & bit) && !(others & bit))
            return true;
    }
    return false;
}

int IndexOf(const List& list, const std::string& name)
{
    for (size_t i = 0; i < list.size(); ++i) {
        if (SameText(list[i].name, name.c_str()))
            return static_cast<int>(i);
    }
    return -1;
}

int Add(List* list, const Spec& spec)
{
    list->push_back(spec);
    return static_cast<int>(list->size()) - 1;
}

bool Take(List* list, const std::string& name, Spec* spec, int* index)
{
    int i = IndexOf(*list, name);
    if (i < 0)
        return false;
    *spec = (*list)[i];
    *index = i;
    list->erase(list->begin() + i);
    return true;
}

void PutBack(List* list, const Spec& spec, int index)
{
    index = std::max(0, std::min(index, static_cast<int>(list->size())));
    list->insert(list->begin() + index, spec);
}

namespace {
bool IsCrown(const Spec& s)
{
    Kind kind;
    return KindOf(s, &kind) && kind == Kind::Crown;
}
} // namespace

std::vector<std::string> CrownOrder(const List& list)
{
    std::vector<std::string> order;
    for (const Spec& s : list) {
        if (IsCrown(s))
            order.push_back(s.name);
    }
    return order;
}

bool MoveCrown(List* list, const std::string& name, int delta)
{
    int at = IndexOf(*list, name);
    if (at < 0 || !IsCrown((*list)[at]) || (delta != -1 && delta != 1))
        return false;
    for (int i = at + delta; i >= 0 && i < static_cast<int>(list->size()); i += delta) {
        if (IsCrown((*list)[i])) {
            std::swap((*list)[at], (*list)[i]);
            return true;
        }
    }
    return false;
}

void ApplyCrownOrder(List* list, const std::vector<std::string>& order)
{
    std::vector<int> slots;
    std::vector<Spec> crowns;
    for (size_t i = 0; i < list->size(); ++i) {
        if (IsCrown((*list)[i])) {
            slots.push_back(static_cast<int>(i));
            crowns.push_back((*list)[i]);
        }
    }
    std::vector<Spec> ordered;
    std::vector<bool> used(crowns.size(), false);
    for (const std::string& name : order) {
        for (size_t c = 0; c < crowns.size(); ++c) {
            if (!used[c] && SameText(crowns[c].name, name.c_str())) {
                used[c] = true;
                ordered.push_back(crowns[c]);
                break;
            }
        }
    }
    for (size_t c = 0; c < crowns.size(); ++c) {
        if (!used[c])
            ordered.push_back(crowns[c]);
    }
    for (size_t i = 0; i < slots.size(); ++i)
        (*list)[slots[i]] = ordered[i];
}

// --- the game side ----------------------------------------------------------------------------------------------

uint32_t Checksum(const std::string& name) { return Crc::GenerateCRCFromString(name.c_str()); }


namespace {

constexpr uint32_t kClass = 0x12B4E660;  // class
constexpr uint32_t kType = 0x7321A8D6;   // type
constexpr uint32_t kPos = 0x7F261953;    // pos
constexpr uint32_t kAngles = 0x9D2D0915; // angles

// The overlay: its specs (names, classes...; places are the structures') and structures, in array order.
List g_list;
std::vector<Script::CStruct*> g_structs;
Script::CArray* g_array;
bool g_registered;
uint32_t g_levelArray; // (the array the overlay was added after: it leaves with it)

// Hidden nodes: the structure (the level's array still the one loaded) and what was active.
struct HiddenNode {
    uint32_t name;
    Script::CStruct* node;
    bool proximActive, emitterActive;
};
std::vector<HiddenNode> g_hidden;

SkateScript::CNodeManager* Manager() { return SkateScript::NodeManagerInstance(); }

// The game's managers made at the level's load (null outside a level).
bool HaveProxim() { return Obj::g_proximManager != nullptr; }
bool HaveEmitters() { return Obj::g_emitterTable != nullptr; }

void AddLight(Script::CStruct* node) { Nx::CreateLightFromNode(node); }
void RemoveLight(uint32_t name) { Nx::DeleteSceneLight(name); }

void Forget();

void (*g_arrayRemovedListener)(uint32_t name); // (SetArrayRemovedListener)

// The node manager's hook: the level's array going takes the overlay with it.
void OnArrayRemoved(uint32_t name)
{
    if (g_arrayRemovedListener)
        g_arrayRemovedListener(name);
    if (name == Nodes::OverlayArray() || !g_registered || name != g_levelArray)
        return;
    Log("level editor: the level's node array left - its %u added nodes go with it",
        static_cast<unsigned>(g_structs.size()));
    Forget();
}

Script::CStruct* Build(const Spec& spec)
{
    Script::CStruct* node = Script::NewStruct();
    if (!node)
        return nullptr;
    node->Init();
    // A chain point drawn on from another copies that one's fields (its own name, place and links aside; not a trick
    // object of the other's cluster) - level_editor_chains.h.
    Script::CStruct* like = spec.like && Manager() ? SkateScript::GetNode(spec.like, false) : nullptr;
    if (like) {
        node->AppendStructure(like);
        for (const char* key : {"Name", "Pos", "Angles", "Links", "Cluster", "ed_hidden_class", "ed_hidden_type"})
            node->RemoveComponent(Checksum(key));
        node->RemoveFlag(Checksum("TrickObject"));
        node->RemoveFlag(Checksum("ed_grind_flat")); // (its own choice: level_editor_chains.h)
    }
    for (const Field& f : Fields(spec)) {
        uint32_t key = f.key.empty() ? 0u : Checksum(f.key);
        if (like && f.type == Field::kFlag && node->ContainsFlag(Checksum(f.text)))
            continue;
        if (like && f.key == "TerrainType" && spec.terrain.empty())
            continue; // (the copied one)
        if (like && key)
            node->RemoveComponent(key);
        switch (f.type) {
        case Field::kFlag:
            node->AddChecksum(0u, Checksum(f.text));
            break;
        case Field::kName:
            node->AddChecksum(key, Checksum(f.text));
            Script::NoteQbName(Checksum(f.text), f.text.c_str());
            break;
        case Field::kString:
            node->AddString(key, f.text.c_str());
            break;
        case Field::kInteger:
            node->AddInteger(key, f.integer);
            break;
        case Field::kFloat:
            node->AddFloat(key, f.value[0]);
            break;
        case Field::kVector:
            node->AddVector(key, f.value[0], f.value[1], f.value[2]);
            break;
        case Field::kNames:
        case Field::kIntegers: {
            Script::CArray* array = Script::NewArray();
            if (!array)
                break;
            array->Init();
            bool names = f.type == Field::kNames;
            uint32_t count = static_cast<uint32_t>(names ? f.list.size() : f.integers.size());
            array->SetSizeAndType(count, names ? Script::ESYMBOLTYPE_NAME : Script::ESYMBOLTYPE_INTEGER);
            for (uint32_t i = 0; i < count; ++i)
                array->SetElement(i, names ? Checksum(f.list[i]) : static_cast<uint32_t>(f.integers[i]));
            node->AddArrayPointer(key, array);
            break;
        }
        }
    }
    return node;
}

void Free(Script::CStruct* node)
{
    if (!node)
        return;
    node->Clear();
    Script::DeleteStruct(node);
}

constexpr uint32_t kLinks = 0x2E7D5EE7; // links

// A structure's links= set to these names (none: removed).
void WriteLinks(Script::CStruct* node, const std::vector<uint32_t>& links)
{
    node->RemoveComponent(kLinks);
    if (links.empty())
        return;
    Script::CArray* array = Script::NewArray();
    if (!array)
        return;
    array->Init();
    array->SetSizeAndType(static_cast<uint32_t>(links.size()), Script::ESYMBOLTYPE_NAME);
    for (size_t i = 0; i < links.size(); ++i)
        array->SetElement(static_cast<uint32_t>(i), links[i]);
    node->AddArrayPointer(kLinks, array);
}

// The overlay registered again with its structures as they are now (none: not registered).
bool Sync()
{
    SkateScript::CNodeManager* manager = Manager();
    if (!manager)
        return false;
    SkateScript::SetNodeArrayRemovedHook(&OnArrayRemoved);
    SkateScript::SetEditorNodeArray(Nodes::OverlayArray());
    if (g_registered) {
        g_registered = false;
        manager->RemoveNodeArray(Nodes::OverlayArray());
    }
    Nodes::Invalidate();
    if (g_structs.empty())
        return true;
    if (!g_array) {
        uint32_t name = Nodes::OverlayArray();
        Script::NoteQbName(name, Nodes::kOverlayArrayName);
        if (Script::CSymbolTableEntry* old = Script::FindSymbol(name))
            Script::CleanUpAndRemoveSymbol(old);
        g_array = Script::NewArray();
        if (!g_array)
            return false;
        g_array->Init();
        Script::CSymbolTableEntry* symbol = Script::CreateSymbol(name);
        symbol->type = Script::ESYMBOLTYPE_ARRAY;
        symbol->raw = reinterpret_cast<uint32_t>(g_array);
    }
    if (manager->m_count >= static_cast<uint32_t>(SkateScript::CNodeManager::kMaxArrays)) {
        Log("level editor: the node manager's 20 node arrays are in use - added nodes not registered");
        return false;
    }
    g_levelArray = manager->GetLastNodeArrayName(); // (the overlay passed over: the level's)
    g_array->SetSizeAndType(static_cast<uint32_t>(g_structs.size()), Script::ESYMBOLTYPE_STRUCTURE);
    for (size_t i = 0; i < g_structs.size(); ++i) {
        g_array->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(g_structs[i]));
        // (links= are names already: kept out of AddNodeArray's ResolveLinks, which reads indices - put on after)
        g_structs[i]->RemoveComponent(kLinks);
    }
    g_registered = manager->AddNodeArray(Nodes::OverlayArray(), false);
    for (size_t i = 0; i < g_structs.size(); ++i)
        WriteLinks(g_structs[i], g_list[i].links);
    Nodes::Invalidate();
    return g_registered;
}

// What the game would have made for the node at the level's load (CFunc_ParseNodeArray), made now.
void MakeLive(const Spec& spec, Script::CStruct* node)
{
    Kind kind;
    if (!KindOf(spec, &kind))
        return;
    uint32_t name = Checksum(spec.name);
    Mth::Vector pos = {spec.pos.x, spec.pos.y, spec.pos.z, 1.0f};
    if (kind == Kind::Proxim && HaveProxim()) {
        if (Obj::ProximNode* proxim = Obj::Proxim_GetNode(name)) {
            // (made before and removed: the game can't free one - used again)
            proxim->m_pos = pos;
            proxim->m_radius = spec.radius;
            proxim->m_radiusSquared = spec.radius * spec.radius;
            Obj::Proxim_SetActive(name, 1);
        } else {
            Obj::Proxim_AddNode(name, true);
        }
    } else if (kind == Kind::Sound && HaveEmitters()) {
        if (Obj::EmitterNode* emitter = Obj::Emitter_GetNode(name)) {
            emitter->m_pos = pos;
            emitter->m_radius = spec.radius;
            emitter->m_radiusSquared = spec.radius * spec.radius;
            emitter->m_active = true;
        } else {
            Obj::Emitter_AddNode(name, true);
        }
    } else if (kind == Kind::Light) {
        AddLight(node);
    }
}

void KillLive(const Spec& spec)
{
    Kind kind;
    if (!KindOf(spec, &kind))
        return;
    uint32_t name = Checksum(spec.name);
    if (kind == Kind::Proxim && HaveProxim())
        Obj::Proxim_SetActive(name, 0);
    else if (kind == Kind::Sound && HaveEmitters())
        Obj::Emitter_SetActive(name, 0);
    else if (kind == Kind::Light)
        RemoveLight(name);
}

// A spec's place as its structure has it now.
Spec WithPlace(const Spec& spec, Script::CStruct* node)
{
    Spec s = spec;
    Mth::Vector pos, angles;
    SkateScript::GetPosition(node, &pos);
    SkateScript::GetAngles(node, &angles);
    s.pos = {pos.x, pos.y, pos.z};
    s.angles = {angles.x, angles.y, angles.z};
    return s;
}

void Forget()
{
    SkateScript::CNodeManager* manager = Manager();
    if (g_registered && manager) {
        g_registered = false;
        manager->RemoveNodeArray(Nodes::OverlayArray());
    }
    g_registered = false;
    for (const Spec& spec : g_list)
        NodeEdit::Forget(Checksum(spec.name));
    for (Script::CStruct* node : g_structs)
        Free(node);
    g_structs.clear();
    g_list.clear();
    if (g_array)
        g_array->SetSizeAndType(0, Script::ESYMBOLTYPE_STRUCTURE);
    g_hidden.clear();
    Nodes::Invalidate();
}

bool InLevel()
{
    SkateScript::CNodeManager* manager = Manager();
    return manager && manager->m_count > (g_registered ? 1u : 0u) && Nx::CurrentLevel() != 0;
}

} // namespace

void SetArrayRemovedListener(void (*listener)(uint32_t name))
{
    g_arrayRemovedListener = listener;
    SkateScript::SetNodeArrayRemovedHook(&OnArrayRemoved);
}

List Current()
{
    List list;
    for (size_t i = 0; i < g_list.size(); ++i)
        list.push_back(WithPlace(g_list[i], g_structs[i]));
    return list;
}

bool IsAdded(uint32_t name)
{
    for (const Spec& spec : g_list) {
        if (Checksum(spec.name) == name)
            return true;
    }
    return false;
}

bool NameTaken(const std::string& name)
{
    uint32_t checksum = Checksum(name);
    if (IndexOf(g_list, name) >= 0)
        return true;
    if (Manager() && SkateScript::GetNode(checksum, false))
        return true;
    return Obj::FindObject(checksum) != nullptr;
}

Counts CountsNow()
{
    Counts counts;
    static const uint32_t kRestart = Checksum("Restart"), kCrown = Checksum("Crown");
    for (const Nodes::Node& node : Nodes::All()) {
        if (node.hidden)
            continue;
        if (node.type == kCrown)
            ++counts.crowns; // (SpawnCrown counts the type, whatever the class)
        if (node.cls == kRestart)
            ++counts.restarts;
    }
    counts.dynamicLights = Nx::g_dynamicLightCount;
    counts.levelLights = Nx::g_sceneLightCount;
    return counts;
}

const char* WhyNot(Kind kind, Team team)
{
    if (!InLevel())
        return "Not in a level";
    if (const char* limit = LimitMessage(kind, CountsNow()))
        return limit;
    std::string fixed = FixedName(kind, team);
    if (!fixed.empty() && (NameTaken(fixed) || (kind == Kind::FlagSet && NameTaken(fixed + "_Base"))))
        return kind == Kind::FlagSet ? "This team flag already exists"
                                     : "This team spawn already exists";
    return nullptr;
}

const char* WhyNotLobbyFlag(Team team)
{
    if (!InLevel())
        return "Not in a level";
    if (NameTaken(LobbyFlagName(team)))
        return "This team flag already exists";
    return nullptr;
}

bool Create(const Spec& spec, int index, std::string* message)
{
    List one = {spec};
    if (!InLevel()) {
        *message = "Not in a level";
        return false;
    }
    if (!ValidName(spec.name) || NameTaken(spec.name)) {
        *message = "A node named " + spec.name + " is there already";
        return false;
    }
    Script::CStruct* node = Build(spec);
    if (!node) {
        *message = "Couldn't make the node";
        return false;
    }
    if (index < 0 || index > static_cast<int>(g_list.size()))
        index = static_cast<int>(g_list.size());
    g_list.insert(g_list.begin() + index, spec);
    g_structs.insert(g_structs.begin() + index, node);
    if (!Sync()) {
        g_list.erase(g_list.begin() + index);
        g_structs.erase(g_structs.begin() + index);
        Free(node);
        Sync();
        *message = "Couldn't add it to the node manager";
        return false;
    }
    MakeLive(spec, node);
    Log("level editor: added %s (%s %s) at (%.1f, %.1f, %.1f)", spec.name.c_str(), spec.cls.c_str(), spec.type.c_str(),
        spec.pos.x, spec.pos.y, spec.pos.z);
    return true;
}

int CreateAll(const List& specs, std::string* message)
{
    if (!InLevel()) {
        *message = "Not in a level";
        return 0;
    }
    std::vector<std::pair<Spec, Script::CStruct*>> made;
    for (const Spec& spec : specs) {
        bool duplicate = false;
        for (const auto& m : made)
            duplicate = duplicate || SameText(m.first.name, spec.name.c_str());
        if (!ValidName(spec.name) || duplicate || NameTaken(spec.name)) {
            Log("level editor: the layout's node %s is there already - skipped", spec.name.c_str());
            continue;
        }
        if (Script::CStruct* node = Build(spec)) {
            g_list.push_back(spec);
            g_structs.push_back(node);
            made.push_back({spec, node});
        }
    }
    if (made.empty())
        return 0;
    if (!Sync()) {
        for (size_t i = 0; i < made.size(); ++i) {
            g_list.pop_back();
            Free(g_structs.back());
            g_structs.pop_back();
        }
        Sync();
        *message = "Couldn't add the layout's nodes to the node manager";
        return 0;
    }
    for (const auto& m : made)
        MakeLive(m.first, m.second);
    return static_cast<int>(made.size());
}

bool Remove(uint32_t name, Spec* spec, int* index)
{
    for (size_t i = 0; i < g_list.size(); ++i) {
        if (Checksum(g_list[i].name) != name)
            continue;
        *spec = WithPlace(g_list[i], g_structs[i]);
        *index = static_cast<int>(i);
        Script::CStruct* node = g_structs[i];
        KillLive(*spec); // (the light lists by name - before the node goes)
        g_list.erase(g_list.begin() + static_cast<std::ptrdiff_t>(i));
        g_structs.erase(g_structs.begin() + static_cast<std::ptrdiff_t>(i));
        Sync();
        NodeEdit::Forget(name);
        Free(node);
        Log("level editor: removed the added node %s", spec->name.c_str());
        return true;
    }
    return false;
}

bool SetLinks(uint32_t name, const std::vector<uint32_t>& links)
{
    for (size_t i = 0; i < g_list.size(); ++i) {
        if (Checksum(g_list[i].name) != name)
            continue;
        g_list[i].links = links;
        WriteLinks(g_structs[i], links);
        Nodes::Invalidate();
        return true;
    }
    return false;
}

std::vector<std::string> Order()
{
    std::vector<std::string> names;
    for (const Spec& spec : g_list)
        names.push_back(spec.name);
    return names;
}

void SetOrder(const std::vector<std::string>& names)
{
    List list;
    std::vector<Script::CStruct*> structs;
    std::vector<bool> used(g_list.size(), false);
    for (const std::string& name : names) {
        int i = IndexOf(g_list, name);
        if (i >= 0 && !used[i]) {
            used[i] = true;
            list.push_back(g_list[i]);
            structs.push_back(g_structs[i]);
        }
    }
    for (size_t i = 0; i < g_list.size(); ++i) {
        if (!used[i]) {
            list.push_back(g_list[i]);
            structs.push_back(g_structs[i]);
        }
    }
    g_list.swap(list);
    g_structs.swap(structs);
    Sync();
}

bool MoveCrown(uint32_t name, int delta, std::string* message)
{
    for (const Spec& spec : g_list) {
        if (Checksum(spec.name) != name)
            continue;
        List list = g_list;
        if (!NodeCreate::MoveCrown(&list, spec.name, delta)) {
            *message = delta < 0 ? "Already the first added crown"
                                 : "Already the last crown";
            return false;
        }
        std::vector<std::string> order;
        for (const Spec& s : list)
            order.push_back(s.name);
        SetOrder(order);
        return true;
    }
    *message = "Only added crowns can be reordered";
    return false;
}

void ApplyCrownOrder(const std::vector<std::string>& order)
{
    List list = g_list;
    NodeCreate::ApplyCrownOrder(&list, order);
    std::vector<std::string> names;
    for (const Spec& s : list)
        names.push_back(s.name);
    SetOrder(names);
}

bool CanHide(const Nodes::Node& node, const char** why)
{
    *why = nullptr;
    if (node.added) {
        *why = "Added nodes are deleted, not hidden";
        return false;
    }
    if (!node.name) {
        *why = "A node without a name can't be hidden";
        return false;
    }
    switch (node.family) {
    case Nodes::Family::Spawn:
    case Nodes::Family::Crown:
    case Nodes::Family::Flag:
    case Nodes::Family::Light:
    case Nodes::Family::Sound:
    case Nodes::Family::Trigger:
        break;
    default:
        *why = "This node type can't be hidden";
        return false;
    }
    if (node.hidden)
        return true;
    if (node.family == Nodes::Family::Crown && Nodes::Walked(Nodes::Family::Crown) <= 1) {
        *why = "Can't hide the last crown";
        return false;
    }
    if (node.family == Nodes::Family::Spawn) {
        uint32_t others = 0;
        for (const Nodes::Node& n : Nodes::All()) {
            if (n.family == Nodes::Family::Spawn && !n.hidden && n.name != node.name)
                others |= n.restartTypes;
        }
        if (HideLeavesNoRestart(node.restartTypes, others)) {
            *why = "Can't hide the last start point";
            return false;
        }
    }
    return true;
}

bool Hidden(uint32_t name)
{
    return std::any_of(g_hidden.begin(), g_hidden.end(), [&](const HiddenNode& h) { return h.name == name; });
}

std::vector<uint32_t> HiddenNames()
{
    std::vector<uint32_t> names;
    for (const HiddenNode& h : g_hidden)
        names.push_back(h.name);
    return names;
}

bool Hide(uint32_t name, bool hide)
{
    if (!Manager() || IsAdded(name))
        return false;
    Script::CStruct* node = SkateScript::GetNode(name, false);
    if (!node)
        return false;
    auto it = std::find_if(g_hidden.begin(), g_hidden.end(), [&](const HiddenNode& h) { return h.name == name; });
    static const uint32_t kLight = Checksum("LevelLight"), kProxim = Checksum("ProximNode"),
                          kEmitter = Checksum("EmitterObject");
    if (hide) {
        if (it != g_hidden.end())
            return true;
        HiddenNode h = {name, node, false, false};
        uint32_t cls = 0, type = 0;
        node->GetChecksum(kClass, &cls, 0);
        node->GetChecksum(kType, &type, 0);
        if (cls == Nodes::HiddenMarker())
            return false;
        node->AddChecksum(Nodes::HiddenClassKey(), cls);
        node->AddChecksum(Nodes::HiddenTypeKey(), type);
        node->AddChecksum(kClass, Nodes::HiddenMarker());
        node->AddChecksum(kType, Nodes::HiddenMarker());
        if (cls == kProxim && HaveProxim()) {
            if (Obj::ProximNode* proxim = Obj::Proxim_GetNode(name)) {
                h.proximActive = proxim->m_active;
                Obj::Proxim_SetActive(name, 0);
            }
        }
        if (cls == kEmitter && HaveEmitters()) {
            if (Obj::EmitterNode* emitter = Obj::Emitter_GetNode(name)) {
                h.emitterActive = emitter->m_active;
                Obj::Emitter_SetActive(name, 0);
            }
        }
        if (cls == kLight)
            RemoveLight(name);
        // (a flag a CTF game made: killed - it comes back when the game makes it again)
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(name));
        if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) && cls != kProxim && cls != kEmitter &&
            cls != kLight)
            object->MarkAsDead();
        g_hidden.push_back(h);
        Log("level editor: hid the level's node %s", Objects::Label(name).c_str());
    } else {
        if (it == g_hidden.end())
            return true;
        HiddenNode h = *it;
        g_hidden.erase(it);
        uint32_t cls = 0, type = 0;
        node->GetChecksum(Nodes::HiddenClassKey(), &cls, 0);
        node->GetChecksum(Nodes::HiddenTypeKey(), &type, 0);
        node->RemoveComponent(Nodes::HiddenClassKey());
        node->RemoveComponent(Nodes::HiddenTypeKey());
        node->RemoveComponent(kClass);
        node->RemoveComponent(kType);
        node->AddChecksum(kClass, cls);
        if (type)
            node->AddChecksum(kType, type);
        if (cls == kProxim && HaveProxim() && h.proximActive)
            Obj::Proxim_SetActive(name, 1);
        if (cls == kEmitter && HaveEmitters() && h.emitterActive)
            Obj::Emitter_SetActive(name, 1);
        if (cls == kLight)
            AddLight(node);
        Log("level editor: showed the level's node %s again", Objects::Label(name).c_str());
    }
    Nodes::Invalidate();
    return true;
}

void ResetAll()
{
    std::vector<uint32_t> hidden = HiddenNames();
    for (uint32_t name : hidden)
        Hide(name, false);
    std::vector<std::string> names = Order();
    for (const std::string& name : names) {
        Spec spec;
        int index;
        Remove(Checksum(name), &spec, &index);
    }
}

void ForgetLevel()
{
    // (a level whose node array stays loaded: hidden nodes shown, added ones removed; another level's: forgotten)
    if (Manager()) {
        for (const HiddenNode& h : std::vector<HiddenNode>(g_hidden)) {
            if (SkateScript::GetNode(h.name, false) == h.node)
                Hide(h.name, false);
        }
    }
    Forget();
}

} // namespace Front::LevelEditor::NodeCreate
