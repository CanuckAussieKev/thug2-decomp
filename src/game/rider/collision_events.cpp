// NOT ORIGINAL - [debug] collision_events: the skater's velocity turning round, logged with its cause (see
// collision_events.h).

#include "game/rider/collision_events.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "engine/sys/mlp.h"
#include "platform/screenshot.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/run_log.h"

namespace skater::collision_events {

namespace {

using namespace physics;

constexpr uint32_t kForwardHeight = 0xD4205C9B; // Skater_First_Forward_Collision_Height
constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769, kSkitch = 0x3506CE64; // balance trick kinds

constexpr int kHistory = 90;         // frames kept
constexpr int kWindow = 60;          // frames a reversal may take
constexpr float kMinSpeedNow = 5.0f; // inches a second
constexpr float kMinSpeedBefore = 60.0f;
constexpr float kOpposed = -0.8f; // the two directions' dot product
constexpr float kSudden = 0.5f;   // one frame's direction change: dot below this (60 degrees)
constexpr float kStill = 1.0f;    // ... or the speed through (almost) zero
constexpr int kMostAirFrames = 20;
constexpr float kTeleport = 300.0f; // a frame's move past this starts the history again
constexpr float kSameSpot = 40.0f;
constexpr DWORD kSpotQuietMs = 1000;
constexpr int kPathRecords = 32;

struct Ray {
    float start[3], end[3];
};

struct Sample {
    uint32_t frame{};
    int state{}, physics{};
    bool trick{};
    uint32_t balance{};
    uint16_t buttons{};
    float stick[2]{};
    float pos[3]{}, vel[3]{}, facing[3]{}, ground_normal_[3]{};
    Face ground;
    Ray ground_ray_{};
};

struct PathRecord {
    uint32_t frame{};
    RouteWay path{};
    Face face;
    Ray ray{};
};

void* g_component = nullptr;
Sample g_samples[kHistory];
int g_sample_total = 0, g_next = 0; // (a ring: g_next the slot for the next sample)
PathRecord g_paths[kPathRecords];
int g_path_count = 0, g_path_next = 0;
int g_events = 0;
std::string g_col_file = "-", g_scn_file = "-";

// The checkpoints (a ring).
constexpr int kMarks = 512;
struct Mark {
    uint32_t frame;
    const char* label;
    float vel[3];
    Ray ray; // (the core's feeler's line there)
};
Mark g_marks[kMarks];
int g_marker_total = 0, g_mark_next = 0;

struct Spot {
    float at[3];
    DWORD time;
    int repeats;
};
Spot g_spots[32];
int g_spot_next = 0;

void copy3(float* to, const maths::Vec4& from)
{
    to[0] = from.x;
    to[1] = from.y;
    to[2] = from.z;
}

Ray line_of(const render::ProbeRay& feeler)
{
    Ray ray{};
    copy3(ray.start, feeler.line_.start);
    copy3(ray.end, feeler.line_.end);
    return ray;
}

Face face_of(const render::ProbeRay& feeler)
{
    Face face;
    face.hit = feeler.hit_.hit;
    if (!face.hit)
        return face;
    face.object = feeler.entity_.owner_object_ != nullptr;
    face.checksum = face.object ? feeler.entity_key_ : feeler.hit_.object_checksum_;
    face.index = static_cast<int>(feeler.hit_.face);
    face.flags = feeler.hit_.face_flags_;
    face.terrain = feeler.hit_.terrain;
    face.coll_obj_ = feeler.hit_.object;
    copy3(face.point, feeler.contact_pos_);
    copy3(face.normal, feeler.surface_dir_);
    return face;
}

std::string get_name_of(uint32_t checksum)
{
    const char* name = script::kept_qb_name(checksum);
    if (!name)
        name = script::look_up_hash_name(checksum);
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
uint32_t current_load_state() { return modules::g_skate_module ? modules::g_skate_module->level_ : 0; }
std::string get_level_name()
{
    uint32_t load = current_load_state();
    if (!load)
        return "-";
    std::string name = get_name_of(load);
    if (name.size() > 5 && _strnicmp(name.c_str(), "Load_", 5) == 0)
        name.erase(0, 5);
    return name;
}

// A file's size and time ("<size>@YYYY-MM-DDTHH:MM:SS", local time), "" if it isn't there.
std::string file_stamp(const std::string& path)
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
std::string level_file_stamp(const std::string& level, const char* kind)
{
    std::string overlay = "thug2-decomp-data\\levels\\" + level + "\\" + level + "." + kind + ".xbx";
    std::string packed = "Data\\pre\\" + level + kind + ".prx";
    for (const std::string& file : {overlay, packed}) {
        std::string stamp = file_stamp(game_folder_path(file.c_str()));
        if (!stamp.empty())
            return stamp;
    }
    return "-";
}

// The local skater (the skate module's 0x00566520; null with none).
void* local_skater()
{
    auto* module = reinterpret_cast<uint8_t*>(modules::g_skate_module);
    return module ? reinterpret_cast<modules::Skate*>(module)->get_home_rider() : nullptr;
}

float get_horizontal(const float* v, float* direction)
{
    float length = std::sqrt(v[0] * v[0] + v[2] * v[2]);
    direction[0] = length > 0.0f ? v[0] / length : 0.0f;
    direction[1] = length > 0.0f ? v[2] / length : 0.0f;
    return length;
}

float get_heading_of(const float* facing)
{
    return static_cast<float>(std::atan2(static_cast<double>(facing[0]), static_cast<double>(facing[2])) * 180.0 /
                              3.14159265358979);
}

const Sample& sample_at(int age) // 0: the newest
{
    return g_samples[((g_next - 1 - age) % kHistory + kHistory) % kHistory];
}

void get_reset()
{
    g_sample_total = g_next = 0;
    g_path_count = g_path_next = 0;
    g_marker_total = g_mark_next = 0;
}

// A probe line: as the skater's feelers see (faces flagged 0x10 skipped), or every face.
Face get_probe(const float* from, const float* to, bool everything)
{
    maths::Vec4 start = {from[0], from[1], from[2], 1.0f};
    maths::Vec4 end = {to[0], to[1], to[2], 1.0f};
    render::ProbeRay ray_storage(&start, &end);
    render::ProbeRay* feeler = &ray_storage;
    feeler->set_excluded(everything ? 0 : 0x10, 0);
    feeler->get_hit_shape(true, false);
    return face_of(*feeler);
}

// Whether to leave this spot out (again within a second); `repeats`: those left out since its last line.
bool quiet_spot(const float* at, int* repeats)
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
    Spot& spot = g_spots[g_spot_next];
    g_spot_next = (g_spot_next + 1) % static_cast<int>(sizeof(g_spots) / sizeof(g_spots[0]));
    std::memcpy(spot.at, at, sizeof(spot.at));
    spot.time = now ? now : 1;
    spot.repeats = 0;
    *repeats = 0;
    return false;
}

float span_to(const float* a, const float* b)
{
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// The distance from p to the segment a-b.
float to_segment(const float* p, const float* a, const float* b)
{
    float e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float w[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float ee = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    float t = ee > 0.0f ? (w[0] * e[0] + w[1] * e[1] + w[2] * e[2]) / ee : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float q[3] = {a[0] + e[0] * t, a[1] + e[1] * t, a[2] + e[2] * t};
    return span_to(p, q);
}

// A static face's corners, its object's box and the edge nearest `spot` - open when no other face of its object has
// both its corners and a probe just beyond its middle finds nothing.
void face_geometry(const Face& face, const float* spot, Detail* detail)
{
    if (!face.hit || face.object || !face.coll_obj_)
        return;
    auto* object = static_cast<render::HitMesh*>(face.coll_obj_);
    const render::TriMeshSource* geometry = object->geometry_;
    if (!geometry || face.index < 0 || face.index >= geometry->tri_count_)
        return;
    uint32_t index[3];
    maths::Vec4 raw[3];
    for (int k = 0; k < 3; ++k) {
        index[k] = geometry->face_vertex(face.index, k);
        raw[k] = geometry->read_stored_vertex(static_cast<int>(index[k]));
        maths::Vec4 world = object->read_vertex_point(static_cast<int>(index[k])); // (a vertex in the world)
        copy3(detail->corners[k], world);
    }
    detail->geometry = true;
    copy3(detail->bounds_low_, geometry->lower_limit_);
    copy3(detail->bounds_high_, geometry->upper_limit_);
    for (int k = 0; k < 3; ++k) {
        float d = to_segment(spot, detail->corners[k], detail->corners[(k + 1) % 3]);
        if (detail->edge < 0 || d < detail->edge_distance_) {
            detail->edge = k;
            detail->edge_distance_ = d;
        }
    }
    int a = detail->edge, b = (a + 1) % 3, c = (a + 2) % 3;
    auto same = [&](uint32_t vertex, int k) {
        if (vertex == index[k])
            return true;
        maths::Vec4 v = geometry->read_stored_vertex(static_cast<int>(vertex));
        float dx = v.x - raw[k].x, dy = v.y - raw[k].y, dz = v.z - raw[k].z;
        return dx * dx + dy * dy + dz * dz < 0.25f;
    };
    for (int j = 0; j < geometry->tri_count_; ++j) {
        if (j == face.index)
            continue;
        bool has_a = false, has_b = false;
        for (int m = 0; m < 3; ++m) {
            uint32_t v = geometry->face_vertex(j, m);
            has_a = has_a || same(v, a);
            has_b = has_b || same(v, b);
        }
        if (has_a && has_b) {
            detail->edge_open_ = 0;
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
    detail->edge_open_ = get_probe(high, low, false).hit ? 0 : 1;
}

const char* get_state_name(int state)
{
    static const char* const kNames[] = {"ground", "air", "wallride", "lip", "rail", "wallplant"};
    return state >= 0 && state < 6 ? kNames[state] : "-";
}
char state_letter(int state) { return state >= 0 && state < 6 ? "gawlrp"[state] : '?'; }

const char* balance_name(uint32_t kind)
{
    switch (kind) {
    case kManual:
        return "manual";
    case kNoseManual:
        return "nose_manual";
    case kSkitch:
        return "skitch";
    case 0:
        return "-";
    default:
        return "other";
    }
}

void get_emit(int before)
{
    const Sample& now = sample_at(0);
    const Sample& old = sample_at(before);
    Occurrence event;
    Detail detail;
    event.frame = now.frame;
    event.frames = static_cast<int>(now.frame - old.frame);
    std::memcpy(event.pos_before_, old.pos, sizeof(event.pos_before_));
    std::memcpy(event.pos, now.pos, sizeof(event.pos));
    std::memcpy(event.vel_before_, old.vel, sizeof(event.vel_before_));
    std::memcpy(event.vel_after_, now.vel, sizeof(event.vel_after_));
    std::memcpy(event.ground_normal_, now.ground_normal_, sizeof(event.ground_normal_));
    event.heading_before_ = get_heading_of(old.facing);
    event.heading = get_heading_of(now.facing);

    // The paths that ran between the two (in order), and the strongest.
    const PathRecord* strongest = nullptr;
    bool seen[static_cast<int>(RouteWay::kTotalOf)] = {};
    for (int i = 0; i < g_path_count; ++i) {
        const PathRecord& record = g_paths[(g_path_next - g_path_count + i + kPathRecords) % kPathRecords];
        if (record.frame < old.frame || record.frame > now.frame)
            continue;
        int p = static_cast<int>(record.path);
        if (!seen[p]) {
            seen[p] = true;
            if (!event.paths.empty())
                event.paths += ',';
            event.paths += path_name(record.path);
        }
        if (!strongest || record.path <= strongest->path)
            strongest = &record;
    }
    if (event.paths.empty())
        event.paths = "-";

    // Between which checkpoints the velocity turned against the old direction (the first time).
    float dir_old[2];
    get_horizontal(old.vel, dir_old);
    const Mark* previous = nullptr;
    const Mark* flip = nullptr;
    bool uber_frig = false;
    event.flipped = "-";
    for (int i = 0; i < g_marker_total; ++i) {
        const Mark& mark = g_marks[(g_mark_next - g_marker_total + i + kMarks) % kMarks];
        if (mark.frame < old.frame || mark.frame > now.frame)
            continue;
        bool against = mark.vel[0] * dir_old[0] + mark.vel[2] * dir_old[1] < 0.0f;
        if (against && previous) {
            char text[160];
            std::snprintf(text, sizeof(text), "%s>%s@%u", previous->label, mark.label, mark.frame);
            event.flipped = text;
            flip = &mark;
            // Between one frame's physics and the next, the velocity exactly negated: the skater's adjust-physics
            // component (0x004F8420, not rewritten) found no collision within uber_frig_current_height feet below
            // the skater and put it back where the frame began (the core's +0x98) going the other way - the "uber
            // frig", as the walking code calls its own (walk_collision.cpp update_ground_below). Its down feeler is
            // the core's own: its line is still there at the next frame's start.
            float sum = 0.0f, size = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += (mark.vel[k] + previous->vel[k]) * (mark.vel[k] + previous->vel[k]);
                size += previous->vel[k] * previous->vel[k];
            }
            if (std::strcmp(previous->label, "update-rest") == 0 && std::strcmp(mark.label, "update-start") == 0 &&
                std::sqrt(sum) < 0.02f * std::sqrt(size) + 1.0f)
                uber_frig = true;
            break;
        }
        previous = against ? nullptr : &mark;
    }

    // The ground at its steepest; the buttons held in the window.
    const Sample* steepest = &now;
    uint16_t held = 0;
    for (int age = 0; age <= before; ++age) {
        const Sample& s = sample_at(age);
        held |= s.buttons;
        if (s.state == 0 && s.ground_normal_[1] < steepest->ground_normal_[1])
            steepest = &s;
        if (s.state == 0 && s.ground_normal_[1] < event.ground_min_y_)
            event.ground_min_y_ = s.ground_normal_[1];
    }
    const char* reason =
        choose_reason(uber_frig, strongest ? path_name(strongest->path) : nullptr, event.ground_min_y_);
    if (!reason)
        return; // (flat ground and no path: the player turned round)
    event.reason = reason;
    const Ray* ray = nullptr;
    if (uber_frig) {
        event.face = now.ground; // (the last ground the skater stood on)
        ray = flip ? &flip->ray : nullptr;
    } else if (strongest) {
        event.face = strongest->face;
        ray = &strongest->ray;
    } else {
        event.face = steepest->ground;
        ray = &steepest->ground_ray_;
    }
    if (event.face.hit)
        std::memcpy(event.spot, event.face.point, sizeof(event.spot));
    else
        std::memcpy(event.spot, now.pos, sizeof(event.spot));
    if (quiet_spot(event.spot, &event.repeats))
        return;

    // The probes, along the travel before the reversal.
    float direction[2];
    if (get_horizontal(old.vel, direction) <= 0.0f)
        get_horizontal(old.facing, direction);
    event.probe_dir_[0] = direction[0];
    event.probe_dir_[2] = direction[1];
    const float* at = event.spot;
    float high[3] = {at[0], at[1] + 48.0f, at[2]}, low[3] = {at[0], at[1] - 200.0f, at[2]};
    float beyond[3] = {at[0] + direction[0] * 24.0f, at[1], at[2] + direction[1] * 24.0f};
    float beyond_high[3] = {beyond[0], beyond[1] + 48.0f, beyond[2]},
          beyond_low[3] = {beyond[0], beyond[1] - 200.0f, beyond[2]};
    float lift = script::fetch_float(kForwardHeight);
    float ahead_from[3] = {old.pos[0], old.pos[1] + lift, old.pos[2]};
    float ahead_to[3] = {ahead_from[0] + direction[0] * 120.0f, ahead_from[1], ahead_from[2] + direction[1] * 120.0f};
    for (int everything = 0; everything < 2; ++everything) {
        event.probes[0 + everything] = get_probe(high, low, everything != 0);
        event.probes[2 + everything] = get_probe(beyond_high, beyond_low, everything != 0);
        event.probes[4 + everything] = get_probe(ahead_from, ahead_to, everything != 0);
    }

    event.number = ++g_events;
    event.level = get_level_name();
    static const bool kShots = config_int("debug", "collision_event_screenshots", 1) != 0;
    event.screenshot = "-";
    if (kShots) {
        char note[64];
        std::snprintf(note, sizeof(note), "collision event %d", event.number);
        event.screenshot = sys::screenshot::request_event_shot(note) ? "requested" : "busy";
    }

    // The detail line.
    detail.number = event.number;
    detail.state = get_state_name(now.state);
    detail.physics = now.physics == 0 ? "skating" : now.physics == 1 ? "walking" : "-";
    detail.speed = std::sqrt(now.vel[0] * now.vel[0] + now.vel[1] * now.vel[1] + now.vel[2] * now.vel[2]);
    detail.trick = now.trick;
    detail.balance = balance_name(now.balance);
    detail.input = pad_button_labels(now.buttons);
    detail.input_window_ = pad_button_labels(held);
    detail.stick[0] = now.stick[0];
    detail.stick[1] = now.stick[1];
    if (ray) {
        detail.ray = true;
        std::memcpy(detail.probe_origin_, ray->start, sizeof(detail.probe_origin_));
        std::memcpy(detail.ray_end_, ray->end, sizeof(detail.ray_end_));
    }
    face_geometry(event.face, event.spot, &detail);
    detail.col_file_ = g_col_file;
    detail.scn_file_ = g_scn_file;
    detail.approach.clear();
    for (int age = 50; age >= 0; age -= 10) {
        if (age >= g_sample_total)
            continue;
        const Sample& s = sample_at(age);
        char text[64];
        std::snprintf(text, sizeof(text), "%s%.0f,%.0f,%.0f,%c", detail.approach.empty() ? "" : ";", s.pos[0], s.pos[1],
                      s.pos[2], state_letter(s.state));
        detail.approach += text;
    }
    detail.camera = sys::screenshot::last_view(detail.camera_pos_, detail.camera_dir_);

    get_log("%s", format_event(event, &get_name_of).c_str());
    get_log("%s", format_detail(detail).c_str());
    // (on screen too, for 2 s: a newer one replaces it)
    char notice[96];
    std::snprintf(notice, sizeof(notice), "collision event %d: %s%s", event.number, event.reason,
                  event.screenshot == "requested" ? " (screenshot)" : "");
    sys::screenshot::show_notice(notice);
}

void detect()
{
    Motion frames[kHistory];
    for (int i = 0; i < g_sample_total; ++i) {
        const Sample& s = sample_at(g_sample_total - 1 - i);
        frames[i].frame = s.frame;
        frames[i].state = s.state;
        std::memcpy(frames[i].vel, s.vel, sizeof(frames[i].vel));
    }
    int index = find_reversal(frames, g_sample_total);
    if (index < 0)
        return;
    get_emit(g_sample_total - 1 - index);
    // (seen once - logged or not: the history starts again from this frame)
    Sample last = sample_at(0);
    g_sample_total = 1;
    g_next = 1;
    g_samples[0] = last;
}

} // namespace

const char* path_name(RouteWay path)
{
    switch (path) {
    case RouteWay::kWallAheadCorner:
        return "wall-ahead-corner";
    case RouteWay::kWallAhead:
        return "wall-ahead";
    case RouteWay::kWallAheadPlant:
        return "wall-ahead-push";
    case RouteWay::kSideWall:
        return "side-wall";
    case RouteWay::kAirBonk:
        return "air-bonk";
    case RouteWay::kAirWallVert:
        return "air-wall-vert";
    case RouteWay::kCeiling:
        return "ceiling";
    case RouteWay::kGroundUnskatable:
        return "ground-unskatable";
    case RouteWay::kGroundRejected:
        return "ground-rejected";
    case RouteWay::kNoGround:
        return "no-ground";
    case RouteWay::kWallAheadClimb:
        return "wall-ahead-climb";
    default:
        return "?";
    }
}

int find_reversal(const Motion* frames, int count)
{
    if (count < 2)
        return -1;
    const Motion& now = frames[count - 1];
    if (now.state != 0)
        return -1;
    float dir_now[2];
    if (get_horizontal(now.vel, dir_now) < kMinSpeedNow)
        return -1;
    for (int i = 0; i < count - 1; ++i) { // (the oldest first: the speed the skater had before)
        const Motion& old = frames[i];
        if (now.frame - old.frame > static_cast<uint32_t>(kWindow) || old.state != 0)
            continue;
        float dir_old[2];
        if (get_horizontal(old.vel, dir_old) < kMinSpeedBefore ||
            dir_old[0] * dir_now[0] + dir_old[1] * dir_now[1] >= kOpposed)
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
            float vs = get_horizontal(s.vel, ds), vp = get_horizontal(p.vel, dp);
            if (vs < kStill || vp < kStill || ds[0] * dp[0] + ds[1] * dp[1] < kSudden)
                sudden = true;
        }
        if (refused || air > kMostAirFrames || !sudden)
            continue;
        return i;
    }
    return -1;
}

const char* choose_reason(bool uber_frig, const char* path, float ground_min_y)
{
    if (uber_frig)
        return "uber-frig";
    if (path)
        return path;
    return ground_min_y < kSlopeNormalY ? "slope" : nullptr;
}

bool get_on()
{
    static const bool kOn = config_int("debug", "collision_events", 0) != 0;
    return kOn;
}

void note_path(SkaterPhysicsModule* component, RouteWay path, const maths::Vec4&)
{
    if (component != g_component || !get_on()) // (g_component: set by end_frame, only while on)
        return;
    PathRecord& record = g_paths[g_path_next];
    g_path_next = (g_path_next + 1) % kPathRecords;
    if (g_path_count < kPathRecords)
        ++g_path_count;
    record.frame = frame_loop::frames_run();
    record.path = path;
    record.face = face_of(component->feeler_);
    record.ray = line_of(component->feeler_);
}

void checkpoint(SkaterPhysicsModule* component, const char* label)
{
    if (component != g_component || !get_on()) // (g_component: set by end_frame, only while on)
        return;
    Mark& mark = g_marks[g_mark_next];
    g_mark_next = (g_mark_next + 1) % kMarks;
    if (g_marker_total < kMarks)
        ++g_marker_total;
    mark.frame = frame_loop::frames_run();
    mark.label = label;
    copy3(mark.vel, component->entity_->vel_);
    mark.ray = line_of(component->feeler_);
}

void end_frame(SkaterPhysicsModule* component)
{
    if (frame_loop::frames_run() == 0 || !get_on()) // (not in the early self-tests)
        return;
    if (component != g_component) {
        // (only the local skater's component: not a self-test's stand-in)
        if (object_of(component) != local_skater())
            return;
        g_component = component;
        get_reset();
    }
    // A line once a level, so the log shows the switch was on; the level's files noted for the events.
    static uint32_t s_logged_load = 0;
    uint32_t load = current_load_state();
    if (load != s_logged_load) {
        s_logged_load = load;
        std::string level = get_level_name();
        g_col_file = level_file_stamp(level, "col");
        g_scn_file = level_file_stamp(level, "scn");
        get_log("collision-event: logging on in %s (col %s, scn %s)", level.c_str(), g_col_file.c_str(),
                g_scn_file.c_str());
    }
    objects::GameEntity* object = object_of(component);
    Sample s = {};
    s.frame = frame_loop::frames_run();
    s.state = component->state_->cur_phase_;
    s.physics = component->state_->physics_mode_;
    s.trick = component->state_->in_trick_move_ != 0;
    objects::RiderTeeterMoves* balance = component->balance_;
    s.balance = balance ? balance->trick_ : 0;
    if (objects::PadInputModule* input = component->input_) {
        // The pad's 16 buttons (0x20 bytes each from +0x20 - the first is up) and its scaled first stick.
        const objects::ControlButton* buttons = &input->pad_primary_.up_dir_;
        for (int i = 0; i < 16; ++i)
            if (buttons[i].held_down_)
                s.buttons = static_cast<uint16_t>(s.buttons | (1u << i));
        s.stick[0] = input->pad_primary_.scaled_stick_[0];
        s.stick[1] = input->pad_primary_.scaled_stick_[1];
    }
    copy3(s.pos, object->at_point_);
    copy3(s.vel, object->vel_);
    copy3(s.facing, object->basis_frame_.row[2]);
    copy3(s.ground_normal_, component->floor_normal_);
    s.ground = face_of(component->floor_feeler_);
    s.ground_ray_ = line_of(component->floor_feeler_);
    if (g_sample_total) {
        const Sample& last = sample_at(0);
        float dx = s.pos[0] - last.pos[0], dy = s.pos[1] - last.pos[1], dz = s.pos[2] - last.pos[2];
        if (s.frame == last.frame) { // (the same frame again: the newer replaces it)
            g_next = (g_next - 1 + kHistory) % kHistory;
            --g_sample_total;
        } else if (dx * dx + dy * dy + dz * dz > kTeleport * kTeleport || s.frame < last.frame)
            get_reset();
    }
    g_samples[g_next] = s;
    g_next = (g_next + 1) % kHistory;
    if (g_sample_total < kHistory)
        ++g_sample_total;
    detect();
}

// --- the lines' text ---

std::string flag_names(uint16_t flags)
{
    static const char* const kNames[16] = {"skatable",       "not_skatable", "wall_ridable",   "vert",
                                           "non_collidable", "decal",        "trigger",        "camera_passes",
                                           "no_shadow",      "shadow",       "no_shadow_wall", "under_ok",
                                           "invisible",      "0x2000",       "0x4000",         "0x8000"};
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

std::string pad_button_labels(uint16_t mask)
{
    static const char* const kNames[16] = {"up", "down", "left",   "right",  "l1",       "l2", "l3",    "r1",
                                           "r2", "r3",   "circle", "square", "triangle", "x",  "start", "select"};
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

std::string face_text(const Face& face, std::string (*name)(uint32_t))
{
    if (!face.hit)
        return "none";
    char text[256];
    std::snprintf(text, sizeof(text),
                  "%s:%s;face:%d;flags:0x%04X;decoded:%s;terrain:%u;n:%.2f,%.2f,%.2f;at:%.1f,%.1f,%.1f",
                  face.object ? "object" : "sector", name(face.checksum).c_str(), face.index, face.flags,
                  flag_names(face.flags).c_str(), face.terrain, face.normal[0], face.normal[1], face.normal[2],
                  face.point[0], face.point[1], face.point[2]);
    return text;
}

namespace {

std::string v3(const float* v, const char* format = "%.1f,%.1f,%.1f")
{
    char text[96];
    std::snprintf(text, sizeof(text), format, v[0], v[1], v[2]);
    return text;
}

} // namespace

std::string format_event(const Occurrence& e, std::string (*name)(uint32_t))
{
    static const char* const kProbes[6] = {"probe_down_hit",        "probe_down_hit_all", "probe_down_beyond",
                                           "probe_down_beyond_all", "probe_ahead",        "probe_ahead_all"};
    char head[160];
    std::snprintf(head, sizeof(head), "collision-event: n=%d level=%s frame=%u reason=%s paths=%s frames=%d", e.number,
                  e.level.empty() ? "-" : e.level.c_str(), e.frame, e.reason, e.paths.c_str(), e.frames);
    std::string line = head;
    line += " spot=" + v3(e.spot);
    line += " pos_before=" + v3(e.pos_before_);
    line += " pos=" + v3(e.pos);
    line += " vel_before=" + v3(e.vel_before_);
    line += " vel_after=" + v3(e.vel_after_);
    char text[96];
    std::snprintf(text, sizeof(text), " heading_before=%.1f heading=%.1f", e.heading_before_, e.heading);
    line += text;
    line += " ground_n=" + v3(e.ground_normal_, "%.2f,%.2f,%.2f");
    std::snprintf(text, sizeof(text), " ground_min_y=%.2f", e.ground_min_y_);
    line += text;
    line += " face=" + face_text(e.face, name);
    for (int i = 0; i < 6; ++i)
        line += std::string(" ") + kProbes[i] + "=" + face_text(e.probes[i], name);
    line += " probe_dir=" + v3(e.probe_dir_, "%.2f,%.2f,%.2f");
    line += " flipped=" + (e.flipped.empty() ? std::string("-") : e.flipped);
    line += " repeats=" + std::to_string(e.repeats);
    line += " screenshot=" + (e.screenshot.empty() ? std::string("-") : e.screenshot);
    return line;
}

std::string format_detail(const Detail& d)
{
    char text[256];
    std::snprintf(text, sizeof(text),
                  "collision-event-detail: n=%d state=%s physics=%s speed=%.1f trick=%d balance=%s input=%s "
                  "input_window=%s stick=%.2f,%.2f",
                  d.number, d.state, d.physics, d.speed, d.trick ? 1 : 0, d.balance, d.input.c_str(),
                  d.input_window_.c_str(), d.stick[0], d.stick[1]);
    std::string line = text;
    line += " ray=" + (d.ray ? v3(d.probe_origin_) + ">" + v3(d.ray_end_) : std::string("-"));
    if (d.geometry) {
        line += " corners=" + v3(d.corners[0]) + ";" + v3(d.corners[1]) + ";" + v3(d.corners[2]);
        line += " col_box=" + v3(d.bounds_low_) + ">" + v3(d.bounds_high_);
    } else {
        line += " corners=- col_box=-";
    }
    if (d.edge >= 0) {
        std::snprintf(text, sizeof(text), " edge=%d edge_dist=%.1f open_edge=%s", d.edge, d.edge_distance_,
                      d.edge_open_ < 0 ? "-"
                      : d.edge_open_   ? "1"
                                       : "0");
        line += text;
    } else {
        line += " edge=- edge_dist=- open_edge=-";
    }
    line += " col_file=" + d.col_file_ + " scn_file=" + d.scn_file_;
    line += " approach=" + (d.approach.empty() ? std::string("-") : d.approach);
    line += " camera=" + (d.camera ? v3(d.camera_pos_) + ">" + v3(d.camera_dir_, "%.3f,%.3f,%.3f") : std::string("-"));
    return line;
}

} // namespace skater::collision_events
