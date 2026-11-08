// NOT ORIGINAL - the level editor's carrying (ours). See level_editor_carry.h.

#include "editor/level_editor_carry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::carry {

namespace {

constexpr float kUnitScaleEpsilon = 1e-6F;         // a scale this close to 1 is unit
constexpr float kZeroEpsilon = 1e-4F;              // a value this close to 0 is zero
constexpr float kQuarterTurnRadians = 1.57079633F; // a quarter turn
constexpr float kFootprintReach = 6.0F;            // how far a point may lie from a footprint and still be over it

} // namespace

// --- the pure parts -----------------------------------------------------------------------------------------------

Placement get_inverse(const Placement& placement)
{
    // y = Q (S x) + o  =>  x = S^-1 Q^-1 (y - o) = Q^-1 (T y) + Q^-1 (T (-o)), T = S^-1 seen through Q (a per-axis
    // scale - level_editor_scale.h: S^-1 Q^-1 = Q^-1 T, odd turns swapping its x and z)
    const V3 scale = placement.scale;
    const V3 reciprocal = {.x = scale.x > 0.0F ? 1.0F / scale.x : 1.0F,
                           .y = scale.y > 0.0F ? 1.0F / scale.y : 1.0F,
                           .z = scale.z > 0.0F ? 1.0F / scale.z : 1.0F};
    const int quarters = (4 - (placement.quarters & 3)) & 3;
    const V3 turned = turned_scale(reciprocal, placement.quarters);
    Placement inverse;
    inverse.quarters = quarters;
    inverse.twist = -placement.twist; // (headings about up: the inverse turns back by the twist too)
    normalise_twist(&inverse);
    inverse.scale = turned;
    inverse.offset = turn_placed(per_axis(placement.offset * -1.0F, turned), inverse);
    return inverse;
}

Placement get_between(const Placement& from, const Placement& to) { return compose(to, get_inverse(from)); }

bool is_none(const Placement& step)
{
    // (a per-axis scale's reciprocal isn't exact - 1 / 1.5 -: Between a placement and itself can be a step of
    // 1.0000001; within a millionth and a ten-thousandth of a unit counts as none)
    auto unit = [](float scale) { return std::fabs(scale - 1.0F) < kUnitScaleEpsilon; };
    auto zero = [](float v) { return std::fabs(v) < kZeroEpsilon; };
    return (step.quarters & 3) == 0 && std::fabs(step.twist) < kZeroEpsilon && unit(step.scale.x) && unit(step.scale.y) && unit(step.scale.z) &&
           zero(step.offset.x) && zero(step.offset.y) && zero(step.offset.z);
}

bool over_footprint(const OrientedBox& box, V3 point, float margin, float above)
{
    const V3 delta = point - box.origin;
    std::array<float, 3> local{};
    for (int i = 0; i < 3; ++i) {
        const float length2 = get_dot(core::at(box.axes, i), core::at(box.axes, i));
        if (!(length2 > 0.0F)) {
            return false;
        }
        // (local units are the box's: a margin in world units is margin / |axis| of them)
        core::at(local, i) = get_dot(delta, core::at(box.axes, i)) / length2;
    }
    const std::array<float, 3> lengths = {{std::sqrt(get_dot(core::at(box.axes, 0), core::at(box.axes, 0))),
                              std::sqrt(get_dot(core::at(box.axes, 1), core::at(box.axes, 1))),
                              std::sqrt(get_dot(core::at(box.axes, 2), core::at(box.axes, 2)))}};
    auto within = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
    return within(core::at(local, 0), box.min.x - (margin / core::at(lengths, 0)),
                  box.max.x + (margin / core::at(lengths, 0))) &&
           within(core::at(local, 2), box.min.z - (margin / core::at(lengths, 2)),
                  box.max.z + (margin / core::at(lengths, 2))) &&
           within(core::at(local, 1), box.min.y - (margin / core::at(lengths, 1)),
                  box.max.y + (above / core::at(lengths, 1)));
}

Attachment attached(uint32_t cluster, const std::vector<uint32_t>& links, uint32_t ground_hit,
                    const std::vector<uint32_t>& members, const std::vector<uint32_t>& member_clusters)
{
    auto in = [](const std::vector<uint32_t>& list, uint32_t id) {
        return id != 0 && std::ranges::find(list, id) != list.end();
    };
    if (in(members, cluster) || in(member_clusters, cluster)) {
        return Attachment::kCluster;
    }
    for (const uint32_t link : links) {
        if (in(members, link)) {
            return Attachment::kLink;
        }
    }
    if (in(members, ground_hit)) {
        return Attachment::kStanding;
    }
    return Attachment::kNone;
}

float turned_heading(float heading, int quarters)
{
    return node_edit::wrap_angle(heading + (static_cast<float>(quarters & 3) * kQuarterTurnRadians));
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCluster = 0x1A3A966BU;    // "Cluster" (a node's TrickObject Cluster=)
constexpr uint32_t kClass = 0x12B4E660U;      // Class
constexpr uint32_t kPedestrian = 0xA0DFAC98U; // Pedestrian

// NOT ORIGINAL grouping - level_editor_carry.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_carry_state()).
struct LevelEditorCarryState {
    bool setting_read_{};
    bool enabled_ = true;
};

LevelEditorCarryState& level_editor_carry_state()
{
    static core::NoDestroy<LevelEditorCarryState> state;
    return state.get();
}

struct Carried {
    std::vector<uint32_t> objects; // (ids)
    std::vector<uint32_t> nodes;   // (names)
    Taken taken;                   // (their places when gathered)
    uint32_t reference = 0;        // (the selection's first piece)
    Placement last;                // (its placement when last followed)
    bool armed = false;
    std::vector<uint32_t> noted; // (objects whose first place was noted)
};
Carried& stored_carried()
{
    static core::NoDestroy<Carried> carried;
    return carried.get();
}

Placement placement_of(uint32_t sector)
{
    Placement placement{}; // (no turn, no offset, scale 1 - the old list {0, {0, 0, 0}, 1.0f} left the scale at (1, 0, 0))
    scenery::placement_now(sector, &placement);
    return placement;
}

// The game's collision ray (as level_editor_scenery.cpp's cast_static: a ProbeRay, constructor 0x00417760, cast
// 0x00417710 - static collision only; the collision object hit at +0x54, its checksum at +0x58): what it meets first.
uint32_t first_hit(V3 from, V3 to, V3* point)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return 0;
    }
    if (!ray->hit().object) {
        return 0;
    }
    const auto& hit = ray->hit().point;
    *point = {.x = hit[0], .y = hit[1], .z = hit[2]};
    return ray->hit().object_checksum_;
}

script::Struct* look_up_node_params(uint32_t name, const nodes::Elem** out)
{
    const int index = (name != 0U) ? nodes::get_find(name) : -1;
    if (index < 0) {
        return nullptr;
    }
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    if (out) {
        *out = &node;
    }
    return nodes::struct_of(node);
}

uint32_t cluster_of(uint32_t name)
{
    uint32_t cluster = 0;
    if (script::Struct* structure = look_up_node_params(name, nullptr)) {
        structure->read_hash_value(kCluster, &cluster, 0);
    }
    return cluster;
}

std::vector<uint32_t> link_names(const nodes::Elem* node)
{
    std::vector<uint32_t> names;
    if (!node) {
        return names;
    }
    const std::vector<nodes::Elem>& all = nodes::get_all();
    for (const int link : node->links) {
        if (link >= 0 && std::cmp_less(link, all.size())) {
            names.push_back(core::at(all, link).name);
        }
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
uint32_t ground_under(const Gather& gather, V3 place)
{
    const bool over = std::ranges::any_of(
        gather.boxes, [&](const auto& box) { return over_footprint(box.second, place, kMargin, kAbove); });
    if (!over) {
        return 0;
    }
    V3 point = {};
    const uint32_t hit = first_hit({.x = place.x, .y = place.y + kRayUp, .z = place.z},
                                   {.x = place.x, .y = place.y - kRayDown, .z = place.z}, &point);
    if ((hit == 0U) || std::ranges::find(gather.members, hit) != gather.members.end() ||
        (scenery::sector_geometry(hit) != nullptr)) {
        return hit;
    }
    for (const auto& box : gather.boxes) {
        if (over_footprint(box.second, point, kFootprintReach, kFootprintReach)) {
            return box.first;
        }
    }
    return hit;
}

Attachment attachment_of(const Gather& gather, uint32_t name, V3 pos)
{
    const nodes::Elem* node = nullptr;
    uint32_t cluster = 0;
    if (script::Struct* structure = look_up_node_params(name, &node)) {
        structure->read_hash_value(kCluster, &cluster, 0);
    }
    const std::vector<uint32_t> links = link_names(node);
    const Attachment attachment = attached(cluster, links, 0, gather.members, gather.clusters);
    if (attachment != Attachment::kNone) {
        return attachment;
    }
    return attached(0, {}, ground_under(gather, pos), gather.members, gather.clusters);
}

void gather_object(objects::Entity* object, void* data)
{
    auto* gather = static_cast<Gather*>(data);
    auto* composite = core::down_cast<objects::GameEntity>(object);
    OrientedBox box{};
    if (!object_edit::model_box(composite, &box)) {
        return;
    }
    uint32_t cls = 0;
    const nodes::Elem* node = nullptr;
    if (script::Struct* structure = look_up_node_params(composite->ident(), &node)) {
        structure->read_hash_value(kClass, &cls, 0);
    }
    if (cls == kPedestrian) {
        return; // (peds walk their paths: not carried)
    }
    if (node && node->family == nodes::Family::kBitMark) {
        return; // (a flag's object: its node is carried, and moves it)
    }
    if (attachment_of(*gather, composite->ident(), object_edit::pos_of(composite)) != Attachment::kNone) {
        gather->objects.push_back(composite->ident());
    }
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) ? object : nullptr;
}

} // namespace

bool is_switched_on()
{
    if (!level_editor_carry_state().setting_read_) {
        level_editor_carry_state().setting_read_ = true;
        level_editor_carry_state().enabled_ = config_int("editor", "carry", 1) != 0;
    }
    return level_editor_carry_state().enabled_;
}

void set_enabled(bool on)
{
    is_switched_on();
    level_editor_carry_state().enabled_ = on;
    config_set_int("editor", "carry", on ? 1 : 0);
    if (!on) {
        disarm();
    }
}

Taken arm()
{
    stored_carried() = Carried{};
    const std::vector<uint32_t>& selection = scenery::get_selection();
    if (!is_switched_on() || selection.empty()) {
        return {};
    }
    Gather gather;
    gather.members = selection;
    for (const uint32_t member : selection) {
        if (const uint32_t cluster = cluster_of(member)) {
            gather.clusters.push_back(cluster);
        }
        OrientedBox box{};
        if (scenery::cuboid(member, &box) && !scenery::is_shadow(member)) {
            gather.boxes.emplace_back(member, box);
        }
    }
    if (objects::EntityHub* manager = objects::object_hub_slot().get()) {
        manager->for_each(gather_object, &gather);
    }
    stored_carried().objects = gather.objects;
    for (const uint32_t id : stored_carried().objects) {
        if (objects::GameEntity* object = live_object(id)) {
            stored_carried().taken.objects.push_back(object_edit::store_away(object));
        }
    }
    for (const nodes::Elem& node : nodes::get_all()) {
        if ((node.name == 0U) || node.hidden || !node_edit::get_movable(node) || node_edit::is_chain(node.name)) {
            continue;
        }
        node_edit::Values values;
        if (!node_edit::get_take_in(node.name, &values)) {
            continue;
        }
        if (attachment_of(gather, node.name, values.pos) != Attachment::kNone &&
            std::ranges::find(stored_carried().nodes, node.name) == stored_carried().nodes.end()) {
            stored_carried().nodes.push_back(node.name);
        }
    }
    stored_carried().taken.nodes = node_edit::get_capture(stored_carried().nodes, node_edit::get_take_in);
    stored_carried().reference = selection.front();
    stored_carried().last = placement_of(stored_carried().reference);
    stored_carried().armed = true;
    if (debug_enabled() && (!stored_carried().objects.empty() || !stored_carried().nodes.empty())) {
        get_log("level editor: carrying %u object%s and %u node%s with the selection",
                static_cast<unsigned>(stored_carried().objects.size()), stored_carried().objects.size() == 1 ? "" : "s",
                static_cast<unsigned>(stored_carried().nodes.size()), stored_carried().nodes.size() == 1 ? "" : "s");
    }
    return stored_carried().taken;
}

void get_follow(void (*on_node)(uint32_t name))
{
    if (!stored_carried().armed || (stored_carried().objects.empty() && stored_carried().nodes.empty())) {
        return;
    }
    const Placement now = placement_of(stored_carried().reference);
    const Placement step = get_between(stored_carried().last, now);
    stored_carried().last = now;
    if (is_none(step)) {
        return;
    }
    const float angle = heading_of(step);
    const bool turned = (step.quarters & 3) != 0 || step.twist != 0.0F;
    for (const uint32_t id : stored_carried().objects) {
        objects::GameEntity* object = live_object(id);
        if (!object) {
            continue;
        }
        if (std::ranges::find(stored_carried().noted, id) == stored_carried().noted.end()) {
            object_edit::note_origin(object);
            object_edit::touch(id);
            stored_carried().noted.push_back(id);
        }
        const V3 to = get_apply(step, object_edit::pos_of(object));
        if (turned) {
            object_edit::turn_about(object, object_edit::pos_of(object), angle); // (its heading; turned about itself)
        }
        object_edit::get_place(object, to);
    }
    for (const uint32_t name : stored_carried().nodes) {
        node_edit::Values values;
        if (!node_edit::get_take_in(name, &values)) {
            continue;
        }
        values.pos = get_apply(step, values.pos);
        if (turned) {
            values.angles.y = node_edit::wrap_angle(values.angles.y + angle);
        }
        node_edit::assign_value(name, values);
        if (on_node) {
            on_node(name);
        }
    }
}

void get_leave()
{
    if (!stored_carried().armed) {
        return;
    }
    for (const object_edit::State& state : stored_carried().taken.objects) {
        object_edit::get_restore(state);
    }
    node_edit::place_in(stored_carried().taken.nodes, [](uint32_t name, const node_edit::Values& values) {
        node_edit::Values now;
        if (node_edit::get_take_in(name, &now) && !node_edit::get_same(now, values)) {
            node_edit::assign_value(name, values);
        }
    });
    stored_carried() = Carried{};
}

void disarm() { stored_carried() = Carried{}; }

bool get_armed() { return stored_carried().armed; }

int total_of() { return stored_carried().armed ? static_cast<int>(stored_carried().objects.size() + stored_carried().nodes.size()) : 0; }

std::vector<uint32_t> carried_objects() { return stored_carried().armed ? stored_carried().objects : std::vector<uint32_t>{}; }

std::vector<uint32_t> carried_nodes() { return stored_carried().armed ? stored_carried().nodes : std::vector<uint32_t>{}; }

} // namespace frontend::level_editor::carry
