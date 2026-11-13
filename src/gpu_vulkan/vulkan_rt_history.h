#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the temporal effects' first frame (docs/design/vulkan.md "The first frame after a load"): what the
// ambient occlusion, the one-bounce light (vulkan_rt_effects.h) and the volumetric fog (vulkan_rt_fog.h) share about a
// history that starts again.
//
// Each of them accumulates a few noisy rays a frame into a per-viewport history (up to kAoMaxHistory / kFogMaxHistory
// frames). A history with nothing to go on - a new viewport or target, the first frame after a load, a viewport not
// drawn the frame before, the settings changed, or a cut (below) - is seeded: the trace and the temporal pass run as many
// times in that frame as the history holds, each with the next frame's directions / jitter and the same camera, so the
// frame shows what a still camera would after that many frames - converged, nothing fades in. Pixels the camera newly
// sees in a continuing shot (disoccluded) still start again on their own, as before.
//
// A cut: between two frames of a viewport the camera moved more than kCutDistance world units or its view direction
// turned more than kCutDegrees - a teleport, a restart, a camera switch, looking behind. Reprojection would find nothing
// that agrees there anyway; seeding instead of fading.
namespace render::vulkan {

inline constexpr float kCutDistance = 250.0F;
inline constexpr float kCutDegrees = 45.0F;

// The camera's view direction in the world (the view matrix's third column - D3D's row-vector layout).
[[nodiscard]] inline std::array<float, 3> view_direction(const rt::Camera& camera)
{
    return {std::get<2>(std::get<0>(camera.view)), std::get<2>(std::get<1>(camera.view)),
            std::get<2>(std::get<2>(camera.view))};
}

// The camera as a history remembers it (the frame before's).
struct HistoryCamera {
    std::array<float, 3> position{};
    std::array<float, 3> direction{};
    bool known = false;
};

[[nodiscard]] inline HistoryCamera history_camera(const rt::Camera& camera)
{
    return {.position = camera.position, .direction = view_direction(camera), .known = true};
}

// Whether `now` is a cut from `before` (an unknown camera never is: the history's own checks decide).
[[nodiscard]] inline bool camera_cut(const HistoryCamera& before, const rt::Camera& now)
{
    if (!before.known) {
        return false;
    }
    const std::array<float, 3> direction = view_direction(now);
    double moved = 0.0;
    double along = 0.0;
    double length_before = 0.0;
    double length_now = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        const double step =
            static_cast<double>(core::at(now.position, i)) - static_cast<double>(core::at(before.position, i));
        moved += step * step;
        along += static_cast<double>(core::at(before.direction, i)) * static_cast<double>(core::at(direction, i));
        length_before +=
            static_cast<double>(core::at(before.direction, i)) * static_cast<double>(core::at(before.direction, i));
        length_now += static_cast<double>(core::at(direction, i)) * static_cast<double>(core::at(direction, i));
    }
    constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
    const double lengths = std::sqrt(length_before * length_now);
    const bool turned =
        lengths > 0.0 && along / lengths < std::cos(static_cast<double>(kCutDegrees) * kRadiansPerDegree);
    return !std::isfinite(moved) || moved > static_cast<double>(kCutDistance) * static_cast<double>(kCutDistance) ||
           turned;
}

// How many frames' rays a history that starts again is seeded with: `most`, the longest it holds (1: not seeded - the
// rays as they are, nothing to accumulate).
[[nodiscard]] constexpr uint32_t seed_frames(bool accumulating, bool starts_again, uint32_t most)
{
    return accumulating && starts_again ? most : 1U;
}

} // namespace render::vulkan
