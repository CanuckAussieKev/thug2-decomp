// NOT ORIGINAL - the fisheye lens' maths (fisheye_lens.h). The lens pass's shader (gpu_vulkan/vulkan_fisheye.cpp) has
// angle_of operation for operation.
#include "engine/gfx/fisheye_lens.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "base/core/checked_index.h"

namespace render::fisheye {

namespace {

constexpr float kHalf = 0.5F;
constexpr float kLeastK = 1e-4F;   // below it the family's member is the equidistant lens itself
constexpr float kLeastRadius = 1e-6F;
constexpr double kDegreesPerHalfTurn = 180.0;
constexpr double kPi = 3.14159265358979323846;

// The family's parameter: k and which side (true: the tangent side, s < 0.5).
struct Member {
    float k = 0.0F;
    bool tangent = true;
};
Member member_of(float strength)
{
    const float s = std::clamp(strength, 0.0F, 1.0F);
    if (s < kHalf) {
        return {.k = 1.0F - (2.0F * s), .tangent = true};
    }
    return {.k = (2.0F * s) - 1.0F, .tangent = false};
}

} // namespace

float radius_of(float angle, float strength)
{
    const Member m = member_of(strength);
    if (m.k < kLeastK) {
        return angle;
    }
    return (m.tangent ? std::tan(m.k * angle) : std::sin(m.k * angle)) / m.k;
}

float angle_of(float radius, float strength)
{
    const Member m = member_of(strength);
    const float r = std::max(radius, 0.0F);
    float angle = r;
    if (m.k >= kLeastK) {
        angle = m.tangent ? std::atan(m.k * r) / m.k : std::asin(std::min(m.k * r, 1.0F)) / m.k;
    }
    return std::min(angle, kMostAngle);
}

Fit fit(float tan_x, float tan_y, float strength, float circle)
{
    Fit out;
    const float x = std::max(std::abs(tan_x), kLeastRadius);
    const float y = std::max(std::abs(tan_y), kLeastRadius);
    const float c = std::clamp(circle, 0.0F, 1.0F);
    out.corner = std::hypot(x, y);
    // the round frame: from the corners (none seen) in to the shorter sides' middles
    const float frame = out.corner + ((std::min(x, y) - out.corner) * c);
    const float lens = radius_of(kMostAngle, strength);
    out.reach = std::min(frame, lens);
    out.framed = out.reach < out.corner * (1.0F - kLeastK);
    // The widened view must hold every place shown: the farthest across is at the reach (the image plane's distance
    // tan(angle) grows faster than the lens' radius), at min(side, reach) along the axis - x and y each.
    const float plane = std::tan(angle_of(out.reach, strength)) / std::max(out.reach, kLeastRadius);
    const float across = std::min(x, out.reach) / x;
    const float down = std::min(y, out.reach) / y;
    out.widen = std::max(1.0F, plane * std::max(across, down));
    return out;
}

float widened_angle_degrees(float angle_degrees, float widen)
{
    const double half = static_cast<double>(angle_degrees) * kPi / (2.0 * kDegreesPerHalfTurn);
    const double widened = std::atan(std::tan(half) * static_cast<double>(widen));
    return static_cast<float>(widened * 2.0 * kDegreesPerHalfTurn / kPi);
}

std::array<float, 2> lens_position(std::array<float, 2> tangents, float strength)
{
    const float t = std::hypot(core::at(tangents, 0), core::at(tangents, 1));
    if (t < kLeastRadius) {
        return tangents;
    }
    const float r = radius_of(std::atan(t), strength);
    return {core::at(tangents, 0) * (r / t), core::at(tangents, 1) * (r / t)};
}

std::array<float, 2> plane_position(std::array<float, 2> place, float strength)
{
    const float r = std::hypot(core::at(place, 0), core::at(place, 1));
    if (r < kLeastRadius) {
        return place;
    }
    const float scale = std::tan(angle_of(r, strength)) / r;
    return {core::at(place, 0) * scale, core::at(place, 1) * scale};
}

} // namespace render::fisheye
