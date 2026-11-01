// NOT ORIGINAL - the level editor's transform tools (ours; see level_editor_transform.h).

#include "editor/level_editor_transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>

#include "editor/level_editor_prefs.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::xform {

namespace {

constexpr float kPi = 3.14159265f;

Settings g_settings;
Repeat g_repeat;

} // namespace

Settings& at_hand() { return g_settings; }

bool fly_always() { return prefs::get_pull().wasd_flies_; }
void set_fly_always(bool always)
{
    prefs::Values v = prefs::get_pull();
    v.wasd_flies_ = always;
    prefs::assign_value(v);
}
float get_grid() { return static_cast<float>(prefs::get_pull().snap_move_); }
void set_grid(float grid)
{
    prefs::Values v = prefs::get_pull();
    v.snap_move_ = static_cast<int>(grid);
    prefs::assign_value(v);
}
float angle_step() { return static_cast<float>(prefs::get_pull().snap_turn_); }
void set_angle_step(float degrees)
{
    prefs::Values v = prefs::get_pull();
    v.snap_turn_ = static_cast<int>(degrees);
    prefs::assign_value(v);
}

const std::vector<float>& grid_sizes()
{
    static const std::vector<float> kSizes = {1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f};
    return kSizes;
}
const std::vector<float>& angle_steps()
{
    static const std::vector<float> kSteps = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    return kSteps;
}

const char* mode_name(Mode mode)
{
    switch (mode) {
    case Mode::kShiftBy:
        return "Move";
    case Mode::kSpinBy:
        return "Rotate";
    case Mode::kScale:
        return "Scale";
    }
    return "?";
}
const char* pivot_name(Pivot pivot)
{
    switch (pivot) {
    case Pivot::kBase:
        return "Base";
    case Pivot::kCentre:
        return "Centre";
    case Pivot::kOwn:
        return "Own";
    }
    return "?";
}
const char* target_name(SnapTarget target)
{
    switch (target) {
    case SnapTarget::kGrid:
        return "Grid";
    case SnapTarget::kWorldGrid:
        return "World grid";
    case SnapTarget::kSurface:
        return "Surface";
    case SnapTarget::kVertex:
        return "Vertex";
    }
    return "?";
}
Mode next_mode(Mode mode)
{
    return mode == Mode::kShiftBy ? Mode::kSpinBy : mode == Mode::kSpinBy ? Mode::kScale : Mode::kShiftBy;
}

// --- the pure parts ---

void heading_axes(float yaw, V3 out[3])
{
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    out[0] = {c, 0.0f, -s};
    out[1] = {0.0f, 1.0f, 0.0f};
    out[2] = {s, 0.0f, c};
}

float get_heading_of(V3 at) { return std::atan2(at.x, at.z); }

void unit_axes(const V3 rows[3], V3 out[3])
{
    static const std::array<V3, 3> kWorld = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    for (int i = 0; i < 3; ++i)
        out[i] = get_dot(rows[i], rows[i]) > 1e-12f ? make_unit(rows[i]) : core::at(kWorld, i);
}

float turn_around_axis(V3 pivot, V3 point) { return std::atan2(point.x - pivot.x, point.z - pivot.z); }

float get_wrap(float radians)
{
    float r = std::fmod(radians, 2.0f * kPi);
    if (r <= -kPi)
        r += 2.0f * kPi;
    else if (r > kPi)
        r -= 2.0f * kPi;
    return r;
}

float snap_angle(float radians, float step_degrees)
{
    if (step_degrees <= 0.0f)
        return radians;
    const float step = step_degrees * kPi / 180.0f;
    return std::round(radians / step) * step;
}

int get_quarters(float radians) { return static_cast<int>(std::lround(radians / (kPi * 0.5f))); }

float snap_relative(float distance, float grid) { return grid > 0.0f ? std::round(distance / grid) * grid : distance; }

float factor_along(float s0, float s)
{
    if (std::fabs(s0) < 1e-3f)
        return 1.0f;
    return std::max(0.05f, std::min(20.0f, s / s0));
}

float factor_from_pixels(float d0, float d)
{
    if (d0 < 4.0f)
        return 1.0f; // (grabbed on the pivot itself: the distance says nothing)
    return std::max(0.05f, std::min(20.0f, d / d0));
}

float snap_factor(float factor) { return std::max(0.1f, std::round(factor * 10.0f) / 10.0f); }

std::vector<V3> get_ring(V3 pivot, float radius, int count)
{
    std::vector<V3> points;
    for (int i = 0; i < count; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count);
        points.push_back({pivot.x + std::sin(a) * radius, pivot.y, pivot.z + std::cos(a) * radius});
    }
    return points;
}

std::vector<Segment> gizmo_segments(const Gizmo& gizmo)
{
    std::vector<Segment> out;
    const V3 p = gizmo.pivot;
    const float length = gizmo.length;
    switch (gizmo.mode) {
    case Mode::kShiftBy:
        for (int axis = 0; axis < 3; ++axis)
            out.push_back({p, p + core::at(gizmo.axes, axis) * length, axis, true});
        break;
    case Mode::kSpinBy: {
        if (!gizmo.rotates)
            break;
        // (the heading ring, and a spoke towards the local z: which way it faces)
        std::vector<V3> ring = get_ring(p, length * 0.8f, 48);
        for (size_t i = 0; i < ring.size(); ++i)
            out.push_back({core::at(ring, i), core::at(ring, (i + 1) % ring.size()), 1, false});
        const V3 forward = {core::at(gizmo.axes, 2).x, 0.0f, core::at(gizmo.axes, 2).z};
        if (get_dot(forward, forward) > 1e-6f)
            out.push_back({p, p + make_unit(forward) * (length * 0.8f), 1, false});
        break;
    }
    case Mode::kScale: {
        float cube = length * 0.06f;
        auto add_cube = [&](V3 c, int handle) {
            std::array<V3, 8> corners{};
            for (int i = 0; i < 8; ++i)
                core::at(corners, i) = c + V3{(i & 1) ? cube : -cube, (i & 2) ? cube : -cube, (i & 4) ? cube : -cube};
            for (int i = 0; i < 8; ++i) {
                for (int bit = 1; bit < 8; bit <<= 1) {
                    if (!(i & bit))
                        out.push_back({core::at(corners, i), core::at(corners, i | bit), handle, false});
                }
            }
        };
        for (int axis = 0; axis < 3; ++axis) {
            const V3 end = p + core::at(gizmo.axes, axis) * length;
            out.push_back({p, end, axis, false});
            add_cube(end, axis);
        }
        add_cube(p, kCentre);
        break;
    }
    }
    return out;
}

int handle_at(const Gizmo& gizmo, const Projector& project, float x, float y, float grab)
{
    int best = -1;
    float best_distance = grab;
    // (Scale's centre cube wins near the pivot: it sits where the three axes start)
    float px = 0.0f, py = 0.0f;
    if (gizmo.mode == Mode::kScale && project(gizmo.pivot, &px, &py) &&
        (x - px) * (x - px) + (y - py) * (y - py) <= grab * grab * 2.25f)
        return kCentre;
    for (const Segment& s : gizmo_segments(gizmo)) {
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (!project(s.a, &ax, &ay) || !project(s.b, &bx, &by))
            continue;
        const float d = distance_to_segment(x, y, ax, ay, bx, by);
        if (d < best_distance) {
            best_distance = d;
            best = s.handle;
        }
    }
    return best;
}

uint32_t handle_colour(int handle, bool lit)
{
    if (lit)
        return 0xFFFFFF40;
    switch (handle) {
    case 0:
        return 0xFFFF4040;
    case 1:
        return 0xFF40E040;
    case 2:
        return 0xFF4080FF;
    default:
        return 0xFFE8E8E8;
    }
}

// --- typed values ---

bool type_key(Typed* typed, char c)
{
    if (c >= 'x' && c <= 'z') {
        const int axis = c - 'x';
        typed->axis = typed->axis == axis ? -1 : axis;
        typed->axis_changed_ = true;
        return true;
    }
    if (c >= '0' && c <= '9') {
        typed->text += c;
        return true;
    }
    if (c == '.') {
        if (typed->text.find('.') != std::string::npos)
            return false;
        typed->text += c;
        return true;
    }
    if (c == '-') {
        if (!typed->text.empty() && core::at(typed->text, 0) == '-')
            typed->text.erase(0, 1);
        else
            typed->text.insert(typed->text.begin(), '-');
        return true;
    }
    if (c == '\b') {
        if (!typed->text.empty())
            typed->text.pop_back();
        else if (typed->axis >= 0) {
            typed->axis = -1;
            typed->axis_changed_ = true;
        } else
            return false;
        return true;
    }
    return false;
}

bool typed_value(const Typed& typed, float* value)
{
    const std::string& t = typed.text;
    if (t.empty() || t == "-" || t == "." || t == "-.")
        return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v))
        return false;
    *value = static_cast<float>(v);
    return true;
}

std::string describe(Mode mode, const Typed& typed, int default_axis)
{
    static const std::array<const char*, 3> kAxisNames = {{"X", "Y", "Z"}};
    const int axis = typed.axis >= 0 ? typed.axis : default_axis;
    std::string text;
    switch (mode) {
    case Mode::kShiftBy:
        text = std::string("Move along ") + core::at(kAxisNames, axis >= 0 && axis < 3 ? axis : 0);
        break;
    case Mode::kSpinBy:
        text = "Turn (degrees of heading)";
        break;
    case Mode::kScale:
        text = axis >= 0 && axis < 3 ? std::string("Scale along ") + core::at(kAxisNames, axis) : std::string("Scale (all axes)");
        break;
    }
    text += ": ";
    text += typed.text.empty() ? std::string("_") : typed.text + "_";
    return text;
}

// --- the repeat of the last duplicate ---

void note_duplicate(Repeat* memory, Kind kind, const std::vector<uint32_t>& made, V3 source_pos, float source_heading)
{
    memory->kind = kind;
    memory->made = made;
    memory->source_pos_ = source_pos;
    memory->source_heading_ = source_heading;
}

bool repeat_for(const Repeat& memory, Kind kind, const std::vector<uint32_t>& selected, V3 pos, float heading,
                RepeatStep* step)
{
    if (memory.kind == Kind::kNone || kind != memory.kind || selected.empty())
        return false;
    std::vector<uint32_t> a = memory.made, b = selected;
    std::ranges::sort(a);
    std::ranges::sort(b);
    if (a != b)
        return false;
    step->offset = pos - memory.source_pos_;
    const float turn = get_wrap(heading - memory.source_heading_);
    if (kind == Kind::kScenery) {
        step->quarters = get_quarters(turn) & 3;
        step->turn = 0.0f;
    } else {
        step->quarters = 0;
        step->turn = std::fabs(turn) < 1e-5f ? 0.0f : turn;
    }
    return true;
}

V3 repeat_target(V3 from, const RepeatStep& step)
{
    const V3 offset = step.quarters       ? get_quarter_turn(step.offset, step.quarters)
                      : step.turn != 0.0f ? turn_about_up(step.offset, step.turn)
                                          : step.offset;
    return from + offset;
}

Repeat& last_duplicate() { return g_repeat; }

} // namespace frontend::level_editor::xform
