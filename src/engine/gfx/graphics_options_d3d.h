#pragma once

// NOT ORIGINAL - the image-quality options' Direct3D side (graphics_options.h): the multisample choice and the
// multisampled scene surface, for the renderer's files that talk to Direct3D.
#include <cstdint>

#include "gpu/gpu_handles.h"

namespace render::graphics_options {

// Choose them (before the device is made) for this back buffer / depth format pair and window mode.
void choose_multisample(GpuInstance* api, uint32_t back_buffer_format, uint32_t depth_format, bool windowed);
// Instead of a full-size screen copy as the render target: the multisampled surface and its depth. False (nothing
// done) when not in use or for any other texture.
bool set_scene_target(GpuTexture* target);
// Before a full-size screen copy is read as a texture: what was drawn into the multisampled surface for it,
// resolved into it (only if anything was since the last time).
void resolve_scene_target(GpuTexture* target);

} // namespace render::graphics_options
