#pragma once

#include <cstdint>
#include <string>
#include <volk.h>

#include "engine/gfx/rt_clouds.h"

// NOT ORIGINAL - the volumetric clouds' model on the device (engine/gfx/rt_clouds.h is the model and its reference;
// [Graphics] rt_clouds, drawn by vulkan_rt_sky_pass): the noise textures, made once by compute passes from the same
// integer hash as the processor's (rt::cloud_shape_texel and its kin - bytes, so the two agree to a rounding), and the
// GLSL of the density and the march - rt_clouds.cpp's functions, line for line.
namespace render::vulkan {

struct Backend;

// The three textures made (once; recorded in `commands` outside a pass, a barrier after). The caller holds the lock.
// False: they can't be made (logged once).
bool update_cloud_noise(Backend& state, VkCommandBuffer commands);
// The textures as combined image samplers (linear, repeating; GENERAL layout): shape (3D), detail (3D), weather (2D).
[[nodiscard]] VkDescriptorImageInfo cloud_noise_descriptor(uint32_t which); // 0 shape, 1 detail, 2 weather
void cloud_noise_used(uint64_t serial);
// The GLSL a shader marching the clouds includes (rt_clouds.h's constants, cloud_density, march_clouds - its structs
// CloudRay and CloudResult): the textures at (set 0, `first_binding` .. + 2).
[[nodiscard]] std::string cloud_glsl(uint32_t first_binding);
// The textures read back (the check's; the frame's work finished first): rt::CloudNoise's bytes.
bool read_cloud_noise(Backend& state, rt::CloudNoise* out);
// Released (the device's shutdown; the caller holds the lock).
void release_cloud_noise(Backend& state);

} // namespace render::vulkan
