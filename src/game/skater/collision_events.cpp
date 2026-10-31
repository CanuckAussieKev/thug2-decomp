// NOT ORIGINAL - [debug] collision_events: the skater's velocity turning round, logged with its cause (see
// collision_events.h).

#include "game/skater/collision_events.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/mth/vector.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/sys/mlp.h"
#include "game/sys/screenshot.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "game/sys/log.h"

namespace Skater::CollisionEvents {

namespace {

using namespace Physics;

constexpr uint32_t kForwardHeight = 0xD4205C9B; // Skater_First_Forward_Collision_Height
constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769, kSkitch = 0x3506CE64; // balance trick kinds

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
constexpr DWORD kSpotQuietMs = 1000;
constexpr int kPathRecords = 32;
constexpr uint32_t kBalanceTrick = 0x1524;

struct Ray {
    float start[3], end[3];
};

struct Sample {
    uint32_t frame;
    int state, physics;
    bool trick;
    uint32_t balance;
    uint16_t buttons;
    float stick[2];
    float pos[3], vel[3], facing[3], groundNormal[3];
    Face ground;
    Ray groundRay;
};

struct PathRecord {
    uint32_t frame;
    Path path;
    Face face;
    Ray ray;
};

void* g_component = nullptr;
Sample g_samples[kHistory];
int g_count = 0, g_next = 0; // (a ring: g_next the slot for the next sample)
PathRecord g_paths[kPathRecords];
int g_pathCount = 0, g_pathNext = 0;
int g_events = 0;
std::string g_colFile = "-", g_scnFile = "-";

// The checkpoints (a ring).
constexpr int kMarks = 512;
struct Mark {
    uint32_t frame;
    const char* label;
    float vel[3];
    Ray ray; // (the core's feeler's line there)
};
Mark g_marks[kMarks];
int g_markCount = 0, g_markNext = 0;

struct Spot {
    float at[3];
    DWORD time;
    int repeats;
};
Spot g_spots[32];
int g_spotNext = 0;

void Copy3(float* to, const Mth::Vector& from)
{
    to[0] = from.x;
    to[1] = from.y;
    to[2] = from.z;
}

Ray LineOf(const Nx::CFeeler& feeler)
{
    Ray ray;
    Copy3(ray.start, feeler.m_line.start);
    Copy3(ray.end, feeler.m_line.end);
    return ray;
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
    face.collObj = feeler.m_hit.object;
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
uint32_t LevelLoad()
{
    uint8_t* module = reinterpret_cast<uint8_t*>(Mdl::g_skateModule);
    return module ? At<uint32_t>(module, 0x378) : 0;
}
std::string LevelName()
{
    uint32_t load = LevelLoad();
    if (!load)
        return "-";
    std::string name = NameOf(load);
    if (name.size() > 5 && _strnicmp(name.c_str(), "Load_", 5) == 0)
        name.erase(0, 5);
    return name;
}

// A file's size and time ("<size>@YYYY-MM-DDTHH:MM:SS", local time), "" if it isn't there.
std::string FileStamp(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return {};
    FILETIME local;
    SYSTEMTIME t;
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &t))
        return {};
    char text[64];
    std::snprintf(text, sizeof(text), "%llu@%04u-%02u-%02uT%02u:%02u:%02u",
                  (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow, t.wYear, t.wMonth,
                  t.wDay, t.wHour, t.wMinute, t.wSecond);
    return text;
}

// The level's collision / scene file: the data overlay's converted one, else the game's own packed one.
std::string LevelFileStamp(const std::string& level, const char* kind)
{
    std::string overlay = "thug2-decomp-data\\levels\\" + level + "\\" + level + "." + kind + ".xbx";
    std::string packed = "Data\\pre\\" + level + kind + ".prx";
    for (const std::string& file : {overlay, packed}) {
        std::string stamp = FileStamp(GameFolderPath(file.c_str()));
        if (!stamp.empty())
            return stamp;
    }
    return "-";
}

// The local skater (the skate module's 0x00566520; null with none).
void* LocalSkater()
{
    uint8_t* module = reinterpret_cast<uint8_t*>(Mdl::g_skateModule);
    return module ? reinterpret_cast<Mdl::Skate*>(module)->GetLocalSkater() : nullptr;
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
    g_markCount = g_markNext = 0;
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

// Whether to leave this spot out (again within a second); `repeats`: those left out since its last line.
bool QuietSpot(const float* at, int* repeats)
{
    DWORD now = GetTickCount();
    for (Spot& spot : g_spots) {
        if (!spot.time)
            continue;
        float dx = spot.at[0] - at[0], dy = spot.at[1] - at[1], dz = spot.at[2] - at[2];
        if (dx * dx + dy * dy + dz * dz >= kSameSpot * kSameSpot)
            continue;
        if (now - spot.time < kSpotQuietMs) {
            ++spot.repeats;
            return true;
        }
        *repeats = spot.repeats;
        spot.repeats = 0;
        spot.time = now ? now : 1;
        return false;
    }
    Spot& spot = g_spots[g_spotNext];
    g_spotNext = (g_spotNext + 1) % static_cast<int>(sizeof(g_spots) / sizeof(g_spots[0]));
    std::memcpy(spot.at, at, sizeof(spot.at));
    spot.time = now ? now : 1;
    spot.repeats = 0;
    *repeats = 0;
    return false;
}

float Distance(const float* a, const float* b)
{
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// The distance from p to the segment a-b.
float ToSegment(const float* p, const float* a, const float* b)
{
    float e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float w[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float ee = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    float t = ee > 0.0f ? (w[0] * e[0] + w[1] * e[1] + w[2] * e[2]) / ee : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float q[3] = {a[0] + e[0] * t, a[1] + e[1] * t, a[2] + e[2] * t};
    return Distance(p, q);
}

// A static face's corners, its object's box and the edge nearest `spot` - open when no other face of its object has
// both its corners and a probe just beyond its middle finds nothing.
void FaceGeometry(const Face& face, const float* spot, Detail* detail)
{
    if (!face.hit || face.object || !face.collObj)
        return;
    auto* object = static_cast<Nx::CCollObj*>(const_cast<void*>(face.collObj));
    const Nx::CCollObjTriData* geometry = object->m_geometry;
    if (!geometry || face.index < 0 || face.index >= geometry->m_numFaces)
        return;
    uint32_t index[3];
    Mth::Vector raw[3];
    for (int k = 0; k < 3; ++k) {
        index[k] = geometry->FaceVertex(face.index, k);
        raw[k] = geometry->GetRawVertexPos(static_cast<int>(index[k]));
        Mth::Vector world;
        object->Call<Mth::Vector*>(9, &world, static_cast<int>(index[k])); // (slot 9: a vertex in the world)
        Copy3(detail->corners[k], world);
    }
    detail->geometry = true;
    Copy3(detail->boxMin, geometry->m_min);
    Copy3(detail->boxMax, geometry->m_max);
    for (int k = 0; k < 3; ++k) {
        float d = ToSegment(spot, detail->corners[k], detail->corners[(k + 1) % 3]);
        if (detail->edge < 0 || d < detail->edgeDistance) {
            detail->edge = k;
            detail->edgeDistance = d;
        }
    }
    int a = detail->edge, b = (a + 1) % 3, c = (a + 2) % 3;
    auto same = [&](uint32_t vertex, int k) {
        if (vertex == index[k])
            return true;
        Mth::Vector v = geometry->GetRawVertexPos(static_cast<int>(vertex));
        float dx = v.x - raw[k].x, dy = v.y - raw[k].y, dz = v.z - raw[k].z;
        return dx * dx + dy * dy + dz * dz < 0.25f;
    };
    for (int j = 0; j < geometry->m_numFaces; ++j) {
        if (j == face.index)
            continue;
        bool hasA = false, hasB = false;
        for (int m = 0; m < 3; ++m) {
            uint32_t v = geometry->FaceVertex(j, m);
            hasA = hasA || same(v, a);
            hasB = hasB || same(v, b);
        }
        if (hasA && hasB) {
            detail->edgeOpen = 0;
            return;
        }
    }
    // None in its object: for ground (a face facing up), anything (another object's) just beyond the edge's middle,
    // away from the third corner? (A wall's edges aren't judged: open stays unknown.)
    if (face.normal[1] < 0.5f)
        return;
    const float* pa = detail->corners[a];
    const float* pb = detail->corners[b];
    const float* pc = detail->corners[c];
    float e[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
    float w[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
    float ee = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    float t = ee > 0.0f ? (w[0] * e[0] + w[1] * e[1] + w[2] * e[2]) / ee : 0.0f;
    float out[3] = {-(w[0] - e[0] * t), -(w[1] - e[1] * t), -(w[2] - e[2] * t)};
    float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (length <= 0.0f)
        return;
    float q[3];
    for (int k = 0; k < 3; ++k)
        q[k] = (pa[k] + pb[k]) * 0.5f + out[k] / length * 6.0f;
    float high[3] = {q[0], q[1] + 24.0f, q[2]}, low[3] = {q[0], q[1] - 48.0f, q[2]};
    detail->edgeOpen = Probe(high, low, false).hit ? 0 : 1;
}

const char* StateName(int state)
{
    static const char* const kNames[] = {"ground", "air", "wallride", "lip", "rail", "wallplant"};
    return state >= 0 && state < 6 ? kNames[state] : "-";
}
char StateLetter(int state) { return state >= 0 && state < 6 ? "gawlrp"[state] : '?'; }

const char* BalanceName(uint32_t kind)
{
    switch (kind) {
    case kManual: return "manual";
    case kNoseManual: return "nose_manual";
    case kSkitch: return "skitch";
    case 0: return "-";
    default: return "other";
    }
}

void Emit(int before)
{
    const Sample& now = SampleAt(0);
    const Sample& old = SampleAt(before);
    Event event;
    Detail detail;
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

    // Between which checkpoints the velocity turned against the old direction (the first time).
    float dirOld[2];
    Horizontal(old.vel, dirOld);
    const Mark* previous = nullptr;
    const Mark* flip = nullptr;
    bool uberFrig = false;
    event.flipped = "-";
    for (int i = 0; i < g_markCount; ++i) {
        const Mark& mark = g_marks[(g_markNext - g_markCount + i + kMarks) % kMarks];
        if (mark.frame < old.frame || mark.frame > now.frame)
            continue;
        bool against = mark.vel[0] * dirOld[0] + mark.vel[2] * dirOld[1] < 0.0f;
        if (against && previous) {
            char text[160];
            std::snprintf(text, sizeof(text), "%s>%s@%u", previous->label, mark.label, mark.frame);
            event.flipped = text;
            flip = &mark;
            // Between one frame's physics and the next, the velocity exactly negated: the skater's adjust-physics
            // component (0x004F8420, not rewritten) found no collision within uber_frig_current_height feet below
            // the skater and put it back where the frame began (the core's +0x98) going the other way - the "uber
            // frig", as the walking code calls its own (walk_collision.cpp UpdateGroundBelow). Its down feeler is
            // the core's own: its line is still there at the next frame's start.
            float sum = 0.0f, size = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += (mark.vel[k] + previous->vel[k]) * (mark.vel[k] + previous->vel[k]);
                size += previous->vel[k] * previous->vel[k];
            }
            if (std::strcmp(previous->label, "update-rest") == 0 && std::strcmp(mark.label, "update-start") == 0 &&
                std::sqrt(sum) < 0.02f * std::sqrt(size) + 1.0f)
                uberFrig = true;
            break;
        }
        previous = against ? nullptr : &mark;
    }

    // The ground at its steepest; the buttons held in the window.
    const Sample* steepest = &now;
    uint16_t held = 0;
    for (int age = 0; age <= before; ++age) {
        const Sample& s = SampleAt(age);
        held |= s.buttons;
        if (s.state == 0 && s.groundNormal[1] < steepest->groundNormal[1])
            steepest = &s;
        if (s.state == 0 && s.groundNormal[1] < event.groundMinY)
            event.groundMinY = s.groundNormal[1];
    }
    const char* reason = ChooseReason(uberFrig, strongest ? PathName(strongest->path) : nullptr, event.groundMinY);
    if (!reason)
        return; // (flat ground and no path: the player turned round)
    event.reason = reason;
    const Ray* ray = nullptr;
    if (uberFrig) {
        event.face = now.ground; // (the last ground the skater stood on)
        ray = flip ? &flip->ray : nullptr;
    } else if (strongest) {
        event.face = strongest->face;
        ray = &strongest->ray;
    } else {
        event.face = steepest->ground;
        ray = &steepest->groundRay;
    }
    if (event.face.hit)
        std::memcpy(event.spot, event.face.point, sizeof(event.spot));
    else
        std::memcpy(event.spot, now.pos, sizeof(event.spot));
    if (QuietSpot(event.spot, &event.repeats))
        return;

    // The probes, along the travel before the reversal.
    float direction[2];
    if (Horizontal(old.vel, direction) <= 0.0f)
        Horizontal(old.facing, direction);
    event.probeDir[0] = direction[0];
    event.probeDir[2] = direction[1];
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
    static const bool shots = ConfigInt("debug", "collision_event_screenshots", 1) != 0;
    event.screenshot = "-";
    if (shots) {
        char note[64];
        std::snprintf(note, sizeof(note), "collision event %d", event.number);
        event.screenshot = Sys::Screenshot::RequestEventShot(note) ? "requested" : "busy";
    }

    // The detail line.
    detail.number = event.number;
    detail.state = StateName(now.state);
    detail.physics = now.physics == 0 ? "skating" : now.physics == 1 ? "walking" : "-";
    detail.speed = std::sqrt(now.vel[0] * now.vel[0] + now.vel[1] * now.vel[1] + now.vel[2] * now.vel[2]);
    detail.trick = now.trick;
    detail.balance = BalanceName(now.balance);
    detail.input = ButtonNames(now.buttons);
    detail.inputWindow = ButtonNames(held);
    detail.stick[0] = now.stick[0];
    detail.stick[1] = now.stick[1];
    if (ray) {
        detail.ray = true;
        std::memcpy(detail.rayStart, ray->start, sizeof(detail.rayStart));
        std::memcpy(detail.rayEnd, ray->end, sizeof(detail.rayEnd));
    }
    FaceGeometry(event.face, event.spot, &detail);
    detail.colFile = g_colFile;
    detail.scnFile = g_scnFile;
    detail.approach.clear();
    for (int age = 50; age >= 0; age -= 10) {
        if (age >= g_count)
            continue;
        const Sample& s = SampleAt(age);
        char text[64];
        std::snprintf(text, sizeof(text), "%s%.0f,%.0f,%.0f,%c", detail.approach.empty() ? "" : ";", s.pos[0],
                      s.pos[1], s.pos[2], StateLetter(s.state));
        detail.approach += text;
    }
    detail.camera = Sys::Screenshot::LastView(detail.cameraPos, detail.cameraDir);

    Log("%s", FormatEvent(event, &NameOf).c_str());
    Log("%s", FormatDetail(detail).c_str());
    // (on screen too, for 2 s: a newer one replaces it)
    char notice[96];
    std::snprintf(notice, sizeof(notice), "collision event %d: %s%s", event.number, event.reason,
                  event.screenshot == "requested" ? " (screenshot)" : "");
    Sys::Screenshot::ShowNotice(notice);
}

void Detect()
{
    Motion frames[kHistory];
    for (int i = 0; i < g_count; ++i) {
        const Sample& s = SampleAt(g_count - 1 - i);
        frames[i].frame = s.frame;
        frames[i].state = s.state;
        std::memcpy(frames[i].vel, s.vel, sizeof(frames[i].vel));
    }
    int index = FindReversal(frames, g_count);
    if (index < 0)
        return;
    Emit(g_count - 1 - index);
    // (seen once - logged or not: the history starts again from this frame)
    Sample last = SampleAt(0);
    g_count = 1;
    g_next = 1;
    g_samples[0] = last;
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

int FindReversal(const Motion* frames, int count)
{
    if (count < 2)
        return -1;
    const Motion& now = frames[count - 1];
    if (now.state != 0)
        return -1;
    float dirNow[2];
    if (Horizontal(now.vel, dirNow) < kMinSpeedNow)
        return -1;
    for (int i = 0; i < count - 1; ++i) { // (the oldest first: the speed the skater had before)
        const Motion& old = frames[i];
        if (now.frame - old.frame > static_cast<uint32_t>(kWindow) || old.state != 0)
            continue;
        float dirOld[2];
        if (Horizontal(old.vel, dirOld) < kMinSpeedBefore || dirOld[0] * dirNow[0] + dirOld[1] * dirNow[1] >= kOpposed)
            continue;
        // A sudden change between, no rail / lip / wall, not long in the air.
        bool sudden = false, refused = false;
        int air = 0;
        for (int j = i + 1; j < count; ++j) {
            const Motion& s = frames[j];
            const Motion& p = frames[j - 1];
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
        return i;
    }
    return -1;
}

const char* ChooseReason(bool uberFrig, const char* path, float groundMinY)
{
    if (uberFrig)
        return "uber-frig";
    if (path)
        return path;
    return groundMinY < kSlopeNormalY ? "slope" : nullptr;
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
    record.ray = LineOf(FeelerAt(component, kFeeler));
}

void Checkpoint(void* component, const char* label)
{
    if (component != g_component || !On()) // (g_component: set by EndFrame, only while on)
        return;
    Mark& mark = g_marks[g_markNext];
    g_markNext = (g_markNext + 1) % kMarks;
    if (g_markCount < kMarks)
        ++g_markCount;
    mark.frame = Mlp::FramesRun();
    mark.label = label;
    Copy3(mark.vel, VectorAt(ObjectOf(component), kVelocity));
    mark.ray = LineOf(FeelerAt(component, kFeeler));
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
    // A line once a level, so the log shows the switch was on; the level's files noted for the events.
    static uint32_t s_loggedLoad = 0;
    uint32_t load = LevelLoad();
    if (load != s_loggedLoad) {
        s_loggedLoad = load;
        std::string level = LevelName();
        g_colFile = LevelFileStamp(level, "col");
        g_scnFile = LevelFileStamp(level, "scn");
        Log("collision-event: logging on in %s (col %s, scn %s)", level.c_str(), g_colFile.c_str(), g_scnFile.c_str());
    }
    void* object = ObjectOf(component);
    uint8_t* flags = FlagsOf(component);
    Sample s = {};
    s.frame = Mlp::FramesRun();
    s.state = At<int>(flags, kState);
    s.physics = At<int>(flags, 0x1C);
    s.trick = flags[0xE0] != 0;
    void* balance = At<void*>(component, kBalanceTrick);
    s.balance = balance ? At<uint32_t>(balance, 0x18) : 0;
    if (uint8_t* pad = InputOf(component)) { // (the Obj::CControlPad: 16 buttons of 0x20 bytes, the sticks at +0x210)
        for (int i = 0; i < 16; ++i)
            if (pad[i * 0x20])
                s.buttons = static_cast<uint16_t>(s.buttons | (1u << i));
        s.stick[0] = At<float>(pad, 0x210);
        s.stick[1] = At<float>(pad, 0x214);
    }
    Copy3(s.pos, VectorAt(object, kPosition));
    Copy3(s.vel, VectorAt(object, kVelocity));
    Copy3(s.facing, VectorAt(object, kForward));
    Copy3(s.groundNormal, VectorAt(component, kGroundNormal));
    s.ground = FaceOf(FeelerAt(component, kLastGroundFeeler));
    s.groundRay = LineOf(FeelerAt(component, kLastGroundFeeler));
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

// --- the lines' text ---

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

std::string ButtonNames(uint16_t mask)
{
    static const char* const kNames[16] = {"up", "down", "left", "right", "l1", "l2", "l3", "r1",
                                           "r2", "r3", "circle", "square", "triangle", "x", "start", "select"};
    std::string out;
    for (int bit = 0; bit < 16; ++bit) {
        if (!(mask & (1u << bit)))
            continue;
        if (!out.empty())
            out += '+';
        out += kNames[bit];
    }
    return out.empty() ? "-" : out;
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

namespace {

std::string V3(const float* v, const char* format = "%.1f,%.1f,%.1f")
{
    char text[96];
    std::snprintf(text, sizeof(text), format, v[0], v[1], v[2]);
    return text;
}

} // namespace

std::string FormatEvent(const Event& e, std::string (*name)(uint32_t))
{
    static const char* const kProbes[6] = {"probe_down_hit", "probe_down_hit_all", "probe_down_beyond",
                                           "probe_down_beyond_all", "probe_ahead", "probe_ahead_all"};
    char head[160];
    std::snprintf(head, sizeof(head), "collision-event: n=%d level=%s frame=%u reason=%s paths=%s frames=%d", e.number,
                  e.level.empty() ? "-" : e.level.c_str(), e.frame, e.reason, e.paths.c_str(), e.frames);
    std::string line = head;
    line += " spot=" + V3(e.spot);
    line += " pos_before=" + V3(e.posBefore);
    line += " pos=" + V3(e.pos);
    line += " vel_before=" + V3(e.velBefore);
    line += " vel_after=" + V3(e.velAfter);
    char text[96];
    std::snprintf(text, sizeof(text), " heading_before=%.1f heading=%.1f", e.headingBefore, e.heading);
    line += text;
    line += " ground_n=" + V3(e.groundNormal, "%.2f,%.2f,%.2f");
    std::snprintf(text, sizeof(text), " ground_min_y=%.2f", e.groundMinY);
    line += text;
    line += " face=" + FaceText(e.face, name);
    for (int i = 0; i < 6; ++i)
        line += std::string(" ") + kProbes[i] + "=" + FaceText(e.probes[i], name);
    line += " probe_dir=" + V3(e.probeDir, "%.2f,%.2f,%.2f");
    line += " flipped=" + (e.flipped.empty() ? std::string("-") : e.flipped);
    line += " repeats=" + std::to_string(e.repeats);
    line += " screenshot=" + (e.screenshot.empty() ? std::string("-") : e.screenshot);
    return line;
}

std::string FormatDetail(const Detail& d)
{
    char text[256];
    std::snprintf(text, sizeof(text),
                  "collision-event-detail: n=%d state=%s physics=%s speed=%.1f trick=%d balance=%s input=%s "
                  "input_window=%s stick=%.2f,%.2f",
                  d.number, d.state, d.physics, d.speed, d.trick ? 1 : 0, d.balance, d.input.c_str(),
                  d.inputWindow.c_str(), d.stick[0], d.stick[1]);
    std::string line = text;
    line += " ray=" + (d.ray ? V3(d.rayStart) + ">" + V3(d.rayEnd) : std::string("-"));
    if (d.geometry) {
        line += " corners=" + V3(d.corners[0]) + ";" + V3(d.corners[1]) + ";" + V3(d.corners[2]);
        line += " col_box=" + V3(d.boxMin) + ">" + V3(d.boxMax);
    } else {
        line += " corners=- col_box=-";
    }
    if (d.edge >= 0) {
        std::snprintf(text, sizeof(text), " edge=%d edge_dist=%.1f open_edge=%s", d.edge, d.edgeDistance,
                      d.edgeOpen < 0 ? "-" : d.edgeOpen ? "1" : "0");
        line += text;
    } else {
        line += " edge=- edge_dist=- open_edge=-";
    }
    line += " col_file=" + d.colFile + " scn_file=" + d.scnFile;
    line += " approach=" + (d.approach.empty() ? std::string("-") : d.approach);
    line += " camera=" + (d.camera ? V3(d.cameraPos) + ">" + V3(d.cameraDir, "%.3f,%.3f,%.3f") : std::string("-"));
    return line;
}

} // namespace Skater::CollisionEvents
