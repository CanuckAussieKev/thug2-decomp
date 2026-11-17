#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the precipitation occlusion map on the device (engine/gfx/precip_occlusion.h; docs/design/vulkan.md
// "Rain" - "Where the rain reaches"): one map for every weather effect that asks where what falls reaches - the rain's
// drops, splashes and wet ground (vulkan_rain.h) now, the snow's settling later. A user asks for it each frame with the
// fall's direction (update_precip_map, at its view_begins); the frame's first view traces it: a compute pass with ray
// queries through the frame's scene (precip::kTraceMask: the level and the cut-out casters) along the lines of fall of
// the two cascades' windows round the camera - only the lines new to a window, a slice of the others
// (precip::kRefreshSlices), every line when the direction moved (a new generation). Kept on the device as RG32UI (the
// first hit's height's bits, the line's tag), the cascades side by side, in GENERAL layout (read as a usampler2D with
// texelFetch). Without ray tracing nothing is traced: the map is off and everything is reached (logged once).
namespace render::vulkan {

struct Backend;

// What a reader binds and puts in its block (precip::reached_glsl's precip_fall / _slant / _near / _far). `image` is
// set whenever the map is made (bind it even when `on` is false - untraced: everything reached).
struct PrecipMap {
    bool on = false;
    VkDescriptorImageInfo image{};      // (nearest sampler, GENERAL)
    std::array<float, 4> fall{};        // xyz the direction (unit), w 1 when on
    std::array<float, 4> slant{};       // xy the slant, z the reference height
    std::array<float, 4> near_window{}; // xy the first texel (absolute), z the texel (units), w the slack
    std::array<float, 4> far_window{};
};

// The map brought round the view's camera for a fall `direction` (unit, y < 0); traced at the frame's first view (later
// views of the frame read the same map). The caller holds the lock; records into the frame's stream outside a pass.
void update_precip_map(Backend& state, const rt::ViewDrawn& view, const std::array<double, 3>& direction);
// The map as it stands (the last update's); `on` false: none (no ray tracing, not traced yet).
[[nodiscard]] PrecipMap precip_map(Backend& state);

struct PrecipStats {
    uint64_t updates = 0; // frames traced
    uint32_t generation = 0;
    bool traced = false; // the last update traced (a scene, ray tracing)
};
[[nodiscard]] PrecipStats precip_stats();
// (the check's) The map read back: RG32UI texels, kCascades x kMapSize wide, kMapSize high. The frame's work finished.
bool read_precip_map(Backend& state, std::vector<uint32_t>* texels);

void release_precip(Backend& state);

} // namespace render::vulkan
