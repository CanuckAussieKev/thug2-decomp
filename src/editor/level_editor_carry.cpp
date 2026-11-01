// NOT ORIGINAL - the level editor's carrying (ours). See level_editor_carry.h.

#include "editor/level_editor_carry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "editor/level_editor_nodes.h"
#include "editor/level_editor_scenery.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"

namespace frontend::level_editor::carry {

// --- the pure parts -----------------------------------------------------------------------------------------------

Placement get_inverse(const Placement& placement)
{
    // y = Q (S x) + o  =>  x = S^-1 Q^-1 (y - o) = Q^-1 (T y) + Q^-1 (T (-o)), T = S^-1 seen through Q (a per-axis
    // scale - level_editor_scale.h: S^-1 Q^-1 = Q^-1 T, odd turns swapping its x and z)
    const V3 s = placement.scale;
    const V3 reciprocal = {s.x > 0.0f ? 1.0f / s.x : 1.0f, s.y > 0.0f ? 1.0f / s.y : 1.0f,
                           s.z > 0.0f ? 1.0f / s.z : 1.0f};
    const int quarters = (4 - (placement.quarters & 3)) & 3;
    const V3 t = turned_scale(reciprocal, placement.quarters);
    Placement inverse;
    inverse.quarters = quarters;
    inverse.scale = t;
    inverse.offset = get_quarter_turn(per_axis(placement.offset * -1.0f, t), quarters);
    return inverse;
}

Placement get_between(const Placement& from, const Placement& to) { return compose(to, get_inverse(from)); }

bool is_none(const Placement& step)
{
    // (a per-axis scale's reciprocal isn't exact - 1 / 1.5 -: Between a placement and itself can be a step of
    // 1.0000001; within a millionth and a ten-thousandth of a unit counts as none)
    auto unit = [](float s) { return std::fabs(s - 1.0f) < 1e-6f; };
    auto zero = [](float v) { return std::fabs(v) < 1e-4f; };
    return (step.quarters & 3) == 0 && unit(step.scale.x) && unit(step.scale.y) && unit(step.scale.z) &&
           zero(step.offset.x) && zero(step.offset.y) && zero(step.offset.z);
}

bool over_footprint(const OrientedBox& box, V3 p, float margin, float above)
{
    const V3 d = p - box.origin;
    float local[3];
    for (int i = 0; i < 3; ++i) {
        const float length2 = get_dot(box.axes[i], box.axes[i]);
        if (!(length2 > 0.0f))
            return false;
        // (local units are the box's: a margin in world units is margin / |axis| of them)
        local[i] = get_dot(d, box.axes[i]) / length2;
    }
    const float lengths[3] = {std::sqrt(get_dot(box.axes[0], box.axes[0])),
                              std::sqrt(get_dot(box.axes[1], box.axes[1])),
                              std::sqrt(get_dot(box.axes[2], box.axes[2]))};
    auto within = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
    return within(local[0], box.min.x - margin / lengths[0], box.max.x + margin / lengths[0]) &&
           within(local[2], box.min.z - margin / lengths[2], box.max.z + margin / lengths[2]) &&
           within(local[1], box.min.y - margin / lengths[1], box.max.y + above / lengths[1]);
}

Attachment attached(uint32_t cluster, const std::vector<uint32_t>& links, uint32_t ground_hit,
                    const std::vector<uint32_t>& members, const std::vector<uint32_t>& member_clusters)
{
    auto in = [](const std::vector<uint32_t>& list, uint32_t id) {
        return id != 0 && std::find(list.begin(), list.end(), id) != list.end();
    };
    if (in(members, cluster) || in(member_clusters, cluster))
        return Attachment::kCluster;
    for (const uint32_t link : links) {
        if (in(members, link))
            return Attachment::kLink;
    }
    if (in(members, ground_hit))
        return Attachment::kStanding;
    return Attachment::kNone;
}

float turned_heading(float heading, int quarters)
{
    return node_edit::wrap_angle(heading + static_cast<float>(quarters & 3) * 1.57079633f);
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCluster = 0x1A3A966Bu;    // "Cluster" (a node's TrickObject Cluster=)
constexpr uint32_t kClass = 0x12B4E660u;      // Class
constexpr uint32_t kPedestrian = 0xA0DFAC98u; // Pedestrian

bool g_setting_read, g_enabled = true;

struct Carried {
    std::vector<uint32_t> objects; // (ids)
    std::vector<uint32_t> nodes;   // (names)
    Taken taken;                   // (their places when gathered)
    uint32_t reference = 0;        // (the selection's first piece)
    Placement last;                // (its placement when last followed)
    bool armed = false;
    std::vector<uint32_t> noted; // (objects whose first place was noted)
};
Carried g_carried;

Placement placement_of(uint32_t sector)
{
    Placement placement = {0, {0.0f, 0.0f, 0.0f}, 1.0f};
    scenery::placement_now(sector, &placement);
    return placement;
}

// The game's collision ray (as level_editor_scenery.cpp's cast_static: a ProbeRay, constructor 0x00417760, cast
// 0x00417710 - static collision only; the collision object hit at +0x54, its checksum at +0x58): what it meets first.
uint32_t first_hit(V3 from, V3 to, V3* point)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {from.x, from.y, from.z, 1.0f};
    const maths::Vec4 end = {to.x, to.y, to.z, 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return 0;
    if (!ray->hit_.object)
        return 0;
    const float* hit = &ray->hit_.point.x;
    *point = {hit[0], hit[1], hit[2]};
    return ray->hit_.object_checksum_;
}

script::Struct* look_up_node_params(uint32_t name, const nodes::Elem** out)
{
    const int index = name ? nodes::get_find(name) : -1;
    if (index < 0)
        return nullptr;
    const nodes::Elem& node = nodes::get_all()[index];
    if (out)
        *out = &node;
    return nodes::struct_of(node);
}

uint32_t cluster_of(uint32_t name)
{
    uint32_t cluster = 0;
    if (script::Struct* s = look_up_node_params(name, nullptr))
        s->read_hash_value(kCluster, &cluster, 0);
    return cluster;
}

std::vector<uint32_t> link_names(const nodes::Elem* node)
{
    std::vector<uint32_t> names;
    if (!node)
        return names;
    const std::vector<nodes::Elem>& all = nodes::get_all();
    for (const int link : node->links) {
        if (link >= 0 && link < static_cast<int>(all.size()))
            names.push_back(all[link].name);
    }
    return names;
}

struct Gather {
    std::vector<uint32_t> members, clusters;
    std::vector<std::pair<uint32_t, OrientedBox>> boxes; // (each member's but shadows')
    std::vector<uint32_t> objects;
};

// What the ground ray below `p` meets first, when `p` is over one of the members' footprints (0: not over one, or
// nothing below). A piece whose collision is still in a collision-only sector (level_editor_shared_coll.h: taken along
// only at its first move - TR's SkWare_Rail01's faces in SkWare_Main_1) counts when the hit is within its box grown by
// the shared collision's 6 units.
uint32_t ground_under(const Gather& g, V3 p)
{
    const bool over = std::any_of(g.boxes.begin(), g.boxes.end(),
                                  [&](const auto& box) { return over_footprint(box.second, p, kMargin, kAbove); });
    if (!over)
        return 0;
    V3 point = {};
    const uint32_t hit = first_hit({p.x, p.y + kRayUp, p.z}, {p.x, p.y - kRayDown, p.z}, &point);
    if (!hit || std::find(g.members.begin(), g.members.end(), hit) != g.members.end() || scenery::sector_geometry(hit))
        return hit;
    for (const auto& box : g.boxes) {
        if (over_footprint(box.second, point, 6.0f, 6.0f))
            return box.first;
    }
    return hit;
}

Attachment attachment_of(const Gather& g, uint32_t name, V3 pos)
{
    const nodes::Elem* node = nullptr;
    uint32_t cluster = 0;
    if (script::Struct* s = look_up_node_params(name, &node))
        s->read_hash_value(kCluster, &cluster, 0);
    const std::vector<uint32_t> links = link_names(node);
    const Attachment a = attached(cluster, links, 0, g.members, g.clusters);
    if (a != Attachment::kNone)
        return a;
    return attached(0, {}, ground_under(g, pos), g.members, g.clusters);
}

void gather_object(objects::Entity* object, void* data)
{
    auto* g = static_cast<Gather*>(data);
    auto* composite = static_cast<objects::GameEntity*>(object);
    OrientedBox box{};
    if (!object_edit::model_box(composite, &box))
        return;
    uint32_t cls = 0;
    const nodes::Elem* node = nullptr;
    if (script::Struct* s = look_up_node_params(composite->ident_, &node))
        s->read_hash_value(kClass, &cls, 0);
    if (cls == kPedestrian)
        return; // (peds walk their paths: not carried)
    if (node && node->family == nodes::Family::kBitMark)
        return; // (a flag's object: its node is carried, and moves it)
    if (attachment_of(*g, composite->ident_, object_edit::pos_of(composite)) != Attachment::kNone)
        g->objects.push_back(composite->ident_);
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) ? object : nullptr;
}

} // namespace

bool is_switched_on()
{
    if (!g_setting_read) {
        g_setting_read = true;
        g_enabled = config_int("editor", "carry", 1) != 0;
    }
    return g_enabled;
}

void set_enabled(bool on)
{
    is_switched_on();
    g_enabled = on;
    config_set_int("editor", "carry", on ? 1 : 0);
    if (!on)
        disarm();
}

Taken arm()
{
    g_carried = Carried{};
    const std::vector<uint32_t>& selection = scenery::get_selection();
    if (!is_switched_on() || selection.empty())
        return {};
    Gather g;
    g.members = selection;
    for (const uint32_t member : selection) {
        if (const uint32_t cluster = cluster_of(member))
            g.clusters.push_back(cluster);
        OrientedBox box{};
        if (scenery::cuboid(member, &box) && !scenery::is_shadow(member))
            g.boxes.emplace_back(member, box);
    }
    if (objects::EntityHub* manager = objects::object_hub_slot())
        manager->for_each(gather_object, &g);
    g_carried.objects = g.objects;
    for (const uint32_t id : g_carried.objects) {
        if (objects::GameEntity* object = live_object(id))
            g_carried.taken.objects.push_back(object_edit::store_away(object));
    }
    for (const nodes::Elem& node : nodes::get_all()) {
        if (!node.name || node.hidden || !node_edit::get_movable(node) || node_edit::is_chain(node.name))
            continue;
        node_edit::Values values;
        if (!node_edit::get_take_in(node.name, &values))
            continue;
        if (attachment_of(g, node.name, values.pos) != Attachment::kNone &&
            std::find(g_carried.nodes.begin(), g_carried.nodes.end(), node.name) == g_carried.nodes.end())
            g_carried.nodes.push_back(node.name);
    }
    g_carried.taken.nodes = node_edit::get_capture(g_carried.nodes, node_edit::get_take_in);
    g_carried.reference = selection.front();
    g_carried.last = placement_of(g_carried.reference);
    g_carried.armed = true;
    if (debug_enabled() && (!g_carried.objects.empty() || !g_carried.nodes.empty()))
        get_log("level editor: carrying %u object%s and %u node%s with the selection",
                static_cast<unsigned>(g_carried.objects.size()), g_carried.objects.size() == 1 ? "" : "s",
                static_cast<unsigned>(g_carried.nodes.size()), g_carried.nodes.size() == 1 ? "" : "s");
    return g_carried.taken;
}

void get_follow(void (*on_node)(uint32_t name))
{
    if (!g_carried.armed || (g_carried.objects.empty() && g_carried.nodes.empty()))
        return;
    const Placement now = placement_of(g_carried.reference);
    const Placement step = get_between(g_carried.last, now);
    g_carried.last = now;
    if (is_none(step))
        return;
    const float angle = static_cast<float>(step.quarters & 3) * 1.57079633f;
    for (const uint32_t id : g_carried.objects) {
        objects::GameEntity* object = live_object(id);
        if (!object)
            continue;
        if (std::find(g_carried.noted.begin(), g_carried.noted.end(), id) == g_carried.noted.end()) {
            object_edit::note_origin(object);
            object_edit::touch(id);
            g_carried.noted.push_back(id);
        }
        const V3 to = get_apply(step, object_edit::pos_of(object));
        if (step.quarters & 3)
            object_edit::turn_about(object, object_edit::pos_of(object), angle); // (its heading; turned about itself)
        object_edit::get_place(object, to);
    }
    for (const uint32_t name : g_carried.nodes) {
        node_edit::Values values;
        if (!node_edit::get_take_in(name, &values))
            continue;
        values.pos = get_apply(step, values.pos);
        if (step.quarters & 3)
            values.angles.y = turned_heading(values.angles.y, step.quarters);
        node_edit::assign_value(name, values);
        if (on_node)
            on_node(name);
    }
}

void get_leave()
{
    if (!g_carried.armed)
        return;
    for (const object_edit::State& state : g_carried.taken.objects)
        object_edit::get_restore(state);
    node_edit::place_in(g_carried.taken.nodes, [](uint32_t name, const node_edit::Values& values) {
        node_edit::Values now;
        if (node_edit::get_take_in(name, &now) && !node_edit::get_same(now, values))
            node_edit::assign_value(name, values);
    });
    g_carried = Carried{};
}

void disarm() { g_carried = Carried{}; }

bool get_armed() { return g_carried.armed; }

int total_of() { return g_carried.armed ? static_cast<int>(g_carried.objects.size() + g_carried.nodes.size()) : 0; }

std::vector<uint32_t> carried_objects() { return g_carried.armed ? g_carried.objects : std::vector<uint32_t>{}; }

std::vector<uint32_t> carried_nodes() { return g_carried.armed ? g_carried.nodes : std::vector<uint32_t>{}; }

} // namespace frontend::level_editor::carry
