// NOT ORIGINAL - the level editor's node edits (ours). See level_editor_node_edit.h.

#include "game/front/level_editor_node_edit.h"

#include <cmath>
#include <unordered_map>

#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/mth/matrix.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "game/sys/log.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/scene_lights.h"

namespace Front::LevelEditor::NodeEdit {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kStep = 15.0f * kPi / 180.0f; // (Z / X: as objects turn)

} // namespace

bool Same(const Values& a, const Values& b)
{
    return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.angles.x == b.angles.x &&
           a.angles.y == b.angles.y && a.angles.z == b.angles.z;
}

// --- the pure parts ---------------------------------------------------------------------------------------------

float WrapAngle(float radians)
{
    if (!std::isfinite(radians))
        return 0.0f;
    // (the half turn as a float - a little over pi - and a hair's room, so a half turn either way comes out +pi)
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kHalf = static_cast<double>(kPi) + 1e-6;
    double a = std::fmod(static_cast<double>(radians), kTwoPi);
    if (a > kHalf)
        a -= kTwoPi;
    else if (a <= -kHalf + 2e-6)
        a += kTwoPi;
    return static_cast<float>(a);
}

V3 Turned(V3 angles, int notches)
{
    angles.y = WrapAngle(angles.y + static_cast<float>(notches) * kStep);
    return angles;
}

float HeadingDegrees(V3 angles)
{
    float degrees = WrapAngle(angles.y) * (180.0f / kPi);
    return degrees <= -180.0f ? 180.0f : degrees;
}

V3 WithHeading(V3 angles, float degrees)
{
    angles.y = WrapAngle(degrees * (kPi / 180.0f));
    return angles;
}

Step Capture(const std::vector<uint32_t>& names, const Reader& read)
{
    Step step;
    for (uint32_t name : names) {
        Values values;
        if (read(name, &values))
            step.nodes.push_back({name, values});
    }
    return step;
}

void Put(const Step& step, const Writer& write)
{
    for (const auto& node : step.nodes)
        write(node.first, node.second);
}

bool Differs(const Step& step, const Reader& read)
{
    for (const auto& node : step.nodes) {
        Values now;
        if (read(node.first, &now) && !Same(now, node.second))
            return true;
    }
    return false;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kPos = 0x7F261953;    // pos
constexpr uint32_t kAngles = 0x9D2D0915; // angles
constexpr uint32_t kShape = 0x22CF0027;  // Shape

// What the editor changed in this level: each node's place as the level had it, the node (its structure, to know the
// level's array is still the one loaded), in the order first changed.
struct Touched {
    uint32_t name;
    Values origin;
    Script::CStruct* node;
};
std::vector<Touched> g_touched;
std::unordered_map<uint32_t, size_t> g_touchedIndex;
// Rail points' places as the level has them (Origin's answer: the rail manager keeps them - Scenery::RailOrigin).
std::unordered_map<uint32_t, Values> g_railOrigins;

// The node's own pos= / angles= component (the last, as CStruct::Search reads them - never one in an included
// structure, which the game doesn't read for these either).
Script::CComponent* OwnVector(Script::CStruct* node, uint32_t name)
{
    Script::CComponent* found = nullptr;
    for (Script::CComponent* c = node->m_head; c; c = c->next) {
        if (c->name == name && c->Type() == Script::ESYMBOLTYPE_VECTOR && c->vector)
            found = c;
    }
    return found;
}

void WriteVector(Script::CStruct* node, uint32_t name, V3 value)
{
    if (Script::CComponent* c = OwnVector(node, name)) {
        c->vector->x = value.x;
        c->vector->y = value.y;
        c->vector->z = value.z;
        return;
    }
    node->AddVector(name, value.x, value.y, value.z);
}

// The light lists 0x00490570 fills (by the node's name; read in 0x0048FCA0, which takes a light away): the dynamic
// lights - up to 256 pointers at 0x006FAAF0, the count at 0x006FAEF0, each with its place at +0x04 and its name at
// +0x28 - and the level lights - up to 150 entries of 0x24 bytes at 0x006FAF80, the count at 0x006FAEF4, the place at
// +0x00 and the name at +0x18.
void MoveLights(uint32_t name, V3 pos)
{
    auto** dynamic = reinterpret_cast<uint8_t**>(Nx::g_dynamicLights);
    int dynamicCount = Nx::g_dynamicLightCount;
    for (int i = 0; i < dynamicCount && i < 256; ++i) {
        uint8_t* light = dynamic[i];
        if (light && *reinterpret_cast<uint32_t*>(light + 0x28) == name) {
            auto* place = reinterpret_cast<Mth::Vector*>(light + 0x04);
            place->x = pos.x;
            place->y = pos.y;
            place->z = pos.z;
        }
    }
    auto* level = reinterpret_cast<uint8_t*>(Nx::g_sceneLights);
    int levelCount = Nx::g_sceneLightCount;
    for (int i = 0; i < levelCount && i < 150; ++i) {
        uint8_t* entry = level + i * 0x24;
        if (*reinterpret_cast<uint32_t*>(entry + 0x18) == name) {
            auto* place = reinterpret_cast<Mth::Vector*>(entry);
            place->x = pos.x;
            place->y = pos.y;
            place->z = pos.z;
        }
    }
}

// What the game keeps of the node, brought up to date (level_editor_node_edit.h's list).
void ApplyLive(const Nodes::Node* node, uint32_t name, const Values& values)
{
    Mth::Vector pos = {values.pos.x, values.pos.y, values.pos.z, 1.0f};
    Nodes::Family family = node ? node->family : Nodes::Family::Other;
    if (family == Nodes::Family::Trigger && reinterpret_cast<Obj::ProximManager*>(Obj::g_proximManager)) {
        if (Obj::ProximNode* proxim = Obj::Proxim_GetNode(name))
            proxim->m_pos = pos;
    }
    if (family == Nodes::Family::Sound && Obj::g_emitterTable) {
        if (Obj::EmitterNode* emitter = Obj::Emitter_GetNode(name))
            emitter->m_pos = pos;
    }
    if (family == Nodes::Family::Light)
        MoveLights(name, values.pos);
    // Its object (a flag a CTF or team game made; any object of the node's name): moved and turned as the node.
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(name));
    if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD)) {
        Objects::State state = Objects::Save(object);
        state.pos = pos;
        Mth::Matrix matrix;
        Mth::Vector angles = {values.angles.x, values.angles.y, values.angles.z, 1.0f};
        matrix.InitFromAngles(angles);
        for (int r = 0; r < 3; ++r)
            state.matrix.row[r] = state.displayMatrix.row[r] = matrix.row[r];
        Objects::Restore(state);
    }
}

// The level's node named `name` (the node manager's: the one the game finds), null if none or no level.
Script::CStruct* StructNamed(uint32_t name)
{
    return name && SkateScript::NodeManagerInstance() ? SkateScript::GetNode(name, false) : nullptr;
}

const Nodes::Node* NodeNamed(uint32_t name)
{
    int index = Nodes::Find(name);
    return index >= 0 ? &Nodes::All()[index] : nullptr;
}

} // namespace

bool Movable(const Nodes::Node& node)
{
    if (!node.hasPos || !node.name)
        return false;
    switch (node.family) {
    case Nodes::Family::Rail:
    case Nodes::Family::Ladder:
        return IsRail(node.name); // (a point of the rail manager: where skaters grind / climb)
    case Nodes::Family::Path:
        return true; // (a waypoint: its pos=, read when a ped or car heads for it - level_editor_chains.h)
    case Nodes::Family::Spawn:
    case Nodes::Family::Crown:
    case Nodes::Family::Flag:
    case Nodes::Family::Light:
    case Nodes::Family::Sound:
    case Nodes::Family::Trigger:
    case Nodes::Family::Other:
        return true;
    default:
        // (particle systems, objects the level hasn't made - level_editor_entities.h)
        return Entities::MovableNode(node.name, static_cast<int>(node.family));
    }
}

const char* WhenApplied(const Nodes::Node& node)
{
    Script::CStruct* s = Nodes::StructOf(node);
    uint32_t shape = 0;
    bool sectorShape = s && s->GetChecksum(kShape, &shape, 0) && shape != 0xAA069978; // (a sphere's)
    switch (node.family) {
    case Nodes::Family::Rail:
        return "Live";
    case Nodes::Family::Ladder:
        return "Live";
    case Nodes::Family::Path:
        return "When next reached";
    case Nodes::Family::Spawn:
        return "Next respawn";
    case Nodes::Family::Crown:
        return "Next crown spawn";
    case Nodes::Family::Flag:
        return "Live";
    case Nodes::Family::Light:
        return "Live; baked lighting on reload";
    case Nodes::Family::Sound:
        return sectorShape ? "Live; area unchanged" : "Live";
    case Nodes::Family::Trigger:
        return sectorShape ? "Live; area unchanged" : "Live";
    case Nodes::Family::Particle:
    case Nodes::Family::Object:
        return Entities::WhenApplied(node.name, static_cast<int>(node.family));
    default:
        return "When next read";
    }
}

bool IsRail(uint32_t name)
{
    V3 pos;
    return name && Scenery::RailPoint(name, &pos, true); // (a ladder's point too)
}

bool IsChain(uint32_t name) { return IsRail(name) || Chains::IsPath(name); }

std::vector<uint32_t> MovedWith(uint32_t name, bool wholeRail)
{
    if (wholeRail && IsRail(name)) {
        std::vector<uint32_t> rail = Scenery::RailOf(name);
        if (!rail.empty())
            return rail;
    }
    if (wholeRail && Chains::IsPath(name))
        return Chains::ChainOf(name); // (a path: its waypoints linked either way)
    return {name};
}

bool Read(uint32_t name, Values* values)
{
    // (a rail point: where the rail manager has it - its node keeps the level's pos=; no angles of its own)
    V3 point;
    if (name && Scenery::RailPoint(name, &point, true)) {
        values->pos = point;
        values->angles = {0.0f, 0.0f, 0.0f};
        return true;
    }
    Script::CStruct* node = StructNamed(name);
    if (!node)
        return false;
    Mth::Vector pos, angles;
    SkateScript::GetPosition(node, &pos);
    SkateScript::GetAngles(node, &angles);
    values->pos = {pos.x, pos.y, pos.z};
    values->angles = {angles.x, angles.y, angles.z};
    return true;
}

void NoteOrigin(uint32_t name)
{
    if (IsRail(name) && !NodeCreate::IsAdded(name))
        return; // (the rail manager notes where the level has it)
    if (g_touchedIndex.count(name))
        return;
    Values origin;
    if (!Read(name, &origin))
        return;
    g_touchedIndex[name] = g_touched.size();
    g_touched.push_back({name, origin, StructNamed(name)});
}

const Values* Origin(uint32_t name)
{
    V3 origin;
    if (IsRail(name) && !NodeCreate::IsAdded(name) && Scenery::RailOrigin(name, &origin)) {
        Values& values = g_railOrigins[name];
        values = {origin, {0.0f, 0.0f, 0.0f}};
        return &values;
    }
    auto it = g_touchedIndex.find(name);
    return it == g_touchedIndex.end() ? nullptr : &g_touched[it->second].origin;
}

bool Set(uint32_t name, const Values& values)
{
    // A rail point: moved in the rail manager (Scenery's rail records - saved as `rail`, reset with the level's
    // scenery), its segments' boxes made again; its node and its angles stay. A point the editor added: its node and its
    // rail manager node both (its `point` record has its place - level_editor_chains.h).
    if (Chains::IsAddedPoint(name)) {
        NoteOrigin(name);
        return Chains::MoveAddedPoint(name, values.pos);
    }
    if (IsRail(name)) {
        if (!Scenery::SetRailPoint(name, values.pos))
            return false;
        int index = Nodes::Find(name);
        if (index >= 0)
            Nodes::Reread(index);
        return true;
    }
    Script::CStruct* node = StructNamed(name);
    if (!node)
        return false;
    NoteOrigin(name);
    WriteVector(node, kPos, values.pos);
    WriteVector(node, kAngles, values.angles);
    const Nodes::Node* layer = NodeNamed(name);
    ApplyLive(layer, name, values);
    Entities::NodeMoved(name, values.pos, values.angles); // (a particle system: its emitter turned and moved, remade)
    int index = Nodes::Find(name);
    if (index >= 0)
        Nodes::Reread(index);
    return true;
}

std::vector<Record> Changed()
{
    std::vector<Record> out;
    for (const Touched& t : g_touched) {
        Values now;
        if (Read(t.name, &now) && !Same(now, t.origin))
            out.push_back({t.name, now});
    }
    return out;
}

void Forget(uint32_t name)
{
    auto it = g_touchedIndex.find(name);
    if (it == g_touchedIndex.end())
        return;
    g_touched.erase(g_touched.begin() + static_cast<std::ptrdiff_t>(it->second));
    g_touchedIndex.clear();
    for (size_t i = 0; i < g_touched.size(); ++i)
        g_touchedIndex[g_touched[i].name] = i;
}

void ResetAll()
{
    std::vector<Touched> touched = g_touched;
    for (const Touched& t : touched)
        Set(t.name, t.origin);
}

void ForgetLevel()
{
    // (a level whose node array stays loaded - the same structures under the same names - gets its places back: the
    // layout puts them in again; another level's nodes of the same name aren't touched)
    for (const Touched& t : g_touched) {
        Script::CStruct* node = StructNamed(t.name);
        if (node && node == t.node) {
            WriteVector(node, kPos, t.origin.pos);
            WriteVector(node, kAngles, t.origin.angles);
        }
    }
    g_touched.clear();
    g_touchedIndex.clear();
    g_railOrigins.clear();
}

} // namespace Front::LevelEditor::NodeEdit
