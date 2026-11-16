#pragma once

#include <algorithm>
#include <cmath>

// NOT ORIGINAL - the frame's one transfer between the game's colours and light (round F, docs/design/vulkan.md "A
// linear, scene-referred frame").
//
// The original's colours - its textures, vertex colours, the bake, the fog and clear colours, the light values the
// game and the relit stream hand the draws - are display-referred: made for a display of gamma 2.2, 1 the original's
// white. The Vulkan frame holds linear, scene-referred light in the sky's unit (rt_sky.h: 1 a white diffuser under the
// reference day) from the draws on: what is display-referred is decoded where it enters the frame (its inputs), every
// pass composes linear light, and the output (vulkan_hdr's pass) encodes once for the display. These two functions and
// their GLSL twins are the only places the display's gamma is applied; a light ratio made from display-referred values
// (a / b) decodes the same way (the power law is multiplicative: decode(a x b) = decode(a) x decode(b)).
namespace render::rt {

inline constexpr double kDisplayGamma = 2.2;

// A display-referred value (at least 0) as linear light, and back.
[[nodiscard]] inline double linear_from_display(double display) { return std::pow(std::max(display, 0.0), kDisplayGamma); }
[[nodiscard]] inline double display_from_linear(double linear) { return std::pow(std::max(linear, 0.0), 1.0 / kDisplayGamma); }
[[nodiscard]] inline float linear_from_display(float display)
{
    return static_cast<float>(linear_from_display(static_cast<double>(display)));
}
[[nodiscard]] inline float display_from_linear(float linear)
{
    return static_cast<float>(display_from_linear(static_cast<double>(linear)));
}

// The same in GLSL, for the passes' shaders (paste into the source before first use).
inline constexpr const char* kDisplayTransferGlsl =
    "const float DISPLAY_GAMMA = 2.2;\n"
    "vec3 linear_from_display(vec3 c) { return pow(max(c, vec3(0.0)), vec3(DISPLAY_GAMMA)); }\n"
    "vec3 display_from_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / DISPLAY_GAMMA)); }\n"
    "float linear_from_display(float c) { return pow(max(c, 0.0), DISPLAY_GAMMA); }\n"
    "float display_from_linear(float c) { return pow(max(c, 0.0), 1.0 / DISPLAY_GAMMA); }\n";

} // namespace render::rt
