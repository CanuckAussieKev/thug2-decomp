// NOT ORIGINAL - the level editor's node edits (ours). See level_editor_node_edit.h.

#include "editor/level_editor_node_edit.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "engine/maths/mat4.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/scene_lights.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/light_sets.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec4.h"

namespace frontend::level_editor::node_edit {

namespace {

constexpr float kDegreesPerHalfTurn = 180.0f; // degrees in half a turn

} // namespace

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kStep = 15.0f * kPi / 180.0f; // (Z / X: as objects turn)

} // namespace

bool get_same(const Values& a, const Values& b)
{
    return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.angles.x == b.angles.x &&
           a.angles.y == b.angles.y && a.angles.z == b.angles.z;
}

// --- the pure parts ---------------------------------------------------------------------------------------------

float wrap_angle(float radians)
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

V3 get_turned(V3 angles, int notches)
{
    angles.y = wrap_angle(angles.y + static_cast<float>(notches) * kStep);
    return angles;
}

float get_heading_degrees(V3 angles)
{
    const float degrees = wrap_angle(angles.y) * (180.0f / kPi);
    return degrees <= -180.0f ? 180.0f : degrees;
}

V3 with_heading(V3 angles, float degrees)
{
    angles.y = wrap_angle(degrees * (kPi / kDegreesPerHalfTurn));
    return angles;
}

Step get_capture(const std::vector<uint32_t>& names, const Reader& read)
{
    Step step;
    for (const uint32_t name : names) {
        Values values;
        if (read(name, &values))
            step.nodes.emplace_back(name, values);
    }
    return step;
}

void place_in(const Step& step, const Writer& write)
{
    for (const auto& node : step.nodes)
        write(node.first, node.second);
}

bool differs(const Step& step, const Reader& read)
{
    for (const auto& node : step.nodes) {
        Values now;
        if (read(node.first, &now) && !get_same(now, node.second))
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
    script::Struct* node;
};
std::vector<Touched>& stored_touched()
{
    static core::NoDestroy<std::vector<Touched>> touched;
    return touched.get();
}
std::unordered_map<uint32_t, size_t>& touched_index()
{
    static std::unordered_map<uint32_t, size_t> value;
    return value;
}
// Rail points' places as the level has them (Origin's answer: the rail manager keeps them - Scenery::rail_origin).
std::unordered_map<uint32_t, Values>& rail_origins()
{
    static std::unordered_map<uint32_t, Values> value;
    return value;
}

// The node's own pos= / angles= component (the last, as CStruct::Hunt reads them - never one in an included
// structure, which the game doesn't read for these either).
script::Component* own_vector(script::Struct* node, uint32_t name)
{
    script::Component* found = nullptr;
    for (script::Component* c = node->head(); c; c = c->next) {
        if (c->name == name && value_kind(*c) == script::kQbvalVector && c->value.vector())
            found = c;
    }
    return found;
}

void store_node_vector(script::Struct* node, uint32_t name, V3 value)
{
    if (const script::Component* c = own_vector(node, name)) {
        c->value.vector()->x() = value.x;
        c->value.vector()->y() = value.y;
        c->value.vector()->z() = value.z;
        return;
    }
    node->put_vec3(checksums::Name{name}, {.x = value.x, .y = value.y, .z = value.z});
}

// The light lists 0x00490570 fills (by the node's name; read in 0x0048FCA0, which takes a light away): the dynamic
// lights (up to 256, by their owner) and the level lights (up to 150, by their label).
void move_lights(uint32_t name, V3 pos)
{
    const int dynamic_count = render::light_sets().dynamic_light_count();
    for (int i = 0; i < dynamic_count && i < 256; ++i) {
        render::DynamicLightBase* light = core::at(render::light_sets().dynamic_lights(), i);
        if (light && light->owner() == name) {
            core::at(light->pos(), 0) = pos.x;
            core::at(light->pos(), 1) = pos.y;
            core::at(light->pos(), 2) = pos.z;
        }
    }
    const int level_count = render::light_sets().scene_light_count();
    for (int i = 0; i < level_count && i < 150; ++i) {
        render::LevelLamp& lamp = core::at(render::light_sets().level_lamps(), i);
        if (lamp.label() == name) {
            lamp.at_point().x = pos.x;
            lamp.at_point().y = pos.y;
            lamp.at_point().z = pos.z;
        }
    }
}

// What the game keeps of the node, brought up to date (level_editor_node_edit.h's list).
void apply_live(const nodes::Elem* node, uint32_t name, const Values& values)
{
    const maths::Vec4 pos = {.x = values.pos.x, .y = values.pos.y, .z = values.pos.z, .w = 1.0f};
    const nodes::Family family = node ? node->family : nodes::Family::kOther;
    if (family == nodes::Family::kFireOff && objects::proximity_hub() != nullptr) {
        if (objects::ProximNode* proxim = objects::proxim_get_node(name))
            proxim->at_point() = pos;
    }
    if (family == nodes::Family::kSound && objects::emitter_table()) {
        if (objects::EmitterNode* emitter = objects::emitter_get_node(name))
            emitter->at_point_ = pos;
    }
    if (family == nodes::Family::kLight)
        move_lights(name, values.pos);
    // Its object (a flag a CTF or team game made; any object of the node's name): moved and turned as the node.
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
    if (object && !(object->object_flags() & objects::kObjectFlagDead)) {
        object_edit::State state = object_edit::store_away(object);
        state.pos = pos;
        maths::Mat4 matrix;
        const maths::Vec4 angles = {.x = values.angles.x, .y = values.angles.y, .z = values.angles.z, .w = 1.0f};
        matrix.init_from_angles(angles);
        for (int r = 0; r < 3; ++r)
            core::at(state.matrix.row, r) = core::at(state.draw_matrix_.row, r) = core::at(matrix.row, r);
        object_edit::get_restore(state);
    }
}

// The level's node named `name` (the node manager's: the one the game finds), null if none or no level.
script::Struct* struct_named(uint32_t name)
{
    return name && level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
}

const nodes::Elem* node_named(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 ? &core::at(nodes::get_all(), index) : nullptr;
}

} // namespace

bool get_movable(const nodes::Elem& node)
{
    if (!node.has_pos_ || !node.name)
        return false;
    switch (node.family) {
    case nodes::Family::kRail:
    case nodes::Family::kLadder:
        return is_rail(node.name); // (a point of the rail manager: where skaters grind / climb)
    // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
    case nodes::Family::kRouteWay:
        return true; // (a waypoint: its pos=, read when a ped or car heads for it - level_editor_chains.h)
    case nodes::Family::kSpawn:
    case nodes::Family::kCrown:
    case nodes::Family::kBitMark:
    case nodes::Family::kLight:
    case nodes::Family::kSound:
    case nodes::Family::kFireOff:
    case nodes::Family::kOther:
        return true;
    default:
        // (particle systems, objects the level hasn't made - level_editor_entities.h)
        return entities::picked_node_index(checksums::Name{node.name}, static_cast<int>(node.family));
    }
}

const char* when_applied(const nodes::Elem& node)
{
    script::Struct* s = nodes::struct_of(node);
    uint32_t shape = 0;
    const bool sector_shape = s && s->read_hash_value(kShape, &shape, 0) && shape != 0xAA069978; // (a sphere's)
    switch (node.family) {
    // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
    case nodes::Family::kRail:
        return "Live";
    case nodes::Family::kLadder:
        return "Live";
    case nodes::Family::kRouteWay:
        return "When next reached";
    case nodes::Family::kSpawn:
        return "Next respawn";
    case nodes::Family::kCrown:
        return "Next crown spawn";
    case nodes::Family::kBitMark:
        return "Live";
    case nodes::Family::kLight:
        return "Live; baked lighting on reload";
    // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
    case nodes::Family::kSound:
        return sector_shape ? "Live; area unchanged" : "Live";
    case nodes::Family::kFireOff:
        return sector_shape ? "Live; area unchanged" : "Live";
    case nodes::Family::kParticle:
    case nodes::Family::kObject:
        return entities::when_applied(checksums::Name{node.name}, static_cast<int>(node.family));
    default:
        return "When next read";
    }
}

bool is_rail(uint32_t name)
{
    V3 pos{};
    return name && scenery::get_rail_point(name, &pos, true); // (a ladder's point too)
}

bool is_chain(uint32_t name) { return is_rail(name) || chains::is_path(name); }

std::vector<uint32_t> moved_with(uint32_t name, bool entire_rail)
{
    if (entire_rail && is_rail(name)) {
        std::vector<uint32_t> rail = scenery::rail_of(name);
        if (!rail.empty())
            return rail;
    }
    if (entire_rail && chains::is_path(name))
        return chains::chain_of(name); // (a path: its waypoints linked either way)
    return {name};
}

bool get_take_in(uint32_t name, Values* values)
{
    // (a rail point: where the rail manager has it - its node keeps the level's pos=; no angles of its own)
    V3 point{};
    if (name && scenery::get_rail_point(name, &point, true)) {
        values->pos = point;
        values->angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f};
        return true;
    }
    script::Struct* node = struct_named(name);
    if (!node)
        return false;
    maths::Vec4 pos, angles;
    level_nodes::fetch_place(node, &pos);
    level_nodes::read_turn_angles(node, &angles);
    values->pos = {.x = pos.x, .y = pos.y, .z = pos.z};
    values->angles = {.x = angles.x, .y = angles.y, .z = angles.z};
    return true;
}

void note_origin(uint32_t name)
{
    if (is_rail(name) && !node_create::is_added(name))
        return; // (the rail manager notes where the level has it)
    if (touched_index().count(name))
        return;
    Values origin;
    if (!get_take_in(name, &origin))
        return;
    touched_index()[name] = stored_touched().size();
    stored_touched().push_back({.name = name, .origin = origin, .node = struct_named(name)});
}

const Values* get_origin(uint32_t name)
{
    V3 origin{};
    if (is_rail(name) && !node_create::is_added(name) && scenery::rail_origin(name, &origin)) {
        Values& values = rail_origins()[name];
        values = {.pos = origin, .angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
        return &values;
    }
    auto it = touched_index().find(name);
    return it == touched_index().end() ? nullptr : &core::at(stored_touched(), it->second).origin;
}

bool assign_value(uint32_t name, const Values& values)
{
    // A rail point: moved in the rail manager (Scenery's rail records - saved as `rail`, reset with the level's
    // scenery), its segments' boxes made again; its node and its angles stay. A point the editor added: its node and its
    // rail manager node both (its `point` record has its place - level_editor_chains.h).
    if (chains::is_added_point(name)) {
        note_origin(name);
        return chains::move_added_point(name, values.pos);
    }
    if (is_rail(name)) {
        if (!scenery::set_rail_point(name, values.pos))
            return false;
        const int index = nodes::get_find(name);
        if (index >= 0)
            nodes::reread(index);
        return true;
    }
    script::Struct* node = struct_named(name);
    if (!node)
        return false;
    note_origin(name);
    store_node_vector(node, kPos, values.pos);
    store_node_vector(node, kAngles, values.angles);
    const nodes::Elem* layer = node_named(name);
    apply_live(layer, name, values);
    entities::node_moved(name, values.pos, values.angles); // (a particle system: its emitter turned and moved, remade)
    const int index = nodes::get_find(name);
    if (index >= 0)
        nodes::reread(index);
    return true;
}

std::vector<LinkRecord> get_changed()
{
    std::vector<LinkRecord> out;
    for (const Touched& t : stored_touched()) {
        Values now;
        if (get_take_in(t.name, &now) && !get_same(now, t.origin))
            out.push_back({.name = t.name, .values = now});
    }
    return out;
}

void get_forget(uint32_t name)
{
    auto it = touched_index().find(name);
    if (it == touched_index().end())
        return;
    stored_touched().erase(stored_touched().begin() + static_cast<std::ptrdiff_t>(it->second));
    touched_index().clear();
    for (size_t i = 0; i < stored_touched().size(); ++i)
        touched_index()[core::at(stored_touched(), i).name] = i;
}

void reset_all()
{
    const std::vector<Touched> touched = stored_touched();
    for (const Touched& t : touched)
        assign_value(t.name, t.origin);
}

void forget_level()
{
    // (a level whose node array stays loaded - the same structures under the same names - gets its places back: the
    // layout puts them in again; another level's nodes of the same name aren't touched)
    for (const Touched& t : stored_touched()) {
        script::Struct* node = struct_named(t.name);
        if (node && node == t.node) {
            store_node_vector(node, kPos, t.origin.pos);
            store_node_vector(node, kAngles, t.origin.angles);
        }
    }
    stored_touched().clear();
    touched_index().clear();
    rail_origins().clear();
}

} // namespace frontend::level_editor::node_edit
