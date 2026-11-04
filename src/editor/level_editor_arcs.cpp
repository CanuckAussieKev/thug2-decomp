// NOT ORIGINAL - the level editor's jump arcs (ours). See level_editor_arcs.h.

#include "editor/level_editor_arcs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/enum_ops.h"
#include "editor/level_editor.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "engine/render/coll_static.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/run_log.h"
#include "engine/gfx/sector.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/core/checksums.h"
#include "engine/render/coll_obj.h"

namespace frontend::level_editor::arcs {

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

namespace {

constexpr float kTinyLength = 1e-6F;
constexpr float kTinyLengthSquared = 1e-12F;
constexpr float kStatStep = 0.1F;             // the share of the stat range one stat point is worth
constexpr float kVertGravityDivisor = 1.1F;   // vert gravity is the base divided by this
constexpr float kJumpAtStatMin = 414.0F;      // jump speed at the lowest ollie stat ...
constexpr float kJumpAtStatMax = 450.0F;      // ... and at the highest
constexpr float kPushSpeedAtStatMin = 532.0F; // push speed at the lowest speed stat ...
constexpr float kPushSpeedAtStatMax = 675.0F; // ... and at the highest
constexpr float kPushSpeedCap = 900.0F;
constexpr float kTopSpeedAtStatMin = 757.0F;
constexpr float kTopSpeedAtStatMax = 900.0F;
constexpr float kMaxSpeedAtStatMin = 957.0F;
constexpr float kMaxSpeedAtStatMax = 1100.0F;
constexpr uint32_t kNoSpeedColour = 0xFFFFFFFF; // ARGB: white, for an index with no speed
constexpr float kHalfGravityFactor = 2.0F;      // v^2 / (2 g)
constexpr std::size_t kSpanTextSize = 48;
constexpr float kUnitsPerFoot = 12.0F; // inches
constexpr std::size_t kSourceTextSize = 96;
constexpr std::size_t kMaxLips = 20000;       // lip points collected at most
constexpr float kMinStepSquared = 0.01F;
constexpr float kLaunchLift =
    2.0F; // a flight starts this far above the lip so the lip itself is not the first thing met
constexpr float kFrameRate = 60.0F; // the simulation step is a frame
constexpr float kMaxFlightSeconds = 6.0F;
constexpr int kRefreshMilliseconds = 250;     // the arcs are worked out again after this long
constexpr uint32_t kLipColour = 0xFFFFFFFF;   // ARGB
constexpr float kLipWidth = 2.0F;
constexpr float kTangentLength = 40.0F;
constexpr uint32_t kTangentColour = 0xC0FFFFFF; // ARGB
constexpr float kTangentWidth = 1.5F;
constexpr std::size_t kLabelTextSize = 96;
constexpr float kDegreesPerRadian = 57.29578F;
constexpr float kLipLabelOffset = 6.0F;
constexpr float kFlightWidth = 2.0F;
constexpr float kCrossHalfWidth = 12.0F; // the cross at the apex
constexpr float kLandingMarkSize = 14.0F;
constexpr std::size_t kDropTextSize = 48;
constexpr float kWallNormalY = 0.5F; // a landing on a face this steep is a wall hit
constexpr float kLandingLabelOffsetX = 8.0F;
constexpr float kLandingLabelRise = 10.0F;
constexpr float kLandingLabelRowHeight = 14.0F;
constexpr std::size_t kLineTextSize = 160;

constexpr V3 kUp = {.x = 0.0F, .y = 1.0F, .z = 0.0F};

float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }

bool eligible(render::FaceFlag flags)
{
    if (core::any(flags & (render::FaceFlag::kNotCollidable | render::FaceFlag::kTrigger |
                           render::FaceFlag::kWall))) { // (not collidable, a trigger, wallride)
        return false;
    }
    if (core::any(flags & render::FaceFlag::kNotSkatable) &&
        !core::any(flags & (render::FaceFlag::kSkatable | render::FaceFlag::kVert))) { // (not skatable)
        return false;
    }
    return true;
}

struct GroupSet {
    V3 heading = {}; // (area-weighted sum, then unit)
    float area = 0.0F;
    std::vector<int> faces;
};

} // namespace

std::vector<Lip> find_lips(const std::vector<Face>& faces, int max_lips)
{
    std::vector<Lip> lips;
    if (max_lips <= 0) {
        return lips;
    }
    // The faces' raw normals and areas; the piece's winding convention from its sloped area.
    struct DetailsOf {
        V3 n;
        float area;
        bool use;
    };
    std::vector<DetailsOf> info(faces.size());
    float up_area = 0.0F;
    float down_area = 0.0F;
    for (size_t i = 0; i < faces.size(); ++i) {
        const Face& face = core::at(faces, i);
        const V3 normal = perpendicular(face.b - face.a, face.c - face.a);
        const float length = len_of(normal);
        core::at(info, i) = {.n = length > kTinyLength ? normal * (1.0F / length) : V3{},
                             .area = length * kHalf,
                             .use = length > kTinyLength && eligible(face.flags)};
        if (!core::at(info, i).use) {
            continue;
        }
        const float y = core::at(info, i).n.y;
        if (std::fabs(y) > kSteepNormalY && std::fabs(y) < kFlatNormalY) {
            (y > 0.0F ? up_area : down_area) += core::at(info, i).area;
        }
    }
    const float convention = down_area > up_area ? -1.0F : 1.0F;
    // The sloped faces in groups by their uphill heading (largest first).
    std::vector<int> order;
    for (size_t i = 0; i < faces.size(); ++i) {
        if (core::at(info, i).use) {
            order.push_back(static_cast<int>(i));
        }
    }
    std::ranges::sort(order, [&](int a, int b) { return core::at(info, a).area > core::at(info, b).area; });
    std::vector<GroupSet> groups;
    auto heading_of = [](V3 normal) { return make_unit({.x = -normal.x, .y = 0.0F, .z = -normal.z}); };
    for (const int index : order) {
        const V3 normal = core::at(info, index).n * convention;
        if (!(normal.y > kSteepNormalY && normal.y < kFlatNormalY)) {
            continue;
        }
        const V3 heading = heading_of(normal);
        GroupSet* into = nullptr;
        for (GroupSet& group_set : groups) {
            if (get_dot(make_unit(group_set.heading), heading) > kHeadingTolerance) {
                into = &group_set;
                break;
            }
        }
        if (!into) {
            groups.push_back({});
            into = &groups.back();
        }
        into->heading = into->heading + heading * core::at(info, index).area;
        into->area += core::at(info, index).area;
        into->faces.push_back(index);
    }
    for (GroupSet& group_set : groups) {
        group_set.heading = make_unit(group_set.heading);
    }
    // Near-vertical faces join the group whose front they face.
    for (const int index : order) {
        const V3 normal = core::at(info, index).n * convention;
        if (std::fabs(normal.y) > kSteepNormalY) {
            continue;
        }
        const V3 heading = heading_of(normal);
        for (GroupSet& group_set : groups) {
            if (get_dot(group_set.heading, heading) > kHeadingTolerance) {
                group_set.faces.push_back(index);
                group_set.area += core::at(info, index).area;
                break;
            }
        }
    }
    std::ranges::sort(groups, [](const GroupSet& a, const GroupSet& b) { return a.area > b.area; });

    for (const GroupSet& group_set : groups) {
        if (std::cmp_greater_equal(lips.size(), max_lips)) {
            break;
        }
        float top = -kFarDistance;
        float bottom = kFarDistance;
        for (const int index : group_set.faces) {
            for (const V3 point : {core::at(faces, index).a, core::at(faces, index).b, core::at(faces, index).c}) {
                top = std::max(top, point.y);
                bottom = std::min(bottom, point.y);
            }
        }
        if (top - bottom < kMinLipHeight) {
            continue;
        }
        Lip lip;
        lip.heading = group_set.heading;
        lip.across = {.x = group_set.heading.z, .y = 0.0F, .z = -group_set.heading.x};
        lip.height = top - bottom;
        // The top faces' tangent (by area) and the edge's vertices.
        constexpr float kTop = 1.5F;
        V3 tangent = {};
        float min = kFarDistance;
        float max = -kFarDistance;
        float u_sum = 0.0F;
        int count = 0;
        bool vert_flag = false;
        for (const int index : group_set.faces) {
            const Face& face = core::at(faces, index);
            bool at_top = false;
            for (const V3 point : {face.a, face.b, face.c}) {
                if (point.y < top - kTop) {
                    continue;
                }
                at_top = true;
                const float scale = get_dot(point, lip.across);
                const float u = get_dot(point, group_set.heading);
                min = std::min(min, scale);
                max = std::max(max, scale);
                u_sum += u;
                ++count;
            }
            if (!at_top) {
                continue;
            }
            const V3 normal = core::at(info, index).n * convention;
            const V3 unit_tangent = make_unit(kUp - normal * normal.y);
            tangent = tangent + unit_tangent * core::at(info, index).area;
            vert_flag |= core::any(face.flags & render::FaceFlag::kVert);
        }
        if (count == 0 || get_dot(tangent, tangent) < kTinyLengthSquared) {
            continue;
        }
        lip.tangent = make_unit(tangent);
        // (a tangent pointing back over the top - an overhang's - launches as vert)
        lip.vert = vert_flag || lip.tangent.y > kVertTangentY || get_dot(lip.tangent, group_set.heading) < 0.0F;
        const float scale = (min + max) * 0.5F;
        const float u = u_sum / static_cast<float>(count);
        lip.width = max - min;
        // (across and heading are orthonormal and horizontal: the point's x / z from its two coordinates)
        const V3 flat = lip.across * scale + group_set.heading * u;
        lip.point = {.x = flat.x, .y = top, .z = flat.z};
        lips.push_back(lip);
    }
    return lips;
}

float scaled_stat(float low, float high, float stat, bool has_limit, float limit)
{
    auto result = static_cast<float>(((static_cast<double>(high) - low) * stat * kStatStep) + low);
    if (!has_limit) {
        return result;
    }
    if (low < high) {
        return result > limit ? limit : result;
    }
    return result < limit ? limit : result;
}

Physics arcs_fallback()
{
    Physics physics;
    physics.air_gravity_ = -kDefaultAirGravity / 1.0F;
    physics.vert_gravity_ = -kDefaultAirGravity / kVertGravityDivisor;
    physics.jump = scaled_stat(kJumpAtStatMin, kJumpAtStatMax, kDefaultStat, false, 0.0F);
    core::at(physics.speeds, 0) =
        scaled_stat(kPushSpeedAtStatMin, kPushSpeedAtStatMax, kDefaultStat, true, kPushSpeedCap);
    core::at(physics.speeds, 1) = scaled_stat(kTopSpeedAtStatMin, kTopSpeedAtStatMax, kDefaultStat, false, 0.0F);
    core::at(physics.speeds, 2) = scaled_stat(kMaxSpeedAtStatMin, kMaxSpeedAtStatMax, kDefaultStat, false, 0.0F);
    return physics;
}

const char* speed_name(int index)
{
    static const std::array<const char*, kSpeeds> kNames = {{"push", "top", "max"}};
    return index >= 0 && index < kSpeeds ? core::at(kNames, index) : "?";
}

uint32_t speed_colour(int index)
{
    static const std::array<uint32_t, kSpeeds> kColours = {{0xFF50E070, 0xFFFFD040, 0xFFFF5040}};
    return index >= 0 && index < kSpeeds ? core::at(kColours, index) : kNoSpeedColour;
}

V3 launch_velocity(const Lip& lip, LaunchSpeeds speeds)
{
    const auto speed = speeds.speed;
    const auto jump = speeds.jump;
    V3 v = lip.tangent * speed;
    v.y = std::max(v.y, 0.0F);
    v.y += jump;
    return v;
}

Flight get_fly(V3 start, V3 velocity, FlightSteps steps, const Cast& cast)
{
    const auto gravity = steps.gravity;
    const auto dt = steps.dt;
    const auto max_time = steps.max_time_;
    const auto skip = steps.skip;
    Flight flight;
    flight.points.push_back(start);
    flight.apex = start;
    V3 point = start;
    V3 v = velocity;
    float fraction = 0.0F;
    while (fraction < max_time) {
        // (as do_air_physics: position += v t + (0, g, 0) t^2 / 2, then v.y += g t)
        const V3 next = point + v * dt + V3{.x = 0.0F, .y = 0.5F * gravity * dt * dt, .z = 0.0F};
        v.y += gravity * dt;
        fraction += dt;
        V3 hit{};
        V3 normal{};
        if (fraction > skip && cast && cast(point, next, &hit, &normal)) {
            flight.points.push_back(hit);
            flight.landed = true;
            flight.landing = hit;
            flight.normal = normal;
            flight.time = fraction;
            return flight;
        }
        point = next;
        if (point.y > flight.apex.y) {
            flight.apex = point;
        }
        flight.points.push_back(point);
    }
    flight.time = fraction;
    return flight;
}

float rise_height(float vertical_speed, float gravity)
{
    if (vertical_speed <= 0.0F || gravity >= 0.0F) {
        return 0.0F;
    }
    return vertical_speed * vertical_speed / (-kHalfGravityFactor * gravity);
}

std::string span_to(float units)
{
    std::array<char, kSpanTextSize> text{};
    text::format_to(text, "%.0f (%.1f ft)", units, units / kUnitsPerFoot);
    return text.data();
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kFallPull = 0xFAA40754;     // Physics_Air_Gravity
constexpr uint32_t kAirHang = 0xC31CA696;      // Physics_Air_hang_Stat
constexpr uint32_t kVertHang = 0x441C38A0;     // Physics_Vert_hang_Stat
constexpr uint32_t kJumpSpeed = 0x02ADE3AD;    // Physics_Jump_Speed_Stat
constexpr uint32_t kKickSpeed = 0x92E0247C;    // Skater_Max_Crouched_Kick_Speed_Stat
constexpr uint32_t kMaxSpeed = 0x2EACDDB3;     // Skater_Max_Speed_Stat
constexpr uint32_t kSpeedCapStat = 0xCC5F87AA; // Skater_Max_Max_Speed_Stat
constexpr uint32_t kDefaultStats = 0x6357D57C; // Skater_Default_Stats
constexpr uint32_t kLimit = 0x8069179F;        // limit
constexpr int kStatOllie = 2, kStatSpeed = 3;  // (STATS_OLLIE, STATS_SPEED)

Physics& physics_state()
{
    static Physics value = arcs_fallback();
    return value;
}
bool& have_read()
{
    static bool have_read{};
    return have_read;
}

// A stat structure's value at `stat` (its pair, its limit), from the physics structure or the global one; false:
// not found.
bool stat_value(script::Struct* physics, checksums::Name stat_name, float stat, float* value)
{
    const uint32_t name = stat_name.value();
    script::Struct* params = nullptr;
    if (!physics || !physics->read_sub_struct(name, &params, 0)) {
        params = script::read_sub_struct(name);
    }
    if (!params) {
        return false;
    }
    script::Pair pair;
    if (!params->fetch_pair(0U, &pair, false)) {
        return false;
    }
    float limit = 0.0F;
    const bool has_limit = params->fetch_float(kLimit, &limit, 0);
    *value = scaled_stat(pair.x(), pair.y(), stat, has_limit, limit);
    return true;
}

void read_physics()
{
    Physics fallback_physics = arcs_fallback();
    auto* skater = core::down_cast<objects::Rider>(objects::find_object(kSkater));
    script::Struct* physics = objects::get_physics_structure(skater);
    if (!physics) {
        physics_state() = fallback_physics;
        return;
    }
    const float default_stat = script::fetch_float(kDefaultStats);
    fallback_physics.speed_stat_ = skater ? skater->fetch_tally(kStatSpeed) : default_stat;
    fallback_physics.ollie_stat_ = skater ? skater->fetch_tally(kStatOllie) : default_stat;
    const float gravity = objects::read_tuning_float(checksums::Name{kFallPull}, 0, skater);
    const float air_hang = script::fetch_float(kAirHang);
    const float vert_hang = script::fetch_float(kVertHang);
    if (gravity < 0.0F) {
        fallback_physics.air_gravity_ = air_hang > 0.0F ? gravity / air_hang : gravity;
        fallback_physics.vert_gravity_ = vert_hang > 0.0F ? gravity / vert_hang : gravity;
    }
    stat_value(physics, checksums::Name{kJumpSpeed}, fallback_physics.ollie_stat_, &fallback_physics.jump);
    stat_value(physics, checksums::Name{kKickSpeed}, fallback_physics.speed_stat_,
               &core::at(fallback_physics.speeds, 0));
    stat_value(physics, checksums::Name{kMaxSpeed}, fallback_physics.speed_stat_,
               &core::at(fallback_physics.speeds, 1));
    stat_value(physics, checksums::Name{kSpeedCapStat}, fallback_physics.speed_stat_,
               &core::at(fallback_physics.speeds, 2));
    std::array<char, kSourceTextSize> source{};
    text::format_to(source, "%s, speed stat %.1f, ollie stat %.1f", skater ? "the skater's physics" : "skater_physics",
                    fallback_physics.speed_stat_, fallback_physics.ollie_stat_);
    fallback_physics.source = source.data();
    physics_state() = fallback_physics;
}

// A sector's collision faces in world space (none: no collision, or it's switched off).
void sector_faces(uint32_t checksum, std::vector<Face>* out)
{
    const render::WorldCell* sector = scenery::sector_of(checksum);
    if (!sector) {
        return;
    }
    const render::HitMesh* coll = sector->hit_body();
    if (!render::is_static_collider(coll) || ((coll->flag_bits() & 4) != 0)) {
        return;
    }
    const auto* tri = coll->geometry();
    if (!tri || !tri->faces() || !tri->vertices()) {
        return;
    }
    for (int face_index = 0; std::cmp_less(face_index, tri->tri_count()) && out->size() < kMaxLips; ++face_index) {
        std::array<V3, 3> point{};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const uint32_t index = tri->face_vertex(face_index, k);
            if (index >= tri->num_vertices()) {
                ok = false;
                break;
            }
            const maths::Vec4 v = tri->read_stored_vertex(static_cast<int>(index));
            core::at(point, k) = {.x = v.x, .y = v.y, .z = v.z};
        }
        if (!ok) {
            continue;
        }
        const render::FaceFlag flags = tri->face_flags(face_index);
        out->push_back({.a = core::at(point, 0), .b = core::at(point, 1), .c = core::at(point, 2), .flags = flags});
    }
}

// The game's collision ray (a ProbeRay: constructor 0x00417760, cast 0x00417710 - static collision only, ignoring
// faces flagged 0x10 as every feeler does; the hit's normal at +0x24, its point at +0x34, the collision object hit
// at +0x54 and its checksum at +0x58).
bool cast_feeler(V3 from, V3 to, SurfaceOut surface, uint32_t* sector)
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
    const auto& hit_point = ray->hit().point;
    const auto& hit_normal = ray->hit().normal;
    *hit = {.x = hit_point[0], .y = hit_point[1], .z = hit_point[2]};
    *normal = {.x = hit_normal[0], .y = hit_normal[1], .z = hit_normal[2]};
    *sector = ray->hit().object ? ray->hit().object_checksum_ : 0;
    return true;
}

struct LipArcs {
    Lip lip;
    std::array<Flight, kSpeeds> flights;
    std::array<uint32_t, kSpeeds> landed_on_ = {};
    std::array<float, kSpeeds> rise = {};
};
struct Cache {
    std::vector<uint32_t> sectors;
    std::vector<V3> boxes; // (each sector's box min, max: moved since?)
    std::vector<LipArcs> arcs;
    std::chrono::steady_clock::time_point made;
    bool valid = false;
};
Cache& cache()
{
    static core::NoDestroy<Cache> cache;
    return cache.get();
}

std::vector<V3> boxes_of(const std::vector<uint32_t>& sectors)
{
    std::vector<V3> boxes;
    for (const uint32_t sector : sectors) {
        OrientedBox box{};
        if (scenery::cuboid(sector, &box)) {
            V3 min{};
            V3 max{};
            bounds_of(box, &min, &max);
            boxes.push_back(min);
            boxes.push_back(max);
        }
    }
    return boxes;
}

bool same_boxes(const std::vector<V3>& a, const std::vector<V3>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const V3 delta = core::at(a, i) - core::at(b, i);
        if (get_dot(delta, delta) > kMinStepSquared) {
            return false;
        }
    }
    return true;
}

void get_remake(const std::vector<uint32_t>& sectors, const std::vector<V3>& boxes)
{
    cache() = Cache{};
    cache().sectors = sectors;
    cache().boxes = boxes;
    cache().made = std::chrono::steady_clock::now();
    cache().valid = true;
    read_physics();
    have_read() = true;
    std::vector<Face> faces;
    for (const uint32_t sector : sectors) {
        sector_faces(sector, &faces);
    }
    for (const Lip& lip : find_lips(faces, 4)) {
        LipArcs arcs;
        arcs.lip = lip;
        for (int i = 0; i < kSpeeds; ++i) {
            const V3 v = launch_velocity(lip, {.speed = core::at(physics_state().speeds, i), .jump = physics_state().jump});
            if (lip.vert) {
                core::at(arcs.rise, i) = rise_height(v.y, physics_state().vert_gravity_);
                continue;
            }
            uint32_t* landed_on = &core::at(arcs.landed_on_, i);
            // (from just off the lip: 2 units up, so the lip's own edge isn't the first thing met)
            core::at(arcs.flights, i) =
                get_fly(lip.point + V3{.x = 0.0F, .y = kLaunchLift, .z = 0.0F}, v,
                        {.gravity = physics_state().air_gravity_,
                         .dt = 1.0F / kFrameRate,
                         .max_time_ = kMaxFlightSeconds,
                         .skip = 1.0F / kFrameRate},
                        [landed_on](V3 from, V3 to, V3* hit, V3* normal) {
                            return cast_feeler(from, to, {.point = hit, .normal = normal}, landed_on);
                        });
        }
        cache().arcs.push_back(arcs);
    }
    if (debug_enabled()) {
        get_log("level editor: jump arcs - %d lips (%s; g %.0f / vert %.0f, jump %.0f, speeds %.0f / %.0f / %.0f)",
                static_cast<int>(cache().arcs.size()), physics_state().source.c_str(), physics_state().air_gravity_,
                physics_state().vert_gravity_, physics_state().jump, core::at(physics_state().speeds, 0),
                core::at(physics_state().speeds, 1), core::at(physics_state().speeds, 2));
    }
}

} // namespace

const Physics& current_physics(bool refresh)
{
    if (refresh || !have_read()) {
        read_physics();
        have_read() = true;
    }
    return physics_state();
}

void add_overlay(const overlays::Painter& painter, const std::vector<uint32_t>& sectors)
{
    if (!overlays::get_shown(overlays::kJumpArcs) || sectors.empty()) {
        cache().valid = false;
        return;
    }
    const std::vector<V3> boxes = boxes_of(sectors);
    const bool stale = !cache().valid || cache().sectors != sectors || !same_boxes(cache().boxes, boxes);
    // (while the selection moves: made again at most four times a second)
    if (stale && (!cache().valid || cache().sectors != sectors ||
                  std::chrono::steady_clock::now() - cache().made > std::chrono::milliseconds(kRefreshMilliseconds))) {
        get_remake(sectors, boxes);
    }
    for (const LipArcs& arcs : cache().arcs) {
        const Lip& lip = arcs.lip;
        const V3 half = lip.across * (lip.width * 0.5F);
        (void)painter.get_seg(lip.point - half, lip.point + half, kLipColour, kLipWidth);
        (void)painter.get_seg(lip.point, lip.point + lip.tangent * kTangentLength, kTangentColour, kTangentWidth);
        std::array<char, kLabelTextSize> label{};
        text::format_to(label, "lip %.0f high, %.0f deg%s", lip.height,
                        std::asin(std::clamp(lip.tangent.y, -1.0F, 1.0F)) * kDegreesPerRadian,
                        lip.vert ? " (vert)" : "");
        painter.draw_label(lip.point, kLipColour, label.data(), kLipLabelOffset, kLipLabelOffset);
        for (int i = 0; i < kSpeeds; ++i) {
            const uint32_t colour = speed_colour(i);
            if (lip.vert) {
                // (straight up and back down: how high)
                const V3 side = lip.across * (lip.width * (0.15F * static_cast<float>(i - 1)));
                const V3 base = lip.point + side;
                const V3 apex = base + V3{.x = 0.0F, .y = core::at(arcs.rise, i), .z = 0.0F};
                (void)painter.get_seg(base, apex, colour, kFlightWidth);
                (void)painter.get_seg(apex - lip.across * kCrossHalfWidth, apex + lip.across * kCrossHalfWidth, colour, kFlightWidth);
                painter.draw_label(apex, colour, std::string(speed_name(i)) + ": rises " + span_to(core::at(arcs.rise, i)));
                continue;
            }
            const Flight& flight = core::at(arcs.flights, i);
            for (size_t k = 1; k < flight.points.size(); ++k) {
                (void)painter.get_seg(core::at(flight.points, k - 1), core::at(flight.points, k), colour, kFlightWidth);
            }
            if (!flight.landed) {
                if (!flight.points.empty()) {
                    painter.draw_label(flight.points.back(), colour,
                                       std::string(speed_name(i)) + ": no landing in 6 s");
                }
                continue;
            }
            painter.perpendicular(flight.landing, kLandingMarkSize, colour, kFlightWidth);
            const V3 gap = flight.landing - lip.point;
            const float across = std::sqrt((gap.x * gap.x) + (gap.z * gap.z));
            std::string text = std::string(speed_name(i)) + ": " + span_to(across);
            std::array<char, kDropTextSize> drop{};
            text::format_to(drop, ", %.0f %s", std::fabs(gap.y), gap.y < 0.0F ? "lower" : "higher");
            text += drop.data();
            if (std::fabs(flight.normal.y) < kWallNormalY) {
                text += " - hits a wall";
            }
            if ((core::at(arcs.landed_on_, i) != 0U) &&
                std::ranges::find(sectors, core::at(arcs.landed_on_, i)) == sectors.end() &&
                (scenery::sector_geometry(core::at(arcs.landed_on_, i)) != nullptr)) {
                text += " on " + object_edit::caption(core::at(arcs.landed_on_, i));
            }
            painter.draw_label(flight.landing, colour, text, kLandingLabelOffsetX,
                               -kLandingLabelRise - (kLandingLabelRowHeight * static_cast<float>(i)));
        }
    }
}

std::string describe()
{
    if (!cache().valid) {
        return "";
    }
    std::array<char, kLineTextSize> text{};
    if (cache().arcs.empty()) {
        return "Jump Arcs: no lip found";
    }
    const LipArcs& first = cache().arcs.front();
    if (first.lip.vert) {
        text::format_to(text, "jump arcs: %d lips; vert - rises %.0f at top speed",
                        static_cast<int>(cache().arcs.size()), core::at(first.rise, 1));
    } else if (core::at(first.flights, 1).landed) {
        text::format_to(text, "jump arcs: %d lips; lands %.0f out at top speed", static_cast<int>(cache().arcs.size()),
                        std::sqrt(((core::at(first.flights, 1).landing.x - first.lip.point.x) *
                                   (core::at(first.flights, 1).landing.x - first.lip.point.x)) +
                                  ((core::at(first.flights, 1).landing.z - first.lip.point.z) *
                                   (core::at(first.flights, 1).landing.z - first.lip.point.z))));
    } else {
        text::format_to(text, "jump arcs: %d lips", static_cast<int>(cache().arcs.size()));
    }
    return text.data();
}

} // namespace frontend::level_editor::arcs
