#pragma once

// NOT ORIGINAL - the image-quality options' Direct3D side (graphics_options.h): the multisample choice and the
// multisampled scene surface, for the renderer's files that talk to Direct3D.
#include <cstdint>

#include "gpu/gpu_handles.h"

namespace render::graphics_options {

// The minification filter a sampler gets where the original picks linear (`linear`) or point: anisotropic in place
// of linear when `anisotropy` is on. (D3DTEXF_* values.)
uint32_t get_min_filter(bool linear, int anisotropy);
// The device's anisotropy (device_anisotropy), set from its caps by create_render_targets; 0 = the original's filters.
int& active_anisotropy();
// The sample count and quality chosen for the device (init_direct_3d, by the msaa option); 0 samples = the option off.
int chosen_samples();
uint32_t chosen_quality();

// The multisampled scene surface (with the msaa option, while the device is multisampled): the screen copies' full
// size ones (0x00786D1C / 0x00786D20) are drawn through it. Made / released with the other default-pool objects.
void create_scene_target();
void release_scene_target();

// Choose them (before the device is made) for this back buffer / depth format pair and window mode.
void choose_multisample(GpuInstance* api, uint32_t back_buffer_format, uint32_t depth_format, bool windowed);
// Instead of a full-size screen copy as the render target: the multisampled surface and its depth. False (nothing
// done) when not in use or for any other texture.
bool set_scene_target(GpuTexture* target);
// Before a full-size screen copy is read as a texture: what was drawn into the multisampled surface for it,
// resolved into it (only if anything was since the last time).
void resolve_scene_target(GpuTexture* target);

} // namespace render::graphics_options
