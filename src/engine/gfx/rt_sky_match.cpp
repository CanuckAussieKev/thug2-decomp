// NOT ORIGINAL - a level's sky capture's texels as directions (rt_sky_match.h).
#include "engine/gfx/rt_sky_match.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

namespace {

constexpr double kHalf = 0.5;

std::array<double, 3> cross3(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return {
        (static_cast<double>(core::at(a, 1)) * core::at(b, 2)) - (static_cast<double>(core::at(a, 2)) * core::at(b, 1)),
        (static_cast<double>(core::at(a, 2)) * core::at(b, 0)) - (static_cast<double>(core::at(a, 0)) * core::at(b, 2)),
        (static_cast<double>(core::at(a, 0)) * core::at(b, 1)) -
            (static_cast<double>(core::at(a, 1)) * core::at(b, 0))};
}

} // namespace

CaptureTexel capture_texel(const CaptureSpot& spot, uint32_t face_size)
{
    const SkyCaptureFace& side = core::at(kSkyCaptureFaces, spot.face % kSkyCaptureFaces.size());
    const double size = std::max(1U, face_size);
    const double across = ((2.0 * (static_cast<double>(spot.x) + kHalf)) / size) - 1.0;
    const double up = 1.0 - ((2.0 * (static_cast<double>(spot.y) + kHalf)) / size);
    const std::array<double, 3> right = cross3(side.forward, side.up); // (render_pc's right-handed look-at)
    std::array<double, 3> direction{};
    double length = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(direction, i) = static_cast<double>(core::at(side.forward, i)) + (across * core::at(right, i)) +
                                 (up * static_cast<double>(core::at(side.up, i)));
        length += core::at(direction, i) * core::at(direction, i);
    }
    length = std::sqrt(length);
    for (double& v : direction) {
        v /= length;
    }
    const double spread = 1.0 + (across * across) + (up * up);
    return {.direction = direction, .weight = 1.0 / (spread * std::sqrt(spread))};
}

} // namespace render::rt
