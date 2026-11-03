// NOT ORIGINAL - the level editor's transform tools (ours; see level_editor_transform.h).

#include "editor/level_editor_transform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <span>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_prefs.h"
#include "engine/maths/vec2.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/trig.h"

namespace frontend::level_editor::xform {

namespace {

constexpr float kDegenerateAxisSquared = 1e-12F; // a rotation row this short is taken as missing
constexpr float kFullTurnFactor = 2.0F;          // a turn is 2 pi radians
constexpr float kMinStartExtent = 1e-3F;         // a scale drag cannot start from a handle this close to the pivot
constexpr float kMinScaleFactor = 0.05F;         // the scale factor a drag may give
constexpr float kMaxScaleFactor = 20.0F;
constexpr float kMinStartPixels = 4.0F;
constexpr float kMinSnappedFactor = 0.1F;
constexpr float kFactorStepsPerUnit = 10.0F; // a snapped scale factor moves in tenths
constexpr float kRingRadiusFraction = 0.8F;  // of the gizmo length
constexpr int kRingPoints = 48;
constexpr float kMinForwardSquared = 1e-6F;
constexpr float kForwardArrowFraction = 0.8F;
constexpr float kCubeFraction = 0.06F;     // the scale handle cubes, of the gizmo length
constexpr float kGrabSlackSquared = 2.25F; // the pick radius is 1.5 times the grab size
constexpr std::uint32_t kLitHandleColour = 0xFFFFFF40;
constexpr std::uint32_t kAxisXColour = 0xFFFF4040;
constexpr std::uint32_t kAxisYColour = 0xFF40E040;
constexpr std::uint32_t kAxisZColour = 0xFF4080FF;
constexpr std::uint32_t kFreeHandleColour = 0xFFE8E8E8;
constexpr float kTurnEpsilon = 1e-5F; // a turn smaller than this is taken as none

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

namespace {


// NOT ORIGINAL grouping - level_editor_transform.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_transform_state()).
struct LevelEditorTransformState {
    Settings settings_{};
    Repeat repeat_{};
};

LevelEditorTransformState& level_editor_transform_state()
{
    static core::NoDestroy<LevelEditorTransformState> state;
    return state.get();
}

} // namespace

Settings& at_hand() { return level_editor_transform_state().settings_; }

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
    static const std::vector<float> kSizes = {1.0F, 5.0F, 10.0F, 25.0F, 50.0F, 100.0F};
    return kSizes;
}
const std::vector<float>& angle_steps()
{
    static const std::vector<float> kSteps = {5.0F, 10.0F, 15.0F, 30.0F, 45.0F, 90.0F};
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

void heading_axes(float yaw, std::span<V3, 3> out)
{
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    core::at(out, 0) = {.x = c, .y = 0.0F, .z = -s};
    core::at(out, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    core::at(out, 2) = {.x = s, .y = 0.0F, .z = c};
}

float get_heading_of(V3 at) { return std::atan2(at.x, at.z); }

void unit_axes(std::span<const V3, 3> rows, std::span<V3, 3> out)
{
    static const std::array<V3, 3> kWorld = {
        {{.x = 1.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 1.0F}}};
    for (int i = 0; i < 3; ++i) {
        core::at(out, i) = get_dot(core::at(rows, i), core::at(rows, i)) > kDegenerateAxisSquared ? make_unit(core::at(rows, i)) : core::at(kWorld, i);
    }
}

float turn_around_axis(V3 pivot, V3 point) { return std::atan2(point.x - pivot.x, point.z - pivot.z); }

float get_wrap(float radians)
{
    float r = std::fmod(radians, kFullTurnFactor * maths::kPi);
    if (r <= -maths::kPi) {
        r += kFullTurnFactor * maths::kPi;
    } else if (r > maths::kPi) {
        r -= kFullTurnFactor * maths::kPi;
    }
    return r;
}

float snap_angle(AngleSnap angle_snap)
{
    const auto radians = angle_snap.radians;
    const auto step_degrees = angle_snap.step_degrees_;
    if (step_degrees <= 0.0F) {
        return radians;
    }
    const float step = step_degrees * maths::kPi / 180.0F;
    return std::round(radians / step) * step;
}

int get_quarters(float radians) { return static_cast<int>(std::lround(radians / (maths::kPi * kHalf))); }

float snap_relative(float distance, float grid) { return grid > 0.0F ? std::round(distance / grid) * grid : distance; }

float factor_along(float s0, float s)
{
    if (std::fabs(s0) < kMinStartExtent) {
        return 1.0F;
    }
    return std::max(kMinScaleFactor, std::min(kMaxScaleFactor, s / s0));
}

float factor_from_pixels(float d0, float d)
{
    if (d0 < kMinStartPixels) {
        return 1.0F; // (grabbed on the pivot itself: the distance says nothing)
    }
    return std::max(kMinScaleFactor, std::min(kMaxScaleFactor, d / d0));
}

float snap_factor(float factor) { return std::max(kMinSnappedFactor, std::round(factor * kFactorStepsPerUnit) / kFactorStepsPerUnit); }

std::vector<V3> get_ring(V3 pivot, RingShape ring)
{
    const auto radius = ring.radius;
    const auto count = ring.count;
    std::vector<V3> points;
    for (int i = 0; i < count; ++i) {
        const float a = 2.0F * maths::kPi * static_cast<float>(i) / static_cast<float>(count);
        points.push_back({.x = pivot.x + (std::sin(a) * radius), .y = pivot.y, .z = pivot.z + (std::cos(a) * radius)});
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
        for (int axis = 0; axis < 3; ++axis) {
            out.push_back({.a = p, .b = p + core::at(gizmo.axes, axis) * length, .handle = axis, .arrow = true});
        }
        break;
    case Mode::kSpinBy: {
        if (!gizmo.rotates) {
            break;
        }
        // (the heading ring, and a spoke towards the local z: which way it faces)
        std::vector<V3> ring = get_ring(p, {.radius = length * kRingRadiusFraction, .count = kRingPoints});
        for (size_t i = 0; i < ring.size(); ++i) {
            out.push_back({.a = core::at(ring, i), .b = core::at(ring, (i + 1) % ring.size()), .handle = 1, .arrow = false});
        }
        const V3 forward = {.x = core::at(gizmo.axes, 2).x, .y = 0.0F, .z = core::at(gizmo.axes, 2).z};
        if (get_dot(forward, forward) > kMinForwardSquared) {
            out.push_back({.a = p, .b = p + make_unit(forward) * (length * kForwardArrowFraction), .handle = 1, .arrow = false});
        }
        break;
    }
    case Mode::kScale: {
        float cube = length * kCubeFraction;
        auto add_cube = [&](V3 c, int handle) {
            std::array<V3, kBoxCorners> corners{};
            for (int i = 0; i < kBoxCorners; ++i) {
                core::at(corners, i) = c + V3{.x = (i & 1) ? cube : -cube, .y = (i & 2) ? cube : -cube, .z = (i & 4) ? cube : -cube};
            }
            for (int i = 0; i < kBoxCorners; ++i) {
                for (int bit = 1; bit < kBoxCorners; bit <<= 1) {
                    if (!(i & bit)) {
                        out.push_back({.a = core::at(corners, i), .b = core::at(corners, i | bit), .handle = handle, .arrow = false});
                    }
                }
            }
        };
        for (int axis = 0; axis < 3; ++axis) {
            const V3 end = p + core::at(gizmo.axes, axis) * length;
            out.push_back({.a = p, .b = end, .handle = axis, .arrow = false});
            add_cube(end, axis);
        }
        add_cube(p, kCentre);
        break;
    }
    }
    return out;
}

int handle_at(const Gizmo& gizmo, const Projector& project, maths::Vec2 at, float grab)
{
    const float x = at.x;
    const float y = at.y;
    int best = -1;
    float best_distance = grab;
    // (Scale's centre cube wins near the pivot: it sits where the three axes start)
    float px = 0.0F;
    float py = 0.0F;
    if (gizmo.mode == Mode::kScale && project(gizmo.pivot, &px, &py) &&
        ((x - px) * (x - px)) + ((y - py) * (y - py)) <= grab * grab * kGrabSlackSquared) {
        return kCentre;
    }
    for (const Segment& s : gizmo_segments(gizmo)) {
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (!project(s.a, &ax, &ay) || !project(s.b, &bx, &by)) {
            continue;
        }
        const float d = distance_to_segment({.x = x, .y = y}, {.x = ax, .y = ay}, {.x = bx, .y = by});
        if (d < best_distance) {
            best_distance = d;
            best = s.handle;
        }
    }
    return best;
}

uint32_t handle_colour(int handle, bool lit)
{
    if (lit) {
        return kLitHandleColour;
    }
    switch (handle) {
    case 0:
        return kAxisXColour;
    case 1:
        return kAxisYColour;
    case 2:
        return kAxisZColour;
    default:
        return kFreeHandleColour;
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
        if (typed->text.find('.') != std::string::npos) {
            return false;
        }
        typed->text += c;
        return true;
    }
    if (c == '-') {
        if (!typed->text.empty() && core::at(typed->text, 0) == '-') {
            typed->text.erase(0, 1);
        } else {
            typed->text.insert(typed->text.begin(), '-');
        }
        return true;
    }
    if (c == '\b') {
        if (!typed->text.empty()) {
            typed->text.pop_back();
        } else if (typed->axis >= 0) {
            typed->axis = -1;
            typed->axis_changed_ = true;
        } else {
            return false;
        }
        return true;
    }
    return false;
}

bool typed_value(const Typed& typed, float* value)
{
    const std::string& t = typed.text;
    if (t.empty() || t == "-" || t == "." || t == "-.") {
        return false;
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) {
        return false;
    }
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
    if (memory.kind == Kind::kNone || kind != memory.kind || selected.empty()) {
        return false;
    }
    std::vector<uint32_t> a = memory.made;
    std::vector<uint32_t> b = selected;
    std::ranges::sort(a);
    std::ranges::sort(b);
    if (a != b) {
        return false;
    }
    step->offset = pos - memory.source_pos_;
    const float turn = get_wrap(heading - memory.source_heading_);
    if (kind == Kind::kScenery) {
        step->quarters = get_quarters(turn) & 3;
        step->turn = 0.0F;
    } else {
        step->quarters = 0;
        step->turn = std::fabs(turn) < kTurnEpsilon ? 0.0F : turn;
    }
    return true;
}

V3 repeat_target(V3 from, const RepeatStep& step)
{
    const V3 offset = step.quarters       ? get_quarter_turn(step.offset, step.quarters)
                      : step.turn != 0.0F ? turn_about_up(step.offset, step.turn)
                                          : step.offset;
    return from + offset;
}

Repeat& last_duplicate() { return level_editor_transform_state().repeat_; }

} // namespace frontend::level_editor::xform
