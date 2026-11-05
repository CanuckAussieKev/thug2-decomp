// NOT ORIGINAL - the level editor's geometry helpers: view angles, rays against spheres, boxes and planes, screen
// projection, clipping to the near plane (ours). See level_editor.h.

#include "editor/level_editor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <array>
#include "base/maths/vec2.h"
#include "base/core/checked_index.h"
#include "base/maths/trig.h"

namespace frontend::level_editor {
namespace {

constexpr double kNdcSpan = 2.0;            // normalised device coordinates run from -1 to 1
constexpr double kParallelEpsilon = 1e-12;  // a ray this close to parallel to a slab never enters it
constexpr double kLineParallelEpsilon = 1e-9; // two lines this close to parallel have no nearest points
constexpr float kHoldMargin = 0.05F;          // a box holds a point within this share of its size outside it
constexpr float kGroundRayRise = 50.0F;       // the ground ray starts this far above the base

constexpr int kHexLetterBase = 10;          // the value of the hex digit 'a'

constexpr double kHalfDouble = 0.5; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

float axis_value(V3 v, int axis) { return axis_ref(v, axis); }
float& axis_ref(V3& v, int axis)
{
    if (axis == 0) {
        return v.x;
    }
    if (axis == 1) {
        return v.y;
    }
    return v.z;
}
int hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + kHexLetterBase;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + kHexLetterBase;
    }
    return -1;
}

V3 make_unit(V3 v)
{
    const double length = std::sqrt((static_cast<double>(v.x) * v.x) + (static_cast<double>(v.y) * v.y) +
                                    (static_cast<double>(v.z) * v.z));
    if (length <= 0.0) {
        return v;
    }
    return {.x = static_cast<float>(v.x / length), .y = static_cast<float>(v.y / length), .z = static_cast<float>(v.z / length)};
}

TickCycle frame_from_yaw_pitch(float yaw, float pitch)
{
    const double cp = std::cos(static_cast<double>(pitch));
    const double sp = std::sin(static_cast<double>(pitch));
    const double cy = std::cos(static_cast<double>(yaw));
    const double sy = std::sin(static_cast<double>(yaw));
    const V3 forward = {.x = static_cast<float>(sy * cp), .y = static_cast<float>(sp), .z = static_cast<float>(cy * cp)};
    TickCycle frame{};
    frame.back = forward * -1.0F;
    frame.right = make_unit(perpendicular({.x = 0.0F, .y = 1.0F, .z = 0.0F}, frame.back));
    frame.up = make_unit(perpendicular(frame.back, frame.right));
    return frame;
}

void yaw_pitch_from_back(V3 back, YawPitchOut view)
{
    auto* const yaw = view.yaw;
    auto* const pitch = view.pitch;
    const V3 forward = make_unit(back * -1.0F);
    *yaw = static_cast<float>(std::atan2(static_cast<double>(forward.x), static_cast<double>(forward.z)));
    *pitch = static_cast<float>(std::asin(std::max(-1.0, std::min(1.0, static_cast<double>(forward.y)))));
}

V3 get_fly(V3 pos, const TickCycle& frame, const FlyInput& input, float distance)
{
    const V3 forward = frame.back * -1.0F;
    const V3 move = forward * static_cast<float>(input.forward) + frame.right * static_cast<float>(input.right) +
                    V3{.x = 0.0F, .y = static_cast<float>(input.up), .z = 0.0F};
    if (move.x == 0.0F && move.y == 0.0F && move.z == 0.0F) {
        return pos;
    }
    return pos + make_unit(move) * distance;
}

void get_look(YawPitchOut view, float dx, float dy, float radians_per_pixel)
{
    auto* const yaw = view.yaw;
    auto* const pitch = view.pitch;
    constexpr float kLimit = 89.0F * 3.14159265F / 180.0F;
    // (yaw turns +z towards +x, which is to the left of a view along +z: right is -yaw)
    *yaw -= dx * radians_per_pixel;
    *pitch = std::max(-kLimit, std::min(kLimit, *pitch - (dy * radians_per_pixel)));
    if (*yaw > maths::kTwoPi || *yaw < -maths::kTwoPi) {
        *yaw = std::fmod(*yaw, maths::kTwoPi);
    }
}

Lens lens_for(LensShape lens_shape)
{
    const auto angle_degrees = lens_shape.angle_degrees_;
    const auto aspect = lens_shape.aspect;
    const double half_angle = static_cast<double>(angle_degrees) * 0.008726646F;
    const double tan_x = std::tan(half_angle);
    const double height_factor = 1.0 / (static_cast<double>(aspect) * 0.90909094F);
    return {.tan_x_ = static_cast<float>(tan_x), .tan_y_ = static_cast<float>(tan_x * height_factor)};
}

bool pixel_to_ndc(std::span<const uint32_t, 4> viewport, float px, float py, float* nx, float* ny)
{
    if (core::at(viewport, 2) == 0 || core::at(viewport, 3) == 0) {
        return false;
    }
    *nx = static_cast<float>(((static_cast<double>(px) - core::at(viewport, 0)) / core::at(viewport, 2) * kNdcSpan) -
                             1.0);
    *ny = static_cast<float>(1.0 -
                             ((static_cast<double>(py) - core::at(viewport, 1)) / core::at(viewport, 3) * kNdcSpan));
    return true;
}

void ndc_to_pixel(std::span<const uint32_t, 4> viewport, float nx, float ny, float* px, float* py)
{
    *px = static_cast<float>(core::at(viewport, 0) +
                             ((static_cast<double>(nx) + 1.0) * kHalfDouble * core::at(viewport, 2)));
    *py = static_cast<float>(core::at(viewport, 1) +
                             ((1.0 - static_cast<double>(ny)) * kHalfDouble * core::at(viewport, 3)));
}

V3 ray_direction(const TickCycle& frame, const Lens& lens, float nx, float ny)
{
    return frame.back * -1.0F + frame.right * (nx * lens.tan_x_) + frame.up * (ny * lens.tan_y_);
}

bool cast_onto(V3 camera_pos, const TickCycle& frame, const Lens& lens, V3 point, float near_z, float* nx, float* ny)
{
    const V3 delta = point - camera_pos;
    const float depth = -get_dot(delta, frame.back);
    if (depth < near_z || lens.tan_x_ == 0.0F || lens.tan_y_ == 0.0F) {
        return false;
    }
    *nx = get_dot(delta, frame.right) / (depth * lens.tan_x_);
    *ny = get_dot(delta, frame.up) / (depth * lens.tan_y_);
    return true;
}

bool ray_sphere(Ray ray, V3 centre, float radius, float* hit_distance)
{
    const auto origin = ray.origin;
    const auto dir = ray.dir;
    const V3 oc = origin - centre;
    const double quad_a = get_dot(dir, dir);
    const double quad_b = 2.0 * get_dot(oc, dir);
    const double quad_c = static_cast<double>(get_dot(oc, oc)) - (radius * radius);
    if (quad_a <= 0.0) {
        return false;
    }
    const double discriminant = (quad_b * quad_b) - (4.0 * quad_a * quad_c);
    if (discriminant < 0.0) {
        return false;
    }
    const double root = std::sqrt(discriminant);
    const double t_near = (-quad_b - root) / (2.0 * quad_a);
    const double t_far = (-quad_b + root) / (2.0 * quad_a);
    if (t_far < 0.0) {
        return false;
    }
    *hit_distance = static_cast<float>(t_near >= 0.0 ? t_near : 0.0); // (inside: at once)
    return true;
}

bool ray_box(Ray ray, V3 box_origin, std::span<const V3, 3> axes, V3 min, V3 max, float* hit_distance)
{
    const auto origin = ray.origin;
    const auto dir = ray.dir;
    // The ray in the box's own coordinates (p = local.x * axes[0] + local.y * axes[1] + local.z * axes[2] + origin;
    // the rows are taken as orthogonal).
    const V3 rel = origin - box_origin;
    std::array<double, 3> local_origin{};
    std::array<double, 3> local_dir{};
    for (int i = 0; i < 3; ++i) {
        const double length_squared = get_dot(core::at(axes, i), core::at(axes, i));
        if (length_squared <= 0.0) {
            return false;
        }
        core::at(local_origin, i) = get_dot(rel, core::at(axes, i)) / length_squared;
        core::at(local_dir, i) = get_dot(dir, core::at(axes, i)) / length_squared;
    }
    const std::array<double, 3> lo = {{min.x, min.y, min.z}};
    const std::array<double, 3> hi = {{max.x, max.y, max.z}};
    double enter = -kFarDistanceDouble;
    double leave = kFarDistanceDouble;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(core::at(local_dir, i)) < kParallelEpsilon) {
            if (core::at(local_origin, i) < core::at(lo, i) || core::at(local_origin, i) > core::at(hi, i)) {
                return false;
            }
            continue;
        }
        double t0 = (core::at(lo, i) - core::at(local_origin, i)) / core::at(local_dir, i);
        double t1 = (core::at(hi, i) - core::at(local_origin, i)) / core::at(local_dir, i);
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave) {
            return false;
        }
    }
    if (leave < 0.0) {
        return false;
    }
    *hit_distance = static_cast<float>(enter >= 0.0 ? enter : 0.0);
    return true;
}

bool ray_plane(Ray ray, V3 point, V3 normal, float* hit_distance)
{
    const auto origin = ray.origin;
    const auto dir = ray.dir;
    const double denominator = get_dot(dir, normal);
    if (std::fabs(denominator) < kLineParallelEpsilon) {
        return false;
    }
    const double hit = get_dot(point - origin, normal) / denominator;
    if (hit < 0.0) {
        return false;
    }
    *hit_distance = static_cast<float>(hit);
    return true;
}

void box_corners(V3 box_origin, std::span<const V3, 3> axes, V3 min, V3 max, std::span<V3, kBoxCornerCount> out)
{
    for (int i = 0; i < kBoxCorners; ++i) {
        const float x = ((i & 1) != 0) ? max.x : min.x;
        const float y = ((i & 2) != 0) ? max.y : min.y;
        const float z = ((i & 4) != 0) ? max.z : min.z;
        core::at(out, i) = box_origin + core::at(axes, 0) * x + core::at(axes, 1) * y + core::at(axes, 2) * z;
    }
}

bool box_looks_world_space(V3 min, V3 max, V3 object_pos)
{
    auto holds = [&](V3 point, float margin) {
        return point.x >= min.x - margin && point.x <= max.x + margin && point.y >= min.y - margin &&
               point.y <= max.y + margin && point.z >= min.z - margin && point.z <= max.z + margin;
    };
    const float size = std::max({max.x - min.x, max.y - min.y, max.z - min.z, 1.0F});
    return holds(object_pos, size * kHoldMargin) && !holds({.x = 0.0F, .y = 0.0F, .z = 0.0F}, size * kHoldMargin);
}

V3 turn_about_up(V3 v, float angle)
{
    const double cosine = std::cos(static_cast<double>(angle));
    const double sine = std::sin(static_cast<double>(angle));
    return {.x = static_cast<float>((v.x * cosine) + (v.z * sine)),
            .y = v.y,
            .z = static_cast<float>((-v.x * sine) + (v.z * cosine))};
}

bool closest_on_axis(Ray axis, Ray line, float* along_axis)
{
    const auto line_origin = line.origin;
    const auto line_dir = line.dir;
    const auto axis_origin = axis.origin;
    const auto u = axis.dir;
    // Minimise |(p + u s) - (o + d t)|: with w = p - o, a = u.u, b = u.d, c = d.d, e = u.w, f = d.w,
    // s = (b f - c e) / (a c - b b).
    const V3 w = axis_origin - line_origin;
    const double dot_uu = get_dot(u, u);
    const double dot_ud = get_dot(u, line_dir);
    const double dot_dd = get_dot(line_dir, line_dir);
    const double dot_uw = get_dot(u, w);
    const double dot_dw = get_dot(line_dir, w);
    const double denominator = (dot_uu * dot_dd) - (dot_ud * dot_ud);
    if (std::fabs(denominator) < kLineParallelEpsilon * dot_uu * dot_dd || dot_uu <= 0.0 || dot_dd <= 0.0) {
        return false;
    }
    *along_axis = static_cast<float>(((dot_ud * dot_dw) - (dot_dd * dot_uw)) / denominator);
    return true;
}

float distance_to_segment(maths::Vec2 point, maths::Vec2 a, maths::Vec2 b)
{
    const float x = point.x;
    const float y = point.y;
    const float ax = a.x;
    const float ay = a.y;
    const float bx = b.x;
    const float by = b.y;
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = (dx * dx) + (dy * dy);
    double along = length > 0.0 ? (((x - ax) * dx) + ((y - ay) * dy)) / length : 0.0;
    along = std::max(0.0, std::min(1.0, along));
    const double px = ax + (dx * along) - x;
    const double py = ay + (dy * along) - y;
    return static_cast<float>(std::sqrt((px * px) + (py * py)));
}

float snap_to_grid(float value, float grid) { return grid > 0.0F ? std::round(value / grid) * grid : value; }

V3 ground_ray_start(V3 pos, float base, bool from_above)
{
    return from_above ? V3{.x = pos.x, .y = base + kGroundRayRise, .z = pos.z}
                      : V3{.x = pos.x, .y = base - 1.0F, .z = pos.z};
}

V3 dropped_to(V3 pos, float base, float ground_y) { return {.x = pos.x, .y = pos.y + (ground_y - base), .z = pos.z}; }

bool clip_to_near(V3 camera_pos, const TickCycle& frame, float near_z, V3* a, V3* b)
{
    const float da = -get_dot(*a - camera_pos, frame.back);
    const float db = -get_dot(*b - camera_pos, frame.back);
    if (da < near_z && db < near_z) {
        return false;
    }
    if (da < near_z) {
        *a = *a + (*b - *a) * ((near_z - da) / (db - da));
    } else if (db < near_z) {
        *b = *b + (*a - *b) * ((near_z - db) / (da - db));
    }
    return true;
}

} // namespace frontend::level_editor
