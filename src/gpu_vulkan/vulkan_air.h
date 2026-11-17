#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <volk.h>

// NOT ORIGINAL - round W: the level's air transport on the device (engine/gfx/air_volume.h; docs/design/vulkan.md "Round
// W: the air and the wind") - a window of it round the camera (air::kWindowCells cells a side across the ground,
// air::kWindowLayers up, each air::kWindowCell units) that every pass moving things with the wind samples: the grass's
// blades, the clutter's leaves and weeds, the falling snow's eddies. Kept as one RG8 2D image, the layers stacked
// (texel (x, layer x kWindowCells + z)); the GLSL twin (air_map_glsl) blends two layers. Filled on a worker thread from
// the published volume (air::air_window) when the camera has moved an eighth of the window from its centre or the volume
// changed, copied up when ready; until then the last window (or none - every place open air).
namespace render::vulkan {

struct Backend;

struct AirMap {
    bool on = false;               // a window on the device (else `image` the blank texture: bind it anyway)
    VkDescriptorImageInfo image{}; // (linear, clamped, SHADER_READ_ONLY_OPTIMAL)
    std::array<float, 4> origin{}; // xyz the window's first corner (world units), w 1 when on
};

// The window brought round `camera` (asked for on a worker; the last one copied up when ready). The caller holds the lock;
// records outside a pass.
void update_air_map(Backend& state, const std::array<float, 3>& camera);
// The window as it stands.
[[nodiscard]] AirMap air_map(Backend& state);
// GLSL: `uniform sampler2D <sampler>` read through air_wind(world) / air_sky(world) / air_at(world) (vec2: wind, sky) - 1
// outside the window or without one; `origin` the block member holding AirMap::origin.
[[nodiscard]] std::string air_map_glsl(const char* sampler, const char* origin);

void release_air(Backend& state);

} // namespace render::vulkan
