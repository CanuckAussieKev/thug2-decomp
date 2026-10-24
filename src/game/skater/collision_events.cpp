// NOT ORIGINAL - [debug] collision_events: the skater's velocity turning round, logged with its cause (see
// collision_events.h).

#include "game/skater/collision_events.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/sys/mlp.h"
#include "game/sys/screenshot.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Skater::CollisionEvents {

namespace {

using namespace Physics;

constexpr uint32_t kForwardHeight = 0xD4205C9B; // Skater_First_Forward_Collision_Height

constexpr int kHistory = 90;          // frames kept
constexpr int kWindow = 60;           // frames a reversal may take
constexpr float kMinSpeedNow = 5.0f;  // inches a second
constexpr float kMinSpeedBefore = 60.0f;
constexpr float kOpposed = -0.8f;     // the two directions' dot product
constexpr float kSudden = 0.5f;       // one frame's direction change: dot below this (60 degrees)
constexpr float kStill = 1.0f;        // ... or the speed through (almost) zero
constexpr int kMostAirFrames = 20;
constexpr float kTeleport = 300.0f;   // a frame's move past this starts the history again
constexpr float kSameSpot = 40.0f;
constexpr DWORD kSpotQuietMs = 5000;
constexpr int kPathRecords = 32;

struct Sample {
    uint32_t frame;
    int state;
    float pos[3], vel[3], facing[3], groundNormal[3];
    Face ground;
};

struct PathRecord {
    uint32_t frame;
    Path path;
    Face face;
};

void* g_component = nullptr;
Sample g_samples[kHistory];
int g_count = 0, g_next = 0; // (a ring: g_next the slot for the next sample)
PathRecord g_paths[kPathRecords];
int g_pathCount = 0, g_pathNext = 0;
int g_events = 0;

struct Spot {
    float at[3];
    DWORD time;
};
Spot g_spots[32];
int g_spotNext = 0;

void Copy3(float* to, const Mth::Vector& from)
{
    to[0] = from.x;
    to[1] = from.y;
    to[2] = from.z;
}

Face FaceOf(const Nx::CFeeler& feeler)
{
    Face face;
    face.hit = feeler.m_hit.hit;
    if (!face.hit)
        return face;
    face.object = feeler.m_object.mp_object != nullptr;
    face.checksum = face.object ? feeler.m_objectId : feeler.m_hit.objectChecksum;
    face.index = static_cast<int>(feeler.m_hit.face);
    face.flags = feeler.m_hit.faceFlags;
    face.terrain = feeler.m_hit.terrain;
    Copy3(face.point, feeler.m_point);
    Copy3(face.normal, feeler.m_normal);
    return face;
}

const Nx::CFeeler& FeelerAt(void* component, uint32_t offset) { return At<Nx::CFeeler>(component, offset); }

std::string NameOf(uint32_t checksum)
{
    const char* name = Script::KeptQbName(checksum);
    if (!name)
        name = Script::FindChecksumName(checksum);
    char text[16];
    if (!name) {
        std::snprintf(text, sizeof(text), "0x%08X", checksum);
        return text;
    }
    std::string out = name;
    for (char& c : out)
        if (c == ' ' || c == '=' || c == ';')
            c = '_';
    return out;
}

// The level loaded: its load script's name (the skate module's +0x378) without "Load_".
std::string LevelName()
{
    uint8_t* module = GameVariable<uint8_t*>(0x007CE478);
    uint32_t load = module ? At<uint32_t>(module, 0x378) : 0;
    if (!load)
        return "-";
    std::string name = NameOf(load);
    if (name.size() > 5 && _strnicmp(name.c_str(), "Load_", 5) == 0)
        name.erase(0, 5);
    return name;
}

// The local skater (the skate module's 0x00566520; null with none).
void* LocalSkater()
{
    uint8_t* module = GameVariable<uint8_t*>(0x007CE478);
    return module ? GameFunction<void*(__thiscall*)(void*)>(0x00566520)(module) : nullptr;
}

float Horizontal(const float* v, float* direction)
{
    float length = std::sqrt(v[0] * v[0] + v[2] * v[2]);
    direction[0] = length > 0.0f ? v[0] / length : 0.0f;
    direction[1] = length > 0.0f ? v[2] / length : 0.0f;
    return length;
}

float HeadingOf(const float* facing)
{
    return static_cast<float>(std::atan2(static_cast<double>(facing[0]), static_cast<double>(facing[2])) * 180.0 /
                              3.14159265358979);
}

const Sample& SampleAt(int age) // 0: the newest
{
    return g_samples[((g_next - 1 - age) % kHistory + kHistory) % kHistory];
}

void Reset()
{
    g_count = g_next = 0;
    g_pathCount = g_pathNext = 0;
}

// A probe line: as the skater's feelers see (faces flagged 0x10 skipped), or every face.
Face Probe(const float* from, const float* to, bool everything)
{
    alignas(16) uint8_t storage[sizeof(Nx::CFeeler)];
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(storage);
    Mth::Vector start = {from[0], from[1], from[2], 1.0f};
    Mth::Vector end = {to[0], to[1], to[2], 1.0f};
    feeler->Construct(&start, &end);
    feeler->SetIgnore(everything ? 0 : 0x10, 0);
    feeler->GetCollision(true, false);
    Face face = FaceOf(*feeler);
    feeler->Destruct();
    return face;
}

bool QuietSpot(const float* at)
{
    DWORD now = GetTickCount();
    for (Spot& spot : g_spots) {
        if (!spot.time)
            continue;
        float dx = spot.at[0] - at[0], dy = spot.at[1] - at[1], dz = spot.at[2] - at[2];
        if (dx * dx + dy * dy + dz * dz < kSameSpot * kSameSpot && now - spot.time < kSpotQuietMs)
            return true;
    }
    Spot& spot = g_spots[g_spotNext];
    g_spotNext = (g_spotNext + 1) % static_cast<int>(sizeof(g_spots) / sizeof(g_spots[0]));
    std::memcpy(spot.at, at, sizeof(spot.at));
    spot.time = now ? now : 1;
    return false;
}

void Emit(int before)
{
    const Sample& now = SampleAt(0);
    const Sample& old = SampleAt(before);
    Event event;
    event.frame = now.frame;
    event.frames = static_cast<int>(now.frame - old.frame);
    std::memcpy(event.posBefore, old.pos, sizeof(event.posBefore));
    std::memcpy(event.pos, now.pos, sizeof(event.pos));
    std::memcpy(event.velBefore, old.vel, sizeof(event.velBefore));
    std::memcpy(event.velAfter, now.vel, sizeof(event.velAfter));
    std::memcpy(event.groundNormal, now.groundNormal, sizeof(event.groundNormal));
    event.headingBefore = HeadingOf(old.facing);
    event.heading = HeadingOf(now.facing);

    // The paths that ran between the two (in order), and the strongest.
    const PathRecord* strongest = nullptr;
    bool seen[static_cast<int>(Path::Count)] = {};
    for (int i = 0; i < g_pathCount; ++i) {
        const PathRecord& record = g_paths[(g_pathNext - g_pathCount + i + kPathRecords) % kPathRecords];
        if (record.frame < old.frame || record.frame > now.frame)
            continue;
        int p = static_cast<int>(record.path);
        if (!seen[p]) {
            seen[p] = true;
            if (!event.paths.empty())
                event.paths += ',';
            event.paths += PathName(record.path);
        }
        if (!strongest || record.path <= strongest->path)
            strongest = &record;
    }
    if (event.paths.empty())
        event.paths = "-";

    // The ground at its steepest.
    const Sample* steepest = &now;
    for (int age = 0; age <= before; ++age) {
        const Sample& s = SampleAt(age);
        if (s.state == 0 && s.groundNormal[1] < steepest->groundNormal[1])
            steepest = &s;
        if (s.state == 0 && s.groundNormal[1] < event.groundMinY)
            event.groundMinY = s.groundNormal[1];
    }
    if (strongest) {
        event.reason = PathName(strongest->path);
        event.face = strongest->face;
    } else {
        event.reason = "slope";
        event.face = steepest->ground;
    }
    if (event.face.hit)
        std::memcpy(event.spot, event.face.point, sizeof(event.spot));
    else
        std::memcpy(event.spot, now.pos, sizeof(event.spot));
    if (QuietSpot(event.spot))
        return;

    // The probes, along the old travel direction.
    float direction[2];
    if (Horizontal(old.vel, direction) <= 0.0f)
        Horizontal(old.facing, direction);
    const float* at = event.spot;
    float high[3] = {at[0], at[1] + 48.0f, at[2]}, low[3] = {at[0], at[1] - 200.0f, at[2]};
    float beyond[3] = {at[0] + direction[0] * 24.0f, at[1], at[2] + direction[1] * 24.0f};
    float beyondHigh[3] = {beyond[0], beyond[1] + 48.0f, beyond[2]}, beyondLow[3] = {beyond[0], beyond[1] - 200.0f, beyond[2]};
    float lift = Script::GetFloat(kForwardHeight);
    float aheadFrom[3] = {old.pos[0], old.pos[1] + lift, old.pos[2]};
    float aheadTo[3] = {aheadFrom[0] + direction[0] * 120.0f, aheadFrom[1], aheadFrom[2] + direction[1] * 120.0f};
    for (int everything = 0; everything < 2; ++everything) {
        event.probes[0 + everything] = Probe(high, low, everything != 0);
        event.probes[2 + everything] = Probe(beyondHigh, beyondLow, everything != 0);
        event.probes[4 + everything] = Probe(aheadFrom, aheadTo, everything != 0);
    }

    event.number = ++g_events;
    event.level = LevelName();
    static const bool shots = ConfigInt("debug", "collision_event_screenshots", 0) != 0;
    event.screenshot = "-";
    if (shots) {
        char note[64];
        std::snprintf(note, sizeof(note), "collision event %d", event.number);
        event.screenshot = Sys::Screenshot::RequestEventShot(note) ? "requested" : "busy";
    }
    Log("%s", FormatEvent(event, &NameOf).c_str());
}

void Detect()
{
    const Sample& now = SampleAt(0);
    if (now.state != 0)
        return;
    float dirNow[2];
    if (Horizontal(now.vel, dirNow) < kMinSpeedNow)
        return;
    int limit = g_count - 1;
    for (int age = limit; age >= 1; --age) { // (the oldest first: the speed the skater had before)
        const Sample& old = SampleAt(age);
        if (now.frame - old.frame > static_cast<uint32_t>(kWindow) || old.state != 0)
            continue;
        float dirOld[2];
        if (Horizontal(old.vel, dirOld) < kMinSpeedBefore || dirOld[0] * dirNow[0] + dirOld[1] * dirNow[1] >= kOpposed)
            continue;
        // A sudden change between, no rail / lip / wall, not long in the air.
        bool sudden = false, refused = false;
        int air = 0;
        for (int a = age - 1; a >= 0; --a) {
            const Sample& s = SampleAt(a);
            const Sample& p = SampleAt(a + 1);
            if (s.state == 1)
                ++air;
            else if (s.state != 0)
                refused = true;
            float ds[2], dp[2];
            float vs = Horizontal(s.vel, ds), vp = Horizontal(p.vel, dp);
            if (vs < kStill || vp < kStill || ds[0] * dp[0] + ds[1] * dp[1] < kSudden)
                sudden = true;
        }
        if (refused || air > kMostAirFrames || !sudden)
            continue;
        Emit(age);
        // (seen once: the history starts again from this frame)
        Sample last = now;
        g_count = 1;
        g_next = 1;
        g_samples[0] = last;
        return;
    }
}

} // namespace

const char* PathName(Path path)
{
    switch (path) {
    case Path::WallAheadCorner: return "wall-ahead-corner";
    case Path::WallAhead: return "wall-ahead";
    case Path::WallAheadPlant: return "wall-ahead-push";
    case Path::SideWall: return "side-wall";
    case Path::AirBonk: return "air-bonk";
    case Path::AirWallVert: return "air-wall-vert";
    case Path::Ceiling: return "ceiling";
    case Path::GroundUnskatable: return "ground-unskatable";
    case Path::GroundRejected: return "ground-rejected";
    case Path::NoGround: return "no-ground";
    case Path::WallAheadClimb: return "wall-ahead-climb";
    default: return "?";
    }
}

bool On()
{
    static const bool on = ConfigInt("debug", "collision_events", 0) != 0;
    return on;
}

void NotePath(void* component, Path path, const Mth::Vector&)
{
    if (component != g_component || !On()) // (g_component: set by EndFrame, only while on)
        return;
    PathRecord& record = g_paths[g_pathNext];
    g_pathNext = (g_pathNext + 1) % kPathRecords;
    if (g_pathCount < kPathRecords)
        ++g_pathCount;
    record.frame = Mlp::FramesRun();
    record.path = path;
    record.face = FaceOf(FeelerAt(component, kFeeler));
}

void EndFrame(void* component)
{
    if (Mlp::FramesRun() == 0 || !On()) // (not in the early self-tests)
        return;
    if (component != g_component) {
        // (only the local skater's component: not a self-test's stand-in)
        if (ObjectOf(component) != LocalSkater())
            return;
        g_component = component;
        Reset();
    }
    void* object = ObjectOf(component);
    Sample s = {};
    s.frame = Mlp::FramesRun();
    s.state = At<int>(FlagsOf(component), kState);
    Copy3(s.pos, VectorAt(object, kPosition));
    Copy3(s.vel, VectorAt(object, kVelocity));
    Copy3(s.facing, VectorAt(object, kForward));
    Copy3(s.groundNormal, VectorAt(component, kGroundNormal));
    s.ground = FaceOf(FeelerAt(component, kLastGroundFeeler));
    if (g_count) {
        const Sample& last = SampleAt(0);
        float dx = s.pos[0] - last.pos[0], dy = s.pos[1] - last.pos[1], dz = s.pos[2] - last.pos[2];
        if (s.frame == last.frame) { // (the same frame again: the newer replaces it)
            g_next = (g_next - 1 + kHistory) % kHistory;
            --g_count;
        } else if (dx * dx + dy * dy + dz * dz > kTeleport * kTeleport || s.frame < last.frame)
            Reset();
    }
    g_samples[g_next] = s;
    g_next = (g_next + 1) % kHistory;
    if (g_count < kHistory)
        ++g_count;
    Detect();
}

// --- the line's text ---

std::string FlagNames(uint16_t flags)
{
    static const char* const kNames[16] = {"skatable", "not_skatable", "wall_ridable", "vert", "non_collidable",
                                           "decal", "trigger", "camera_passes", "no_shadow", "shadow",
                                           "no_shadow_wall", "under_ok", "invisible", "0x2000", "0x4000", "0x8000"};
    std::string out;
    for (int bit = 0; bit < 16; ++bit) {
        if (!(flags & (1u << bit)))
            continue;
        if (!out.empty())
            out += '|';
        out += kNames[bit];
    }
    return out.empty() ? "0" : out;
}

std::string FaceText(const Face& face, std::string (*name)(uint32_t))
{
    if (!face.hit)
        return "none";
    char text[256];
    std::snprintf(text, sizeof(text), "%s:%s;face:%d;flags:0x%04X;decoded:%s;terrain:%u;n:%.2f,%.2f,%.2f;at:%.1f,%.1f,%.1f",
                  face.object ? "object" : "sector", name(face.checksum).c_str(), face.index, face.flags,
                  FlagNames(face.flags).c_str(), face.terrain, face.normal[0], face.normal[1], face.normal[2],
                  face.point[0], face.point[1], face.point[2]);
    return text;
}

std::string FormatEvent(const Event& e, std::string (*name)(uint32_t))
{
    auto v3 = [](const float* v, const char* format) {
        char text[96];
        std::snprintf(text, sizeof(text), format, v[0], v[1], v[2]);
        return std::string(text);
    };
    static const char* const kProbes[6] = {"probe_down_hit", "probe_down_hit_all", "probe_down_beyond",
                                           "probe_down_beyond_all", "probe_ahead", "probe_ahead_all"};
    char head[160];
    std::snprintf(head, sizeof(head), "collision-event: n=%d level=%s frame=%u reason=%s paths=%s frames=%d", e.number,
                  e.level.empty() ? "-" : e.level.c_str(), e.frame, e.reason, e.paths.c_str(), e.frames);
    std::string line = head;
    line += " spot=" + v3(e.spot, "%.1f,%.1f,%.1f");
    line += " pos_before=" + v3(e.posBefore, "%.1f,%.1f,%.1f");
    line += " pos=" + v3(e.pos, "%.1f,%.1f,%.1f");
    line += " vel_before=" + v3(e.velBefore, "%.1f,%.1f,%.1f");
    line += " vel_after=" + v3(e.velAfter, "%.1f,%.1f,%.1f");
    char text[96];
    std::snprintf(text, sizeof(text), " heading_before=%.1f heading=%.1f", e.headingBefore, e.heading);
    line += text;
    line += " ground_n=" + v3(e.groundNormal, "%.2f,%.2f,%.2f");
    std::snprintf(text, sizeof(text), " ground_min_y=%.2f", e.groundMinY);
    line += text;
    line += " face=" + FaceText(e.face, name);
    for (int i = 0; i < 6; ++i)
        line += std::string(" ") + kProbes[i] + "=" + FaceText(e.probes[i], name);
    line += " screenshot=" + (e.screenshot.empty() ? std::string("-") : e.screenshot);
    return line;
}

} // namespace Skater::CollisionEvents
