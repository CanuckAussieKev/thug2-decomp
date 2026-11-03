// NOT ORIGINAL - [debug] collision_events: the skater's velocity turning round, logged with its cause (see
// collision_events.h).

#include "game/rider/collision_events.h"
#include "engine/core/enum_ops.h"
#include "game/rider/core_physics.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <array>

#include "engine/maths/vec4.h"
#include "engine/objects/control_pad.h"
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
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace skater::collision_events {

namespace {
constexpr std::size_t kSpotCapacity = 32;
constexpr std::size_t kShortTextBytes = 16;
constexpr std::size_t kLoadPrefixLength = 5;      // "Load_"
constexpr std::size_t kFileTextBytes = 64;
constexpr int kHighWordShift = 32;
constexpr double kDegreesPerHalfTurn = 180.0;
constexpr double kPiDecimal = 3.14159265358979;
constexpr float kSamePointDistanceSq = 0.25f;     // two hits closer than this (squared, in feet) are one spot
constexpr float kFloorNormalMinY = 0.5f;          // a face leaning less than this is not a floor
constexpr float kHalf = 0.5f;
constexpr float kProbeOffsetDistance = 6.0f;      // how far from the edge the probe pair is placed
constexpr int kStateNameCount = 6;
constexpr std::size_t kLineTextBytes = 160;
constexpr float kJitterFraction = 0.02f;
constexpr std::size_t kNoteBytes = 64;
constexpr int kHistoryOldestAge = 50;             // frames back the history is shown from
constexpr int kHistoryAgeStep = 10;
constexpr std::size_t kAgeTextBytes = 64;
constexpr std::size_t kNoticeBytes = 96;
constexpr int kPadButtonCount = 16;
constexpr unsigned kPadButtonStride = 0x20u;
constexpr std::size_t kReportBytes = 256;
constexpr std::size_t kReportLineBytes = 96;
constexpr std::size_t kHeadBytes = 160;

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
    std::array<float, 3> start;
    std::array<float, 3> end;
};

struct Sample {
    uint32_t frame{};
    int state{}, physics{};
    bool trick{};
    uint32_t balance{};
    uint16_t buttons{};
    std::array<float, 2> stick{};
    std::array<float, 3> pos{}, vel{}, facing{}, ground_normal_{};
    Face ground;
    Ray ground_ray_{};
};

struct PathRecord {
    uint32_t frame{};
    RouteWay path{};
    Face face;
    Ray ray{};
};

// NOT ORIGINAL grouping - collision_events.cpp's state (the original's globals). Made on first use, never destroyed
// (collision_events_state()).
struct CollisionEventsState {
    void* component_ = nullptr;
    std::array<Sample, kHistory> samples_{};
    int sample_total_ = 0; // (a ring: collision_events_state().next_ the slot for the next sample)
    int next_ = 0;
    std::array<PathRecord, kPathRecords> paths_{};
    int path_count_ = 0;
    int path_next_ = 0;
    int events_ = 0;
};

CollisionEventsState& collision_events_state()
{
    static core::NoDestroy<CollisionEventsState> state;
    return state.get();
}

std::string& col_file_stamp()
{
    static std::string value = "-";
    return value;
}
std::string& scn_file_stamp()
{
    static std::string value = "-";
    return value;
}

// The checkpoints (a ring).
constexpr int kMarks = 512;
struct Mark {
    uint32_t frame;
    const char* label;
    std::array<float, 3> vel;
    Ray ray; // (the core's feeler's line there)
};
// NOT ORIGINAL - the marks' ring: the marks, how many were made and the slot for the next. Made on first use, never
// destroyed (mark_ring()).
struct MarkRing {
    std::array<Mark, kMarks> marks_{};
    int marker_total_ = 0;
    int mark_next_ = 0;
};

MarkRing& mark_ring()
{
    static core::NoDestroy<MarkRing> state;
    return state.get();
}

struct Spot {
    std::array<float, 3> at;
    DWORD time;
    int repeats;
};
// NOT ORIGINAL - the spots' ring and the slot for the next. Made on first use, never destroyed (spot_ring()).
struct SpotRing {
    std::array<Spot, kSpotCapacity> spots_{};
    int spot_next_ = 0;
};
SpotRing& spot_ring()
{
    static core::NoDestroy<SpotRing> state;
    return state.get();
}

void copy3(std::array<float, 3>& to, const maths::Vec4& from) { to = {from.x, from.y, from.z}; }

Ray line_of(const render::ProbeRay& feeler)
{
    Ray ray{};
    copy3(ray.start, feeler.line().start);
    copy3(ray.end, feeler.line().end);
    return ray;
}

Face face_of(const render::ProbeRay& feeler)
{
    Face face;
    face.hit = feeler.hit().hit;
    if (!face.hit)
        return face;
    face.object = feeler.entity().get_pull() != nullptr;
    face.checksum = face.object ? feeler.entity_key() : feeler.hit().object_checksum_;
    face.index = static_cast<int>(feeler.hit().face);
    face.flags = core::to_underlying(feeler.hit().face_flags_);
    face.terrain = feeler.hit().terrain;
    face.coll_obj_ = feeler.hit().object;
    copy3(face.point, feeler.contact_pos());
    copy3(face.normal, feeler.surface_dir());
    return face;
}

std::string get_name_of(uint32_t checksum)
{
    const char* name = script::kept_qb_name(checksum);
    if (!name)
        name = script::look_up_hash_name(checksum);
    std::array<char, kShortTextBytes> text{};
    if (!name) {
        text::format_to(text, "0x%08X", checksum);
        return text.data();
    }
    std::string out = name;
    for (char& c : out)
        if (c == ' ' || c == '=' || c == ';')
            c = '_';
    return out;
}

// The level loaded: its load script's name (the skate module's +0x378) without "Load_".
uint32_t current_load_state() { return modules::Skate::sole() ? modules::Skate::sole()->level() : 0; }
std::string get_level_name()
{
    const uint32_t load = current_load_state();
    if (!load)
        return "-";
    std::string name = get_name_of(load);
    if (name.size() > kLoadPrefixLength && _strnicmp(name.c_str(), "Load_", kLoadPrefixLength) == 0)
        name.erase(0, kLoadPrefixLength);
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
    std::array<char, kFileTextBytes> text{};
    text::format_to(text, "%llu@%04u-%02u-%02uT%02u:%02u:%02u",
                    (static_cast<unsigned long long>(data.nFileSizeHigh) << kHighWordShift) | data.nFileSizeLow, t.wYear, t.wMonth,
                    t.wDay, t.wHour, t.wMinute, t.wSecond);
    return text.data();
}

// The level's collision / scene file: the data overlay's converted one, else the game's own packed one.
std::string level_file_stamp(const std::string& level, const char* kind)
{
    const std::string overlay = "thug2-decomp-data\\levels\\" + level + "\\" + level + "." + kind + ".xbx";
    const std::string packed = "Data\\pre\\" + level + kind + ".prx";
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
    auto* module = modules::Skate::sole();
    return module ? module->get_home_rider() : nullptr;
}

float get_horizontal(const std::array<float, 3>& v, std::array<float, 2>& direction)
{
    const float x = v.front();
    const float z = v.back();
    const float length = std::sqrt(x * x + z * z);
    direction = {length > 0.0f ? x / length : 0.0f, length > 0.0f ? z / length : 0.0f};
    return length;
}

float get_heading_of(const std::array<float, 3>& facing)
{
    return static_cast<float>(std::atan2(static_cast<double>(facing.front()), static_cast<double>(facing.back())) * kDegreesPerHalfTurn /
                              kPiDecimal);
}

const Sample& sample_at(int age) // 0: the newest
{
    return core::at(collision_events_state().samples_, ((collision_events_state().next_ - 1 - age) % kHistory + kHistory) % kHistory);
}

void get_reset()
{
    collision_events_state().sample_total_ = collision_events_state().next_ = 0;
    collision_events_state().path_count_ = collision_events_state().path_next_ = 0;
    mark_ring().marker_total_ = mark_ring().mark_next_ = 0;
}

// A probe line: as the skater's feelers see (faces flagged 0x10 skipped), or every face.
Face get_probe(const std::array<float, 3>& from, const std::array<float, 3>& to, bool everything)
{
    const maths::Vec4 start = {.x = std::get<0>(from), .y = std::get<1>(from), .z = std::get<2>(from), .w = 1.0f};
    const maths::Vec4 end = {.x = std::get<0>(to), .y = std::get<1>(to), .z = std::get<2>(to), .w = 1.0f};
    render::ProbeRay ray_storage(&start, &end);
    render::ProbeRay* feeler = &ray_storage;
    feeler->set_excluded({.ignore = everything ? render::kNoFaceFlags : render::kDefaultIgnoredFaceFlags, .require = render::kNoFaceFlags});
    feeler->get_hit_shape(true, false);
    return face_of(*feeler);
}

// Whether to leave this spot out (again within a second); `repeats`: those left out since its last line.
bool quiet_spot(const std::array<float, 3>& at, int* repeats)
{
    const DWORD now = GetTickCount();
    for (Spot& spot : spot_ring().spots_) {
        if (!spot.time)
            continue;
        const float dx = core::at(spot.at, 0) - std::get<0>(at);
        const float dy = core::at(spot.at, 1) - std::get<1>(at);
        const float dz = core::at(spot.at, 2) - std::get<2>(at);
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
    Spot& spot = core::at(spot_ring().spots_, spot_ring().spot_next_);
    spot_ring().spot_next_ = (spot_ring().spot_next_ + 1) % static_cast<int>(sizeof(spot_ring().spots_) / sizeof(core::at(spot_ring().spots_, 0)));
    spot.at = at;
    spot.time = now ? now : 1;
    spot.repeats = 0;
    *repeats = 0;
    return false;
}

float span_to(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return std::sqrt((core::at(a, 0) - core::at(b, 0)) * (core::at(a, 0) - core::at(b, 0)) + (core::at(a, 1) - core::at(b, 1)) * (core::at(a, 1) - core::at(b, 1)) + (core::at(a, 2) - core::at(b, 2)) * (core::at(a, 2) - core::at(b, 2)));
}

// The distance from p to the segment a-b.
float to_segment(const std::array<float, 3>& p, const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    const std::array<float, 3> e = {{core::at(b, 0) - core::at(a, 0), core::at(b, 1) - core::at(a, 1), core::at(b, 2) - core::at(a, 2)}};
    const std::array<float, 3> w = {{core::at(p, 0) - core::at(a, 0), core::at(p, 1) - core::at(a, 1), core::at(p, 2) - core::at(a, 2)}};
    const float ee = core::at(e, 0) * core::at(e, 0) + core::at(e, 1) * core::at(e, 1) + core::at(e, 2) * core::at(e, 2);
    float t = ee > 0.0f ? (core::at(w, 0) * core::at(e, 0) + core::at(w, 1) * core::at(e, 1) + core::at(w, 2) * core::at(e, 2)) / ee : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const std::array<float, 3> q = {{core::at(a, 0) + core::at(e, 0) * t, core::at(a, 1) + core::at(e, 1) * t, core::at(a, 2) + core::at(e, 2) * t}};
    return span_to(p, q);
}

// A static face's corners, its object's box and the edge nearest `spot` - open when no other face of its object has
// both its corners and a probe just beyond its middle finds nothing.
void face_geometry(const Face& face, const std::array<float, 3>& spot, Detail* detail)
{
    if (!face.hit || face.object || !face.coll_obj_)
        return;
    auto* object = static_cast<render::HitMesh*>(face.coll_obj_);
    const render::TriMeshSource* geometry = object->geometry();
    if (!geometry || face.index < 0 || std::cmp_greater_equal(face.index, geometry->tri_count()))
        return;
    std::array<uint32_t, 3> index{};
    std::array<maths::Vec4, 3> raw{};
    for (int k = 0; k < 3; ++k) {
        core::at(index, k) = geometry->face_vertex(face.index, k);
        core::at(raw, k) = geometry->read_stored_vertex(static_cast<int>(core::at(index, k)));
        const maths::Vec4 world = object->read_vertex_point(static_cast<int>(core::at(index, k))); // (a vertex in the world)
        copy3(core::at(detail->corners, k), world);
    }
    detail->geometry = true;
    copy3(detail->bounds_low_, geometry->bounds().min);
    copy3(detail->bounds_high_, geometry->bounds().max);
    for (int k = 0; k < 3; ++k) {
        const float d = to_segment(spot, core::at(detail->corners, k), core::at(detail->corners, (k + 1) % 3));
        if (detail->edge < 0 || d < detail->edge_distance_) {
            detail->edge = k;
            detail->edge_distance_ = d;
        }
    }
    const int a = detail->edge;
    const int b = (a + 1) % 3;
    const int c = (a + 2) % 3;
    auto same = [&](uint32_t vertex, int k) {
        if (vertex == core::at(index, k))
            return true;
        const maths::Vec4 v = geometry->read_stored_vertex(static_cast<int>(vertex));
        const float dx = v.x - core::at(raw, k).x;
        const float dy = v.y - core::at(raw, k).y;
        const float dz = v.z - core::at(raw, k).z;
        return dx * dx + dy * dy + dz * dz < kSamePointDistanceSq;
    };
    for (int j = 0; std::cmp_less(j, geometry->tri_count()); ++j) {
        if (j == face.index)
            continue;
        bool has_a = false, has_b = false;
        for (int m = 0; m < 3; ++m) {
            const uint32_t v = geometry->face_vertex(j, m);
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
    if (core::at(face.normal, 1) < kFloorNormalMinY)
        return;
    const std::array<float, 3>& pa = core::at(detail->corners, a);
    const std::array<float, 3>& pb = core::at(detail->corners, b);
    const std::array<float, 3>& pc = core::at(detail->corners, c);
    const std::array<float, 3> e = {{core::at(pb, 0) - core::at(pa, 0), core::at(pb, 1) - core::at(pa, 1), core::at(pb, 2) - core::at(pa, 2)}};
    const std::array<float, 3> w = {{core::at(pc, 0) - core::at(pa, 0), core::at(pc, 1) - core::at(pa, 1), core::at(pc, 2) - core::at(pa, 2)}};
    const float ee = core::at(e, 0) * core::at(e, 0) + core::at(e, 1) * core::at(e, 1) + core::at(e, 2) * core::at(e, 2);
    const float t = ee > 0.0f ? (core::at(w, 0) * core::at(e, 0) + core::at(w, 1) * core::at(e, 1) + core::at(w, 2) * core::at(e, 2)) / ee : 0.0f;
    const std::array<float, 3> out = {{-(core::at(w, 0) - core::at(e, 0) * t), -(core::at(w, 1) - core::at(e, 1) * t), -(core::at(w, 2) - core::at(e, 2) * t)}};
    const float length = std::sqrt(core::at(out, 0) * core::at(out, 0) + core::at(out, 1) * core::at(out, 1) + core::at(out, 2) * core::at(out, 2));
    if (length <= 0.0f)
        return;
    std::array<float, 3> q{};
    for (int k = 0; k < 3; ++k)
        core::at(q, k) = (core::at(pa, k) + core::at(pb, k)) * kHalf + core::at(out, k) / length * kProbeOffsetDistance;
    const std::array<float, 3> high = {core::at(q, 0), core::at(q, 1) + 24.0f, core::at(q, 2)};
    const std::array<float, 3> low = {core::at(q, 0), core::at(q, 1) - 48.0f, core::at(q, 2)};
    detail->edge_open_ = get_probe(high, low, false).hit ? 0 : 1;
}

const char* get_state_name(int state)
{
    static const auto kNames = std::to_array<const char*>({"ground", "air", "wallride", "lip", "rail", "wallplant"});
    return state >= 0 && state < kStateNameCount ? core::at(kNames, state) : "-";
}
char state_letter(int state)
{
    constexpr std::string_view kLetters = "gawlrp";
    return state >= 0 && state < kStateNameCount ? core::at(kLetters, state) : '?';
}

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
    event.pos_before_ = old.pos;
    event.pos = now.pos;
    event.vel_before_ = old.vel;
    event.vel_after_ = now.vel;
    event.ground_normal_ = now.ground_normal_;
    event.heading_before_ = get_heading_of(old.facing);
    event.heading = get_heading_of(now.facing);

    // The paths that ran between the two (in order), and the strongest.
    const PathRecord* strongest = nullptr;
    std::array<bool, static_cast<int>(RouteWay::kTotalOf)> seen = {};
    for (int i = 0; i < collision_events_state().path_count_; ++i) {
        const PathRecord& record = core::at(collision_events_state().paths_, (collision_events_state().path_next_ - collision_events_state().path_count_ + i + kPathRecords) % kPathRecords);
        if (record.frame < old.frame || record.frame > now.frame)
            continue;
        const int p = static_cast<int>(record.path);
        if (!core::at(seen, p)) {
            core::at(seen, p) = true;
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
    std::array<float, 2> dir_old{};
    get_horizontal(old.vel, dir_old);
    const Mark* previous = nullptr;
    const Mark* flip = nullptr;
    bool uber_frig = false;
    event.flipped = "-";
    for (int i = 0; i < mark_ring().marker_total_; ++i) {
        const Mark& mark = core::at(mark_ring().marks_, (mark_ring().mark_next_ - mark_ring().marker_total_ + i + kMarks) % kMarks);
        if (mark.frame < old.frame || mark.frame > now.frame)
            continue;
        const bool against = core::at(mark.vel, 0) * core::at(dir_old, 0) + core::at(mark.vel, 2) * core::at(dir_old, 1) < 0.0f;
        if (against && previous) {
            std::array<char, kLineTextBytes> text{};
            text::format_to(text, "%s>%s@%u", previous->label, mark.label, mark.frame);
            event.flipped = text.data();
            flip = &mark;
            // Between one frame's physics and the next, the velocity exactly negated: the skater's adjust-physics
            // component (0x004F8420, not rewritten) found no collision within uber_frig_current_height feet below
            // the skater and put it back where the frame began (the core's +0x98) going the other way - the "uber
            // frig", as the walking code calls its own (walk_collision.cpp update_ground_below). Its down feeler is
            // the core's own: its line is still there at the next frame's start.
            float sum = 0.0f, size = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += (core::at(mark.vel, k) + core::at(previous->vel, k)) * (core::at(mark.vel, k) + core::at(previous->vel, k));
                size += core::at(previous->vel, k) * core::at(previous->vel, k);
            }
            if (std::strcmp(previous->label, "update-rest") == 0 && std::strcmp(mark.label, "update-start") == 0 &&
                std::sqrt(sum) < kJitterFraction * std::sqrt(size) + 1.0f)
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
        if (s.state == 0 && core::at(s.ground_normal_, 1) < core::at(steepest->ground_normal_, 1))
            steepest = &s;
        if (s.state == 0 && core::at(s.ground_normal_, 1) < event.ground_min_y_)
            event.ground_min_y_ = core::at(s.ground_normal_, 1);
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
        std::memcpy(event.spot.data(), event.face.point.data(), sizeof(event.spot));
    else
        event.spot = now.pos;
    if (quiet_spot(event.spot, &event.repeats))
        return;

    // The probes, along the travel before the reversal.
    std::array<float, 2> direction{};
    if (get_horizontal(old.vel, direction) <= 0.0f)
        get_horizontal(old.facing, direction);
    core::at(event.probe_dir_, 0) = core::at(direction, 0);
    core::at(event.probe_dir_, 2) = core::at(direction, 1);
    const std::array<float, 3>& at = event.spot;
    const std::array<float, 3> high = {{std::get<0>(at), std::get<1>(at) + 48.0f, std::get<2>(at)}};
    const std::array<float, 3> low = {{std::get<0>(at), std::get<1>(at) - 200.0f, std::get<2>(at)}};
    const std::array<float, 3> beyond = {{std::get<0>(at) + core::at(direction, 0) * 24.0f, std::get<1>(at), std::get<2>(at) + core::at(direction, 1) * 24.0f}};
    const std::array<float, 3> beyond_high = {core::at(beyond, 0), core::at(beyond, 1) + 48.0f, core::at(beyond, 2)};
    const std::array<float, 3> beyond_low = {core::at(beyond, 0), core::at(beyond, 1) - 200.0f, core::at(beyond, 2)};
    const float lift = script::fetch_float(kForwardHeight);
    std::array<float, 3> ahead_from = {{core::at(old.pos, 0), core::at(old.pos, 1) + lift, core::at(old.pos, 2)}};
    const std::array<float, 3> ahead_to = {{core::at(ahead_from, 0) + core::at(direction, 0) * 120.0f, core::at(ahead_from, 1), core::at(ahead_from, 2) + core::at(direction, 1) * 120.0f}};
    for (int everything = 0; everything < 2; ++everything) {
        core::at(event.probes, 0 + everything) = get_probe(high, low, everything != 0);
        core::at(event.probes, 2 + everything) = get_probe(beyond_high, beyond_low, everything != 0);
        core::at(event.probes, 4 + everything) = get_probe(ahead_from, ahead_to, everything != 0);
    }

    event.number = ++collision_events_state().events_;
    event.level = get_level_name();
    static const bool kShots = config_int("debug", "collision_event_screenshots", 1) != 0;
    event.screenshot = "-";
    if (kShots) {
        std::array<char, kNoteBytes> note{};
        text::format_to(note, "collision event %d", event.number);
        event.screenshot = sys::screenshot::request_event_shot(note.data()) ? "requested" : "busy";
    }

    // The detail line.
    detail.number = event.number;
    detail.state = get_state_name(now.state);
    detail.physics = now.physics == 0 ? "skating" : now.physics == 1 ? "walking" : "-";
    detail.speed = std::sqrt(core::at(now.vel, 0) * core::at(now.vel, 0) + core::at(now.vel, 1) * core::at(now.vel, 1) + core::at(now.vel, 2) * core::at(now.vel, 2));
    detail.trick = now.trick;
    detail.balance = balance_name(now.balance);
    detail.input = pad_button_labels(now.buttons);
    detail.input_window_ = pad_button_labels(held);
    core::at(detail.stick, 0) = core::at(now.stick, 0);
    core::at(detail.stick, 1) = core::at(now.stick, 1);
    if (ray) {
        detail.ray = true;
        std::memcpy(detail.probe_origin_.data(), ray->start.data(), sizeof(detail.probe_origin_));
        std::memcpy(detail.ray_end_.data(), ray->end.data(), sizeof(detail.ray_end_));
    }
    face_geometry(event.face, event.spot, &detail);
    detail.col_file_ = col_file_stamp();
    detail.scn_file_ = scn_file_stamp();
    detail.approach.clear();
    for (int age = kHistoryOldestAge; age >= 0; age -= kHistoryAgeStep) {
        if (age >= collision_events_state().sample_total_)
            continue;
        const Sample& s = sample_at(age);
        std::array<char, kAgeTextBytes> text{};
        text::format_to(text, "%s%.0f,%.0f,%.0f,%c", detail.approach.empty() ? "" : ";", core::at(s.pos, 0),
                        core::at(s.pos, 1), core::at(s.pos, 2), state_letter(s.state));
        detail.approach += text.data();
    }
    detail.camera = sys::screenshot::last_view(detail.camera_pos_, detail.camera_dir_);

    get_log("%s", format_event(event, &get_name_of).c_str());
    get_log("%s", format_detail(detail).c_str());
    // (on screen too, for 2 s: a newer one replaces it)
    std::array<char, kNoticeBytes> notice{};
    text::format_to(notice, "collision event %d: %s%s", event.number, event.reason,
                    event.screenshot == "requested" ? " (screenshot)" : "");
    sys::screenshot::show_notice(notice.data());
}

void detect()
{
    std::array<Motion, kHistory> frames;
    for (int i = 0; i < collision_events_state().sample_total_; ++i) {
        const Sample& s = sample_at(collision_events_state().sample_total_ - 1 - i);
        core::at(frames, i).frame = s.frame;
        core::at(frames, i).state = s.state;
        core::at(frames, i).vel = s.vel;
    }
    const int index = find_reversal(std::span<const Motion>(frames).first(static_cast<std::size_t>(collision_events_state().sample_total_)));
    if (index < 0)
        return;
    get_emit(collision_events_state().sample_total_ - 1 - index);
    // (seen once - logged or not: the history starts again from this frame)
    const Sample last = sample_at(0);
    collision_events_state().sample_total_ = 1;
    collision_events_state().next_ = 1;
    core::at(collision_events_state().samples_, 0) = last;
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

int find_reversal(std::span<const Motion> frames)
{
    const auto count = static_cast<int>(frames.size());
    if (count < 2)
        return -1;
    const Motion& now = core::at(frames, count - 1);
    if (now.state != 0)
        return -1;
    std::array<float, 2> dir_now{};
    if (get_horizontal(now.vel, dir_now) < kMinSpeedNow)
        return -1;
    for (int i = 0; i < count - 1; ++i) { // (the oldest first: the speed the skater had before)
        const Motion& old = core::at(frames, i);
        if (now.frame - old.frame > static_cast<uint32_t>(kWindow) || old.state != 0)
            continue;
        std::array<float, 2> dir_old{};
        if (get_horizontal(old.vel, dir_old) < kMinSpeedBefore ||
            core::at(dir_old, 0) * core::at(dir_now, 0) + core::at(dir_old, 1) * core::at(dir_now, 1) >= kOpposed)
            continue;
        // A sudden change between, no rail / lip / wall, not long in the air.
        bool sudden = false, refused = false;
        int air = 0;
        for (int j = i + 1; j < count; ++j) {
            const Motion& s = core::at(frames, j);
            const Motion& p = core::at(frames, j - 1);
            if (s.state == 1)
                ++air;
            else if (s.state != 0)
                refused = true;
            std::array<float, 2> ds{};
            std::array<float, 2> dp{};
            const float vs = get_horizontal(s.vel, ds);
            const float vp = get_horizontal(p.vel, dp);
            if (vs < kStill || vp < kStill || core::at(ds, 0) * core::at(dp, 0) + core::at(ds, 1) * core::at(dp, 1) < kSudden)
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
    if (component != collision_events_state().component_ || !get_on()) // (collision_events_state().component_: set by end_frame, only while on)
        return;
    PathRecord& record = core::at(collision_events_state().paths_, collision_events_state().path_next_);
    collision_events_state().path_next_ = (collision_events_state().path_next_ + 1) % kPathRecords;
    if (collision_events_state().path_count_ < kPathRecords)
        ++collision_events_state().path_count_;
    record.frame = frame_loop::frames_run();
    record.path = path;
    record.face = face_of(component->feeler());
    record.ray = line_of(component->feeler());
}

void checkpoint(SkaterPhysicsModule* component, const char* label)
{
    if (component != collision_events_state().component_ || !get_on()) // (collision_events_state().component_: set by end_frame, only while on)
        return;
    Mark& mark = core::at(mark_ring().marks_, mark_ring().mark_next_);
    mark_ring().mark_next_ = (mark_ring().mark_next_ + 1) % kMarks;
    if (mark_ring().marker_total_ < kMarks)
        ++mark_ring().marker_total_;
    mark.frame = frame_loop::frames_run();
    mark.label = label;
    copy3(mark.vel, component->entity()->vel());
    mark.ray = line_of(component->feeler());
}

void end_frame(SkaterPhysicsModule* component)
{
    if (frame_loop::frames_run() == 0 || !get_on()) // (not in the early self-tests)
        return;
    if (component != collision_events_state().component_) {
        // (only the local skater's component: not a self-test's stand-in)
        if (object_of(component) != local_skater())
            return;
        collision_events_state().component_ = component;
        get_reset();
    }
    // A line once a level, so the log shows the switch was on; the level's files noted for the events.
    static uint32_t s_logged_load = 0;
    const uint32_t load = current_load_state();
    if (load != s_logged_load) {
        s_logged_load = load;
        const std::string level = get_level_name();
        col_file_stamp() = level_file_stamp(level, "col");
        scn_file_stamp() = level_file_stamp(level, "scn");
        get_log("collision-event: logging on in %s (col %s, scn %s)", level.c_str(), col_file_stamp().c_str(),
                scn_file_stamp().c_str());
    }
    const objects::GameEntity* object = object_of(component);
    Sample s = {};
    s.frame = frame_loop::frames_run();
    s.state = component->state()->cur_phase();
    s.physics = component->state()->physics_mode();
    s.trick = component->state()->in_trick_move() != 0;
    const objects::RiderTeeterMoves* balance = component->balance();
    s.balance = balance ? balance->trick() : 0;
    if (objects::PadInputModule* input = component->input()) {
        // The pad's 16 buttons (0x20 bytes each from +0x20 - the first is up) and its scaled first stick.
        for (int i = 0; i < kPadButtonCount; ++i)
            if (const objects::ControlButton* button = input->pad_primary().button_at_offset(static_cast<uint32_t>(i) * kPadButtonStride);
                button && button->held_down())
                s.buttons = static_cast<uint16_t>(s.buttons | (1u << i));
        core::at(s.stick, 0) = core::at(input->pad_primary().scaled_stick(), 0);
        core::at(s.stick, 1) = core::at(input->pad_primary().scaled_stick(), 1);
    }
    copy3(s.pos, object->at_point());
    copy3(s.vel, object->vel());
    copy3(s.facing, core::at(object->basis_frame().row, 2));
    copy3(s.ground_normal_, component->floor_normal());
    s.ground = face_of(component->floor_feeler());
    s.ground_ray_ = line_of(component->floor_feeler());
    if (collision_events_state().sample_total_) {
        const Sample& last = sample_at(0);
        const float dx = core::at(s.pos, 0) - core::at(last.pos, 0);
        const float dy = core::at(s.pos, 1) - core::at(last.pos, 1);
        const float dz = core::at(s.pos, 2) - core::at(last.pos, 2);
        if (s.frame == last.frame) { // (the same frame again: the newer replaces it)
            collision_events_state().next_ = (collision_events_state().next_ - 1 + kHistory) % kHistory;
            --collision_events_state().sample_total_;
        } else if (dx * dx + dy * dy + dz * dz > kTeleport * kTeleport || s.frame < last.frame)
            get_reset();
    }
    core::at(collision_events_state().samples_, collision_events_state().next_) = s;
    collision_events_state().next_ = (collision_events_state().next_ + 1) % kHistory;
    if (collision_events_state().sample_total_ < kHistory)
        ++collision_events_state().sample_total_;
    detect();
}

// --- the lines' text ---

std::string flag_names(uint16_t flags)
{
    static const std::array<const char*, 16> kNames = {{"skatable",       "not_skatable", "wall_ridable",   "vert",
                                           "non_collidable", "decal",        "trigger",        "camera_passes",
                                           "no_shadow",      "shadow",       "no_shadow_wall", "under_ok",
                                           "invisible",      "0x2000",       "0x4000",         "0x8000"}};
    std::string out;
    for (int bit = 0; bit < kPadButtonCount; ++bit) {
        if (!(flags & (1u << bit)))
            continue;
        if (!out.empty())
            out += '|';
        out += core::at(kNames, bit);
    }
    return out.empty() ? "0" : out;
}

std::string pad_button_labels(uint16_t mask)
{
    static const std::array<const char*, 16> kNames = {{"up", "down", "left",   "right",  "l1",       "l2", "l3",    "r1",
                                           "r2", "r3",   "circle", "square", "triangle", "x",  "start", "select"}};
    std::string out;
    for (int bit = 0; bit < kPadButtonCount; ++bit) {
        if (!(mask & (1u << bit)))
            continue;
        if (!out.empty())
            out += '+';
        out += core::at(kNames, bit);
    }
    return out.empty() ? "-" : out;
}

std::string face_text(const Face& face, std::string (*name)(uint32_t))
{
    if (!face.hit)
        return "none";
    std::array<char, kReportBytes> text{};
    text::format_to(text, "%s:%s;face:%d;flags:0x%04X;decoded:%s;terrain:%u;n:%.2f,%.2f,%.2f;at:%.1f,%.1f,%.1f",
                    face.object ? "object" : "sector", name(face.checksum).c_str(), face.index, face.flags,
                    flag_names(face.flags).c_str(), face.terrain, core::at(face.normal, 0), core::at(face.normal, 1),
                    core::at(face.normal, 2), core::at(face.point, 0), core::at(face.point, 1),
                    core::at(face.point, 2));
    return text.data();
}

namespace {

std::string v3(std::span<const float> v, int places = 1)
{
    std::array<char, kReportLineBytes> text{};
    text::format_to(text, "%.*f,%.*f,%.*f", places, core::at(v, 0), places, core::at(v, 1), places, core::at(v, 2));
    return text.data();
}

} // namespace

std::string format_event(const Occurrence& e, std::string (*name)(uint32_t))
{
    static const std::array<const char*, 6> kProbes = {{"probe_down_hit",        "probe_down_hit_all", "probe_down_beyond",
                                           "probe_down_beyond_all", "probe_ahead",        "probe_ahead_all"}};
    std::array<char, kHeadBytes> head{};
    text::format_to(head, "collision-event: n=%d level=%s frame=%u reason=%s paths=%s frames=%d", e.number,
                    e.level.empty() ? "-" : e.level.c_str(), e.frame, e.reason, e.paths.c_str(), e.frames);
    std::string line = head.data();
    line += " spot=" + v3(e.spot);
    line += " pos_before=" + v3(e.pos_before_);
    line += " pos=" + v3(e.pos);
    line += " vel_before=" + v3(e.vel_before_);
    line += " vel_after=" + v3(e.vel_after_);
    std::array<char, kReportLineBytes> text{};
    text::format_to(text, " heading_before=%.1f heading=%.1f", e.heading_before_, e.heading);
    line += text.data();
    line += " ground_n=" + v3(e.ground_normal_, 2);
    text::format_to(text, " ground_min_y=%.2f", e.ground_min_y_);
    line += text.data();
    line += " face=" + face_text(e.face, name);
    for (int i = 0; i < kProbeFaceLimit; ++i)
        line += std::string(" ") + core::at(kProbes, i) + "=" + face_text(core::at(e.probes, i), name);
    line += " probe_dir=" + v3(e.probe_dir_, 2);
    line += " flipped=" + (e.flipped.empty() ? std::string("-") : e.flipped);
    line += " repeats=" + std::to_string(e.repeats);
    line += " screenshot=" + (e.screenshot.empty() ? std::string("-") : e.screenshot);
    return line;
}

std::string format_detail(const Detail& d)
{
    std::array<char, kReportBytes> text{};
    text::format_to(text,
                    "collision-event-detail: n=%d state=%s physics=%s speed=%.1f trick=%d balance=%s input=%s "
                    "input_window=%s stick=%.2f,%.2f",
                    d.number, d.state, d.physics, d.speed, d.trick ? 1 : 0, d.balance, d.input.c_str(),
                    d.input_window_.c_str(), core::at(d.stick, 0), core::at(d.stick, 1));
    std::string line = text.data();
    line += " ray=" + (d.ray ? v3(d.probe_origin_) + ">" + v3(d.ray_end_) : std::string("-"));
    if (d.geometry) {
        line += " corners=" + v3(core::at(d.corners, 0)) + ";" + v3(core::at(d.corners, 1)) + ";" + v3(core::at(d.corners, 2));
        line += " col_box=" + v3(d.bounds_low_) + ">" + v3(d.bounds_high_);
    } else {
        line += " corners=- col_box=-";
    }
    if (d.edge >= 0) {
        text::format_to(text, " edge=%d edge_dist=%.1f open_edge=%s", d.edge, d.edge_distance_,
                        d.edge_open_ < 0 ? std::string_view("-").data()
                        : d.edge_open_   ? std::string_view("1").data()
                                         : std::string_view("0").data());
        line += text.data();
    } else {
        line += " edge=- edge_dist=- open_edge=-";
    }
    line += " col_file=" + d.col_file_ + " scn_file=" + d.scn_file_;
    line += " approach=" + (d.approach.empty() ? std::string("-") : d.approach);
    line += " camera=" + (d.camera ? v3(d.camera_pos_) + ">" + v3(d.camera_dir_, 3) : std::string("-"));
    return line;
}

} // namespace skater::collision_events
