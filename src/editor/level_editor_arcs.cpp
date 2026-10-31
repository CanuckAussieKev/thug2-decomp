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
#include <vector>

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

namespace frontend::level_editor::arcs {

// --- the pieces -------------------------------------------------------------------------------------------------

namespace {

constexpr V3 kUp = {0.0f, 1.0f, 0.0f};

float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }

bool eligible(uint16_t flags)
{
    if (flags & (0x10 | 0x40 | 0x04)) // (not collidable, a trigger, wallride)
        return false;
    if ((flags & 0x02) && !(flags & (0x01 | 0x08))) // (not skatable)
        return false;
    return true;
}

struct GroupSet {
    V3 heading = {}; // (area-weighted sum, then unit)
    float area = 0.0f;
    std::vector<int> faces;
};

} // namespace

std::vector<Lip> find_lips(const std::vector<Face>& faces, int max_lips)
{
    std::vector<Lip> lips;
    if (max_lips <= 0)
        return lips;
    // The faces' raw normals and areas; the piece's winding convention from its sloped area.
    struct DetailsOf {
        V3 n;
        float area;
        bool use;
    };
    std::vector<DetailsOf> info(faces.size());
    float up_area = 0.0f, down_area = 0.0f;
    for (size_t i = 0; i < faces.size(); ++i) {
        const Face& f = faces[i];
        V3 n = perpendicular(f.b - f.a, f.c - f.a);
        float length = len_of(n);
        info[i] = {length > 1e-6f ? n * (1.0f / length) : V3{}, length * 0.5f, length > 1e-6f && eligible(f.flags)};
        if (!info[i].use)
            continue;
        float y = info[i].n.y;
        if (std::fabs(y) > kSteepNormalY && std::fabs(y) < kFlatNormalY)
            (y > 0.0f ? up_area : down_area) += info[i].area;
    }
    float convention = down_area > up_area ? -1.0f : 1.0f;
    // The sloped faces in groups by their uphill heading (largest first).
    std::vector<int> order;
    for (size_t i = 0; i < faces.size(); ++i)
        if (info[i].use)
            order.push_back(static_cast<int>(i));
    std::sort(order.begin(), order.end(), [&](int a, int b) { return info[a].area > info[b].area; });
    std::vector<GroupSet> groups;
    auto heading_of = [](V3 n) { return make_unit({-n.x, 0.0f, -n.z}); };
    for (int i : order) {
        V3 n = info[i].n * convention;
        if (!(n.y > kSteepNormalY && n.y < kFlatNormalY))
            continue;
        V3 h = heading_of(n);
        GroupSet* into = nullptr;
        for (GroupSet& g : groups) {
            if (get_dot(make_unit(g.heading), h) > kHeadingTolerance) {
                into = &g;
                break;
            }
        }
        if (!into) {
            groups.push_back({});
            into = &groups.back();
        }
        into->heading = into->heading + h * info[i].area;
        into->area += info[i].area;
        into->faces.push_back(i);
    }
    for (GroupSet& g : groups)
        g.heading = make_unit(g.heading);
    // Near-vertical faces join the group whose front they face.
    for (int i : order) {
        V3 n = info[i].n * convention;
        if (std::fabs(n.y) > kSteepNormalY)
            continue;
        V3 h = heading_of(n);
        for (GroupSet& g : groups) {
            if (get_dot(g.heading, h) > kHeadingTolerance) {
                g.faces.push_back(i);
                g.area += info[i].area;
                break;
            }
        }
    }
    std::sort(groups.begin(), groups.end(), [](const GroupSet& a, const GroupSet& b) { return a.area > b.area; });

    for (const GroupSet& g : groups) {
        if (static_cast<int>(lips.size()) >= max_lips)
            break;
        float top = -1e30f, bottom = 1e30f;
        for (int i : g.faces) {
            for (V3 p : {faces[i].a, faces[i].b, faces[i].c}) {
                top = std::max(top, p.y);
                bottom = std::min(bottom, p.y);
            }
        }
        if (top - bottom < kMinLipHeight)
            continue;
        Lip lip;
        lip.heading = g.heading;
        lip.across = {g.heading.z, 0.0f, -g.heading.x};
        lip.height = top - bottom;
        // The top faces' tangent (by area) and the edge's vertices.
        constexpr float kTop = 1.5f;
        V3 tangent = {};
        float min = 1e30f, max = -1e30f, u_sum = 0.0f;
        int count = 0;
        bool vert_flag = false;
        for (int i : g.faces) {
            const Face& f = faces[i];
            bool at_top = false;
            for (V3 p : {f.a, f.b, f.c}) {
                if (p.y < top - kTop)
                    continue;
                at_top = true;
                float s = get_dot(p, lip.across), u = get_dot(p, g.heading);
                min = std::min(min, s);
                max = std::max(max, s);
                u_sum += u;
                ++count;
            }
            if (!at_top)
                continue;
            V3 n = info[i].n * convention;
            V3 t = make_unit(kUp - n * n.y);
            tangent = tangent + t * info[i].area;
            vert_flag |= (f.flags & 0x08) != 0;
        }
        if (count == 0 || get_dot(tangent, tangent) < 1e-12f)
            continue;
        lip.tangent = make_unit(tangent);
        // (a tangent pointing back over the top - an overhang's - launches as vert)
        lip.vert = vert_flag || lip.tangent.y > kVertTangentY || get_dot(lip.tangent, g.heading) < 0.0f;
        float s = (min + max) * 0.5f, u = u_sum / static_cast<float>(count);
        lip.width = max - min;
        // (across and heading are orthonormal and horizontal: the point's x / z from its two coordinates)
        V3 flat = lip.across * s + g.heading * u;
        lip.point = {flat.x, top, flat.z};
        lips.push_back(lip);
    }
    return lips;
}

float scaled_stat(float low, float high, float stat, bool has_limit, float limit)
{
    float result = static_cast<float>((static_cast<double>(high) - low) * stat * 0.1f + low);
    if (!has_limit)
        return result;
    if (low < high)
        return result > limit ? limit : result;
    return result < limit ? limit : result;
}

Physics arcs_fallback()
{
    Physics p;
    p.air_gravity_ = -1350.0f / 1.0f;
    p.vert_gravity_ = -1350.0f / 1.1f;
    p.jump = scaled_stat(414.0f, 450.0f, 5.0f, false, 0.0f);
    p.speeds[0] = scaled_stat(532.0f, 675.0f, 5.0f, true, 900.0f);
    p.speeds[1] = scaled_stat(757.0f, 900.0f, 5.0f, false, 0.0f);
    p.speeds[2] = scaled_stat(957.0f, 1100.0f, 5.0f, false, 0.0f);
    return p;
}

const char* speed_name(int index)
{
    static const char* const kNames[kSpeeds] = {"push", "top", "max"};
    return index >= 0 && index < kSpeeds ? kNames[index] : "?";
}

uint32_t speed_colour(int index)
{
    static const uint32_t kColours[kSpeeds] = {0xFF50E070, 0xFFFFD040, 0xFFFF5040};
    return index >= 0 && index < kSpeeds ? kColours[index] : 0xFFFFFFFF;
}

V3 launch_velocity(const Lip& lip, float speed, float jump)
{
    V3 v = lip.tangent * speed;
    if (v.y < 0.0f)
        v.y = 0.0f;
    v.y += jump;
    return v;
}

Flight get_fly(V3 start, V3 velocity, float gravity, float dt, float max_time, float skip, const Cast& cast)
{
    Flight flight;
    flight.points.push_back(start);
    flight.apex = start;
    V3 p = start, v = velocity;
    float t = 0.0f;
    while (t < max_time) {
        // (as do_air_physics: position += v t + (0, g, 0) t^2 / 2, then v.y += g t)
        V3 next = p + v * dt + V3{0.0f, 0.5f * gravity * dt * dt, 0.0f};
        v.y += gravity * dt;
        t += dt;
        V3 hit, normal;
        if (t > skip && cast && cast(p, next, &hit, &normal)) {
            flight.points.push_back(hit);
            flight.landed = true;
            flight.landing = hit;
            flight.normal = normal;
            flight.time = t;
            return flight;
        }
        p = next;
        if (p.y > flight.apex.y)
            flight.apex = p;
        flight.points.push_back(p);
    }
    flight.time = t;
    return flight;
}

float rise_height(float vertical_speed, float gravity)
{
    if (vertical_speed <= 0.0f || gravity >= 0.0f)
        return 0.0f;
    return vertical_speed * vertical_speed / (-2.0f * gravity);
}

std::string span_to(float units)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.0f (%.1f ft)", units, units / 12.0f);
    return text;
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

Physics g_physics = arcs_fallback();
bool g_have_read;

// A stat structure's value at `stat` (its pair, its limit), from the physics structure or the global one; false:
// not found.
bool stat_value(script::Struct* physics, uint32_t name, float stat, float* value)
{
    script::Struct* params = nullptr;
    if (!physics || !physics->read_sub_struct(name, &params, 0))
        params = script::read_sub_struct(name);
    if (!params)
        return false;
    script::Pair pair;
    pair.prepare_all();
    if (!params->fetch_pair(0u, &pair, false))
        return false;
    float limit = 0.0f;
    bool has_limit = params->fetch_float(kLimit, &limit, 0);
    *value = scaled_stat(pair.x, pair.y, stat, has_limit, limit);
    return true;
}

void read_physics()
{
    Physics p = arcs_fallback();
    auto* skater = static_cast<objects::Rider*>(objects::find_object(kSkater));
    script::Struct* physics = objects::get_physics_structure(skater);
    if (!physics) {
        g_physics = p;
        return;
    }
    float default_stat = script::fetch_float(kDefaultStats);
    p.speed_stat_ = skater ? skater->fetch_tally(kStatSpeed) : default_stat;
    p.ollie_stat_ = skater ? skater->fetch_tally(kStatOllie) : default_stat;
    float gravity = objects::read_tuning_float(kFallPull, 0, skater);
    float air_hang = script::fetch_float(kAirHang), vert_hang = script::fetch_float(kVertHang);
    if (gravity < 0.0f) {
        p.air_gravity_ = air_hang > 0.0f ? gravity / air_hang : gravity;
        p.vert_gravity_ = vert_hang > 0.0f ? gravity / vert_hang : gravity;
    }
    stat_value(physics, kJumpSpeed, p.ollie_stat_, &p.jump);
    stat_value(physics, kKickSpeed, p.speed_stat_, &p.speeds[0]);
    stat_value(physics, kMaxSpeed, p.speed_stat_, &p.speeds[1]);
    stat_value(physics, kSpeedCapStat, p.speed_stat_, &p.speeds[2]);
    char source[96];
    std::snprintf(source, sizeof(source), "%s, speed stat %.1f, ollie stat %.1f",
                  skater ? "the skater's physics" : "skater_physics", p.speed_stat_, p.ollie_stat_);
    p.source = source;
    g_physics = p;
}

// A sector's collision faces in world space (none: no collision, or it's switched off).
void sector_faces(uint32_t checksum, std::vector<Face>* out)
{
    uint8_t* sector = scenery::sector_of(checksum);
    if (!sector)
        return;
    uint8_t* coll = *reinterpret_cast<uint8_t**>(sector + 0x18);
    if (!render::is_static_collider(coll) || (coll[0x08] & 4))
        return;
    const auto* tri = *reinterpret_cast<const render::TriMeshSource* const*>(coll + 0x0C);
    if (!tri || !tri->faces_ || !tri->vertices_)
        return;
    for (int f = 0; f < tri->tri_count_ && out->size() < 20000; ++f) {
        V3 p[3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            uint32_t index = tri->face_vertex(f, k);
            if (index >= tri->num_vertices_) {
                ok = false;
                break;
            }
            maths::Vec4 v = tri->read_stored_vertex(static_cast<int>(index));
            p[k] = {v.x, v.y, v.z};
        }
        if (!ok)
            continue;
        uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->faces_ + f * (tri->small_faces_ ? 8 : 10));
        out->push_back({p[0], p[1], p[2], flags});
    }
}

// The game's collision ray (a ProbeRay: constructor 0x00417760, cast 0x00417710 - static collision only, ignoring
// faces flagged 0x10 as every feeler does; the hit's normal at +0x24, its point at +0x34, the collision object hit
// at +0x54 and its checksum at +0x58).
bool cast_feeler(V3 from, V3 to, V3* hit, V3* normal, uint32_t* sector)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<render::ProbeRay*>(feeler);
    ray->make_new();
    maths::Vec4 start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const float* p = &ray->hit_.point.x;
    const float* n = &ray->hit_.normal.x;
    *hit = {p[0], p[1], p[2]};
    *normal = {n[0], n[1], n[2]};
    *sector = ray->hit_.object ? ray->hit_.object_checksum_ : 0;
    return true;
}

struct LipArcs {
    Lip lip;
    Flight flights[kSpeeds];
    uint32_t landed_on_[kSpeeds] = {};
    float rise[kSpeeds] = {};
};
struct Cache {
    std::vector<uint32_t> sectors;
    std::vector<V3> boxes; // (each sector's box min, max: moved since?)
    std::vector<LipArcs> arcs;
    std::chrono::steady_clock::time_point made;
    bool valid = false;
};
Cache g_cache;

std::vector<V3> boxes_of(const std::vector<uint32_t>& sectors)
{
    std::vector<V3> boxes;
    for (uint32_t s : sectors) {
        OrientedBox box;
        if (scenery::cuboid(s, &box)) {
            V3 min, max;
            bounds_of(box, &min, &max);
            boxes.push_back(min);
            boxes.push_back(max);
        }
    }
    return boxes;
}

bool same_boxes(const std::vector<V3>& a, const std::vector<V3>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        V3 d = a[i] - b[i];
        if (get_dot(d, d) > 0.01f)
            return false;
    }
    return true;
}

void get_remake(const std::vector<uint32_t>& sectors, const std::vector<V3>& boxes)
{
    g_cache = Cache{};
    g_cache.sectors = sectors;
    g_cache.boxes = boxes;
    g_cache.made = std::chrono::steady_clock::now();
    g_cache.valid = true;
    read_physics();
    g_have_read = true;
    std::vector<Face> faces;
    for (uint32_t s : sectors)
        sector_faces(s, &faces);
    for (const Lip& lip : find_lips(faces, 4)) {
        LipArcs arcs;
        arcs.lip = lip;
        for (int i = 0; i < kSpeeds; ++i) {
            V3 v = launch_velocity(lip, g_physics.speeds[i], g_physics.jump);
            if (lip.vert) {
                arcs.rise[i] = rise_height(v.y, g_physics.vert_gravity_);
                continue;
            }
            uint32_t* landed_on = &arcs.landed_on_[i];
            // (from just off the lip: 2 units up, so the lip's own edge isn't the first thing met)
            arcs.flights[i] = get_fly(lip.point + V3{0.0f, 2.0f, 0.0f}, v, g_physics.air_gravity_, 1.0f / 60.0f, 6.0f,
                                      1.0f / 60.0f, [landed_on](V3 from, V3 to, V3* hit, V3* normal) {
                                          return cast_feeler(from, to, hit, normal, landed_on);
                                      });
        }
        g_cache.arcs.push_back(arcs);
    }
    if (debug_enabled())
        get_log("level editor: jump arcs - %d lips (%s; g %.0f / vert %.0f, jump %.0f, speeds %.0f / %.0f / %.0f)",
                static_cast<int>(g_cache.arcs.size()), g_physics.source.c_str(), g_physics.air_gravity_,
                g_physics.vert_gravity_, g_physics.jump, g_physics.speeds[0], g_physics.speeds[1], g_physics.speeds[2]);
}

} // namespace

const Physics& current_physics(bool refresh)
{
    if (refresh || !g_have_read) {
        read_physics();
        g_have_read = true;
    }
    return g_physics;
}

void add_overlay(const overlays::Painter& painter, const std::vector<uint32_t>& sectors)
{
    if (!overlays::get_shown(overlays::kJumpArcs) || sectors.empty()) {
        g_cache.valid = false;
        return;
    }
    std::vector<V3> boxes = boxes_of(sectors);
    bool stale = !g_cache.valid || g_cache.sectors != sectors || !same_boxes(g_cache.boxes, boxes);
    // (while the selection moves: made again at most four times a second)
    if (stale && (!g_cache.valid || g_cache.sectors != sectors ||
                  std::chrono::steady_clock::now() - g_cache.made > std::chrono::milliseconds(250)))
        get_remake(sectors, boxes);
    for (const LipArcs& arcs : g_cache.arcs) {
        const Lip& lip = arcs.lip;
        V3 half = lip.across * (lip.width * 0.5f);
        painter.get_seg(lip.point - half, lip.point + half, 0xFFFFFFFF, 2.0f);
        painter.get_seg(lip.point, lip.point + lip.tangent * 40.0f, 0xC0FFFFFF, 1.5f);
        char label[96];
        std::snprintf(label, sizeof(label), "lip %.0f high, %.0f deg%s", lip.height,
                      std::asin(std::clamp(lip.tangent.y, -1.0f, 1.0f)) * 57.29578f, lip.vert ? " (vert)" : "");
        painter.text_tools(lip.point, 0xFFFFFFFF, label, 6.0f, 6.0f);
        for (int i = 0; i < kSpeeds; ++i) {
            uint32_t colour = speed_colour(i);
            if (lip.vert) {
                // (straight up and back down: how high)
                V3 side = lip.across * (lip.width * (0.15f * static_cast<float>(i - 1)));
                V3 base = lip.point + side, apex = base + V3{0.0f, arcs.rise[i], 0.0f};
                painter.get_seg(base, apex, colour, 2.0f);
                painter.get_seg(apex - lip.across * 12.0f, apex + lip.across * 12.0f, colour, 2.0f);
                painter.text_tools(apex, colour, std::string(speed_name(i)) + ": rises " + span_to(arcs.rise[i]));
                continue;
            }
            const Flight& flight = arcs.flights[i];
            for (size_t k = 1; k < flight.points.size(); ++k)
                painter.get_seg(flight.points[k - 1], flight.points[k], colour, 2.0f);
            if (!flight.landed) {
                if (!flight.points.empty())
                    painter.text_tools(flight.points.back(), colour,
                                       std::string(speed_name(i)) + ": no landing in 6 s");
                continue;
            }
            painter.perpendicular(flight.landing, 14.0f, colour, 2.0f);
            V3 gap = flight.landing - lip.point;
            float across = std::sqrt(gap.x * gap.x + gap.z * gap.z);
            std::string text = std::string(speed_name(i)) + ": " + span_to(across);
            char drop[48];
            std::snprintf(drop, sizeof(drop), gap.y < 0.0f ? ", %.0f lower" : ", %.0f higher", std::fabs(gap.y));
            text += drop;
            if (std::fabs(flight.normal.y) < 0.5f)
                text += " - hits a wall";
            if (arcs.landed_on_[i] && std::find(sectors.begin(), sectors.end(), arcs.landed_on_[i]) == sectors.end() &&
                scenery::sector_geometry(arcs.landed_on_[i]))
                text += " on " + object_edit::caption(arcs.landed_on_[i]);
            painter.text_tools(flight.landing, colour, text, 8.0f, -10.0f - 14.0f * static_cast<float>(i));
        }
    }
}

std::string describe()
{
    if (!g_cache.valid)
        return "";
    char text[160];
    if (g_cache.arcs.empty())
        return "Jump Arcs: no lip found";
    const LipArcs& first = g_cache.arcs.front();
    if (first.lip.vert)
        std::snprintf(text, sizeof(text), "jump arcs: %d lips; vert - rises %.0f at top speed",
                      static_cast<int>(g_cache.arcs.size()), first.rise[1]);
    else if (first.flights[1].landed)
        std::snprintf(
            text, sizeof(text), "jump arcs: %d lips; lands %.0f out at top speed",
            static_cast<int>(g_cache.arcs.size()),
            std::sqrt(
                (first.flights[1].landing.x - first.lip.point.x) * (first.flights[1].landing.x - first.lip.point.x) +
                (first.flights[1].landing.z - first.lip.point.z) * (first.flights[1].landing.z - first.lip.point.z)));
    else
        std::snprintf(text, sizeof(text), "jump arcs: %d lips", static_cast<int>(g_cache.arcs.size()));
    return text;
}

} // namespace frontend::level_editor::arcs
