#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style.h"

// NOT ORIGINAL - the art styles' processor twins share these pieces (vulkan_style.cpp defines them; the looks'
// twins in vulkan_style_looks.cpp use them): each is the GLSL function of the same name's twin.
namespace render::vulkan::style_twin {

inline constexpr std::size_t kLightness = 0; // (an OKLab colour's L, a, b)
inline constexpr std::size_t kGreenRed = 1;
inline constexpr std::size_t kBlueYellow = 2;

// A pixel of an image (column, row).
struct Pixel {
    int column = 0;
    int row = 0;
};

// An image's pixel, its coordinates clamped into it.
template <typename T>
const T& clamped_at(const std::vector<T>& image, Extent size, Pixel at)
{
    const auto column = static_cast<std::size_t>(std::clamp(at.column, 0, static_cast<int>(size.width) - 1));
    const auto row = static_cast<std::size_t>(std::clamp(at.row, 0, static_cast<int>(size.height) - 1));
    return core::at(image, (row * size.width) + column);
}

[[nodiscard]] float finite(float value);
[[nodiscard]] StyleRgb finite3(const StyleRgb& colour);
// A value as a 16-bit float stores it (the passes' RGBA16F images).
[[nodiscard]] float as_half(float value);
[[nodiscard]] StyleRgb as_half3(const StyleRgb& colour);
[[nodiscard]] float smoothstep(float low, float high, float value);
// GLSL's mix: from (1 - part) + to part.
[[nodiscard]] float mix(float from, float to, float part);
[[nodiscard]] StyleRgb mix3(const StyleRgb& from, const StyleRgb& to, float part);
[[nodiscard]] StyleRgb to_lab(const StyleRgb& colour);
[[nodiscard]] StyleRgb from_lab(const StyleRgb& lab);
[[nodiscard]] StyleRgb simplified_lab(const StyleParameters& parameters, StyleRgb lab);
// The smoothed half-resolution colour at a full-resolution pixel, guided by the pixel's own colour (`guided` false:
// plain bilinear - the thermal look's blur).
[[nodiscard]] StyleRgb upsampled(const std::vector<StyleRgb>& smooth, Extent half, Pixel at, const StyleRgb& own,
                                 bool guided = true);
// The edge at a pixel of the linear depth (faded with the distance; before rt_style_outlines).
[[nodiscard]] float edge_at(const StyleParameters& parameters, const std::vector<float>& depth, Extent size, Pixel at);

} // namespace render::vulkan::style_twin
