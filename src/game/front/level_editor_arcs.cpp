// NOT ORIGINAL - the level editor's jump arcs (ours). See level_editor_arcs.h.

#include "game/front/level_editor_arcs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_scenery.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "game/sys/log.h"
#include "game/core/game_vtables.h"

namespace Front::LevelEditor::Arcs {

// --- the pieces -------------------------------------------------------------------------------------------------

namespace {

constexpr V3 kUp = {0.0f, 1.0f, 0.0f};

float Length(V3 v) { return std::sqrt(Dot(v, v)); }

bool Eligible(uint16_t flags)
{
    if (flags & (0x10 | 0x40 | 0x04)) // (not collidable, a trigger, wallride)
        return false;
    if ((flags & 0x02) && !(flags & (0x01 | 0x08))) // (not skatable)
        return false;
    return true;
}

struct Group {
    V3 heading = {};     // (area-weighted sum, then unit)
    float area = 0.0f;
    std::vector<int> faces;
};

} // namespace

std::vector<Lip> FindLips(const std::vector<Face>& faces, int maxLips)
{
    std::vector<Lip> lips;
    if (maxLips <= 0)
        return lips;
    // The faces' raw normals and areas; the piece's winding convention from its sloped area.
    struct Info {
        V3 n;
        float area;
        bool use;
    };
    std::vector<Info> info(faces.size());
    float upArea = 0.0f, downArea = 0.0f;
    for (size_t i = 0; i < faces.size(); ++i) {
        const Face& f = faces[i];
        V3 n = Cross(f.b - f.a, f.c - f.a);
        float length = Length(n);
        info[i] = {length > 1e-6f ? n * (1.0f / length) : V3{}, length * 0.5f, length > 1e-6f && Eligible(f.flags)};
        if (!info[i].use)
            continue;
        float y = info[i].n.y;
        if (std::fabs(y) > kSteepNormalY && std::fabs(y) < kFlatNormalY)
            (y > 0.0f ? upArea : downArea) += info[i].area;
    }
    float convention = downArea > upArea ? -1.0f : 1.0f;
    // The sloped faces in groups by their uphill heading (largest first).
    std::vector<int> order;
    for (size_t i = 0; i < faces.size(); ++i)
        if (info[i].use)
            order.push_back(static_cast<int>(i));
    std::sort(order.begin(), order.end(), [&](int a, int b) { return info[a].area > info[b].area; });
    std::vector<Group> groups;
    auto headingOf = [](V3 n) { return Normalize({-n.x, 0.0f, -n.z}); };
    for (int i : order) {
        V3 n = info[i].n * convention;
        if (!(n.y > kSteepNormalY && n.y < kFlatNormalY))
            continue;
        V3 h = headingOf(n);
        Group* into = nullptr;
        for (Group& g : groups) {
            if (Dot(Normalize(g.heading), h) > kHeadingTolerance) {
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
    for (Group& g : groups)
        g.heading = Normalize(g.heading);
    // Near-vertical faces join the group whose front they face.
    for (int i : order) {
        V3 n = info[i].n * convention;
        if (std::fabs(n.y) > kSteepNormalY)
            continue;
        V3 h = headingOf(n);
        for (Group& g : groups) {
            if (Dot(g.heading, h) > kHeadingTolerance) {
                g.faces.push_back(i);
                g.area += info[i].area;
                break;
            }
        }
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.area > b.area; });

    for (const Group& g : groups) {
        if (static_cast<int>(lips.size()) >= maxLips)
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
        float sMin = 1e30f, sMax = -1e30f, uSum = 0.0f;
        int count = 0;
        bool vertFlag = false;
        for (int i : g.faces) {
            const Face& f = faces[i];
            bool atTop = false;
            for (V3 p : {f.a, f.b, f.c}) {
                if (p.y < top - kTop)
                    continue;
                atTop = true;
                float s = Dot(p, lip.across), u = Dot(p, g.heading);
                sMin = std::min(sMin, s);
                sMax = std::max(sMax, s);
                uSum += u;
                ++count;
            }
            if (!atTop)
                continue;
            V3 n = info[i].n * convention;
            V3 t = Normalize(kUp - n * n.y);
            tangent = tangent + t * info[i].area;
            vertFlag |= (f.flags & 0x08) != 0;
        }
        if (count == 0 || Dot(tangent, tangent) < 1e-12f)
            continue;
        lip.tangent = Normalize(tangent);
        // (a tangent pointing back over the top - an overhang's - launches as vert)
        lip.vert = vertFlag || lip.tangent.y > kVertTangentY || Dot(lip.tangent, g.heading) < 0.0f;
        float s = (sMin + sMax) * 0.5f, u = uSum / static_cast<float>(count);
        lip.width = sMax - sMin;
        // (across and heading are orthonormal and horizontal: the point's x / z from its two coordinates)
        V3 flat = lip.across * s + g.heading * u;
        lip.point = {flat.x, top, flat.z};
        lips.push_back(lip);
    }
    return lips;
}

float ScaledStat(float low, float high, float stat, bool hasLimit, float limit)
{
    float result = static_cast<float>((static_cast<double>(high) - low) * stat * 0.1f + low);
    if (!hasLimit)
        return result;
    if (low < high)
        return result > limit ? limit : result;
    return result < limit ? limit : result;
}

Physics ArcsFallback()
{
    Physics p;
    p.airGravity = -1350.0f / 1.0f;
    p.vertGravity = -1350.0f / 1.1f;
    p.jump = ScaledStat(414.0f, 450.0f, 5.0f, false, 0.0f);
    p.speeds[0] = ScaledStat(532.0f, 675.0f, 5.0f, true, 900.0f);
    p.speeds[1] = ScaledStat(757.0f, 900.0f, 5.0f, false, 0.0f);
    p.speeds[2] = ScaledStat(957.0f, 1100.0f, 5.0f, false, 0.0f);
    return p;
}

const char* SpeedName(int index)
{
    static const char* const kNames[kSpeeds] = {"push", "top", "max"};
    return index >= 0 && index < kSpeeds ? kNames[index] : "?";
}

uint32_t SpeedColour(int index)
{
    static const uint32_t kColours[kSpeeds] = {0xFF50E070, 0xFFFFD040, 0xFFFF5040};
    return index >= 0 && index < kSpeeds ? kColours[index] : 0xFFFFFFFF;
}

V3 LaunchVelocity(const Lip& lip, float speed, float jump)
{
    V3 v = lip.tangent * speed;
    if (v.y < 0.0f)
        v.y = 0.0f;
    v.y += jump;
    return v;
}

Flight Fly(V3 start, V3 velocity, float gravity, float dt, float maxTime, float skip, const Cast& cast)
{
    Flight flight;
    flight.points.push_back(start);
    flight.apex = start;
    V3 p = start, v = velocity;
    float t = 0.0f;
    while (t < maxTime) {
        // (as DoAirPhysics: position += v t + (0, g, 0) t^2 / 2, then v.y += g t)
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

float RiseHeight(float verticalSpeed, float gravity)
{
    if (verticalSpeed <= 0.0f || gravity >= 0.0f)
        return 0.0f;
    return verticalSpeed * verticalSpeed / (-2.0f * gravity);
}

std::string Distance(float units)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.0f (%.1f ft)", units, units / 12.0f);
    return text;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSkater = 0x5B8AB877;              // Skater (the local skater's alias)
constexpr uint32_t kAirGravity = 0xFAA40754;          // Physics_Air_Gravity
constexpr uint32_t kAirHang = 0xC31CA696;             // Physics_Air_hang_Stat
constexpr uint32_t kVertHang = 0x441C38A0;            // Physics_Vert_hang_Stat
constexpr uint32_t kJumpSpeed = 0x02ADE3AD;           // Physics_Jump_Speed_Stat
constexpr uint32_t kKickSpeed = 0x92E0247C;           // Skater_Max_Crouched_Kick_Speed_Stat
constexpr uint32_t kMaxSpeed = 0x2EACDDB3;            // Skater_Max_Speed_Stat
constexpr uint32_t kMaxMaxSpeed = 0xCC5F87AA;         // Skater_Max_Max_Speed_Stat
constexpr uint32_t kDefaultStats = 0x6357D57C;        // Skater_Default_Stats
constexpr uint32_t kLimit = 0x8069179F;               // limit
void** const& kCollStaticVtable = Vt::kCollStatic;    // (CCollStatic)
constexpr int kStatOllie = 2, kStatSpeed = 3;         // (STATS_OLLIE, STATS_SPEED)

Physics g_physics = ArcsFallback();
bool g_haveRead;

// A stat structure's value at `stat` (its pair, its limit), from the physics structure or the global one; false:
// not found.
bool StatValue(Script::CStruct* physics, uint32_t name, float stat, float* value)
{
    Script::CStruct* params = nullptr;
    if (!physics || !physics->GetStructure(name, &params, 0))
        params = Script::GetStructure(name);
    if (!params)
        return false;
    Script::CPair pair;
    pair.Init();
    if (!params->GetPair(0u, &pair, false))
        return false;
    float limit = 0.0f;
    bool hasLimit = params->GetFloat(kLimit, &limit, 0);
    *value = ScaledStat(pair.x, pair.y, stat, hasLimit, limit);
    return true;
}

void ReadPhysics()
{
    Physics p = ArcsFallback();
    auto* skater = static_cast<Obj::CSkater*>(Obj::FindObject(kSkater));
    Script::CStruct* physics = Obj::GetPhysicsStructure(skater);
    if (!physics) {
        g_physics = p;
        return;
    }
    float defaultStat = Script::GetFloat(kDefaultStats);
    p.speedStat = skater ? skater->GetStat(kStatSpeed) : defaultStat;
    p.ollieStat = skater ? skater->GetStat(kStatOllie) : defaultStat;
    float gravity = Obj::GetPhysicsFloat(kAirGravity, 0, skater);
    float airHang = Script::GetFloat(kAirHang), vertHang = Script::GetFloat(kVertHang);
    if (gravity < 0.0f) {
        p.airGravity = airHang > 0.0f ? gravity / airHang : gravity;
        p.vertGravity = vertHang > 0.0f ? gravity / vertHang : gravity;
    }
    StatValue(physics, kJumpSpeed, p.ollieStat, &p.jump);
    StatValue(physics, kKickSpeed, p.speedStat, &p.speeds[0]);
    StatValue(physics, kMaxSpeed, p.speedStat, &p.speeds[1]);
    StatValue(physics, kMaxMaxSpeed, p.speedStat, &p.speeds[2]);
    char source[96];
    std::snprintf(source, sizeof(source), "%s, speed stat %.1f, ollie stat %.1f", skater ? "the skater's physics" : "skater_physics",
                  p.speedStat, p.ollieStat);
    p.source = source;
    g_physics = p;
}

// A sector's collision faces in world space (none: no collision, or it's switched off).
void SectorFaces(uint32_t checksum, std::vector<Face>* out)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    if (!sector)
        return;
    uint8_t* coll = *reinterpret_cast<uint8_t**>(sector + 0x18);
    if (!coll || *reinterpret_cast<void***>(coll) != kCollStaticVtable || (coll[0x08] & 4))
        return;
    const auto* tri = *reinterpret_cast<const Nx::CCollObjTriData* const*>(coll + 0x0C);
    if (!tri || !tri->m_faces || !tri->m_vertices)
        return;
    for (int f = 0; f < tri->m_numFaces && out->size() < 20000; ++f) {
        V3 p[3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            uint32_t index = tri->FaceVertex(f, k);
            if (index >= tri->m_numVertices) {
                ok = false;
                break;
            }
            Mth::Vector v = tri->GetRawVertexPos(static_cast<int>(index));
            p[k] = {v.x, v.y, v.z};
        }
        if (!ok)
            continue;
        uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10));
        out->push_back({p[0], p[1], p[2], flags});
    }
}

// The game's collision ray (a CFeeler: constructor 0x00417760, cast 0x00417710 - static collision only, ignoring
// faces flagged 0x10 as every feeler does; the hit's normal at +0x24, its point at +0x34, the collision object hit
// at +0x54 and its checksum at +0x58).
bool CastFeeler(V3 from, V3 to, V3* hit, V3* normal, uint32_t* sector)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Nx::CFeeler*>(feeler);
    ray->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return false;
    const float* p = reinterpret_cast<const float*>(feeler + 0x34);
    const float* n = reinterpret_cast<const float*>(feeler + 0x24);
    *hit = {p[0], p[1], p[2]};
    *normal = {n[0], n[1], n[2]};
    *sector = *reinterpret_cast<void* const*>(feeler + 0x54) ? *reinterpret_cast<const uint32_t*>(feeler + 0x58) : 0;
    return true;
}

struct LipArcs {
    Lip lip;
    Flight flights[kSpeeds];
    uint32_t landedOn[kSpeeds] = {};
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

std::vector<V3> BoxesOf(const std::vector<uint32_t>& sectors)
{
    std::vector<V3> boxes;
    for (uint32_t s : sectors) {
        OrientedBox box;
        if (Scenery::Box(s, &box)) {
            V3 min, max;
            BoundsOf(box, &min, &max);
            boxes.push_back(min);
            boxes.push_back(max);
        }
    }
    return boxes;
}

bool SameBoxes(const std::vector<V3>& a, const std::vector<V3>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        V3 d = a[i] - b[i];
        if (Dot(d, d) > 0.01f)
            return false;
    }
    return true;
}

void Remake(const std::vector<uint32_t>& sectors, const std::vector<V3>& boxes)
{
    g_cache = Cache{};
    g_cache.sectors = sectors;
    g_cache.boxes = boxes;
    g_cache.made = std::chrono::steady_clock::now();
    g_cache.valid = true;
    ReadPhysics();
    g_haveRead = true;
    std::vector<Face> faces;
    for (uint32_t s : sectors)
        SectorFaces(s, &faces);
    for (const Lip& lip : FindLips(faces, 4)) {
        LipArcs arcs;
        arcs.lip = lip;
        for (int i = 0; i < kSpeeds; ++i) {
            V3 v = LaunchVelocity(lip, g_physics.speeds[i], g_physics.jump);
            if (lip.vert) {
                arcs.rise[i] = RiseHeight(v.y, g_physics.vertGravity);
                continue;
            }
            uint32_t* landedOn = &arcs.landedOn[i];
            // (from just off the lip: 2 units up, so the lip's own edge isn't the first thing met)
            arcs.flights[i] = Fly(lip.point + V3{0.0f, 2.0f, 0.0f}, v, g_physics.airGravity, 1.0f / 60.0f, 6.0f,
                                  1.0f / 60.0f, [landedOn](V3 from, V3 to, V3* hit, V3* normal) {
                                      return CastFeeler(from, to, hit, normal, landedOn);
                                  });
        }
        g_cache.arcs.push_back(arcs);
    }
    if (DebugEnabled())
        Log("level editor: jump arcs - %d lips (%s; g %.0f / vert %.0f, jump %.0f, speeds %.0f / %.0f / %.0f)",
            static_cast<int>(g_cache.arcs.size()), g_physics.source.c_str(), g_physics.airGravity,
            g_physics.vertGravity, g_physics.jump, g_physics.speeds[0], g_physics.speeds[1], g_physics.speeds[2]);
}

} // namespace

const Physics& CurrentPhysics(bool refresh)
{
    if (refresh || !g_haveRead) {
        ReadPhysics();
        g_haveRead = true;
    }
    return g_physics;
}

void AddOverlay(const Overlays::Painter& painter, const std::vector<uint32_t>& sectors)
{
    if (!Overlays::Shown(Overlays::kJumpArcs) || sectors.empty()) {
        g_cache.valid = false;
        return;
    }
    std::vector<V3> boxes = BoxesOf(sectors);
    bool stale = !g_cache.valid || g_cache.sectors != sectors || !SameBoxes(g_cache.boxes, boxes);
    // (while the selection moves: made again at most four times a second)
    if (stale && (!g_cache.valid || g_cache.sectors != sectors ||
                  std::chrono::steady_clock::now() - g_cache.made > std::chrono::milliseconds(250)))
        Remake(sectors, boxes);
    for (const LipArcs& arcs : g_cache.arcs) {
        const Lip& lip = arcs.lip;
        V3 half = lip.across * (lip.width * 0.5f);
        painter.Line(lip.point - half, lip.point + half, 0xFFFFFFFF, 2.0f);
        painter.Line(lip.point, lip.point + lip.tangent * 40.0f, 0xC0FFFFFF, 1.5f);
        char label[96];
        std::snprintf(label, sizeof(label), "lip %.0f high, %.0f deg%s", lip.height,
                      std::asin(std::clamp(lip.tangent.y, -1.0f, 1.0f)) * 57.29578f, lip.vert ? " (vert)" : "");
        painter.Text(lip.point, 0xFFFFFFFF, label, 6.0f, 6.0f);
        for (int i = 0; i < kSpeeds; ++i) {
            uint32_t colour = SpeedColour(i);
            if (lip.vert) {
                // (straight up and back down: how high)
                V3 side = lip.across * (lip.width * (0.15f * static_cast<float>(i - 1)));
                V3 base = lip.point + side, apex = base + V3{0.0f, arcs.rise[i], 0.0f};
                painter.Line(base, apex, colour, 2.0f);
                painter.Line(apex - lip.across * 12.0f, apex + lip.across * 12.0f, colour, 2.0f);
                painter.Text(apex, colour, std::string(SpeedName(i)) + ": rises " + Distance(arcs.rise[i]));
                continue;
            }
            const Flight& flight = arcs.flights[i];
            for (size_t k = 1; k < flight.points.size(); ++k)
                painter.Line(flight.points[k - 1], flight.points[k], colour, 2.0f);
            if (!flight.landed) {
                if (!flight.points.empty())
                    painter.Text(flight.points.back(), colour, std::string(SpeedName(i)) + ": no landing in 6 s");
                continue;
            }
            painter.Cross(flight.landing, 14.0f, colour, 2.0f);
            V3 gap = flight.landing - lip.point;
            float across = std::sqrt(gap.x * gap.x + gap.z * gap.z);
            std::string text = std::string(SpeedName(i)) + ": " + Distance(across);
            char drop[48];
            std::snprintf(drop, sizeof(drop), gap.y < 0.0f ? ", %.0f lower" : ", %.0f higher", std::fabs(gap.y));
            text += drop;
            if (std::fabs(flight.normal.y) < 0.5f)
                text += " - hits a wall";
            if (arcs.landedOn[i] && std::find(sectors.begin(), sectors.end(), arcs.landedOn[i]) == sectors.end() &&
                Scenery::SectorGeometry(arcs.landedOn[i]))
                text += " on " + Objects::Label(arcs.landedOn[i]);
            painter.Text(flight.landing, colour, text, 8.0f, -10.0f - 14.0f * static_cast<float>(i));
        }
    }
}

std::string Describe()
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
        std::snprintf(text, sizeof(text), "jump arcs: %d lips; lands %.0f out at top speed",
                      static_cast<int>(g_cache.arcs.size()),
                      std::sqrt((first.flights[1].landing.x - first.lip.point.x) * (first.flights[1].landing.x - first.lip.point.x) +
                                (first.flights[1].landing.z - first.lip.point.z) * (first.flights[1].landing.z - first.lip.point.z)));
    else
        std::snprintf(text, sizeof(text), "jump arcs: %d lips", static_cast<int>(g_cache.arcs.size()));
    return text;
}

} // namespace Front::LevelEditor::Arcs
