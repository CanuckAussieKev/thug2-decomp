#pragma once

// NOT ORIGINAL - the level's wind (docs/design/vulkan.md "Snow", "Grass"): one wind every effect that moves with it
// reads - the grass's sway, the falling snow's drift. API-neutral, constant over a level (the game has no wind of its own):
// a direction over the ground and a strength that gusts on a slow clock of its own.

#include <array>
#include <cmath>

namespace render::wind {

inline constexpr std::array<float, 2> kDirection{0.8F, 0.6F}; // downwind (x, z; unit length)
inline constexpr float kSpeed = 22.0F;        // world units a second at the wind's mean (about 2 km/h of drift)
inline constexpr float kGustShare = 0.45F;    // the gusts: the speed between (1 - this) and (1 + this) of the mean
inline constexpr float kGustSlow = 0.13F;     // the gusts' two waves (radians a second)
inline constexpr float kGustFast = 0.41F;

// The wind's strength at `seconds` on its clock, over its mean (the shaders' twin: vulkan_snow.cpp's wind_at).
[[nodiscard]] inline float gust(float seconds)
{
    constexpr float kSlowShare = 0.65F;
    constexpr float kFastShare = 0.35F;
    return 1.0F + (kGustShare * ((kSlowShare * std::sin(seconds * kGustSlow)) + (kFastShare * std::sin((seconds * kGustFast) + 1.3F))));
}

} // namespace render::wind
