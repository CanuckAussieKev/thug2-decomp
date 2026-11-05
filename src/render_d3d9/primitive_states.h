#pragma once

#include <cstdint>

#include "engine/gfx/gpu.h"

// The device states for the engine's list of 3D primitives, and thin device calls (Nx engine, PC).
namespace render {

// 0x004B60B0: blend mode 0x11, position + one texture coordinate, no pixel shader, the texture alone (colour and
// alpha), clamped addressing, alpha test from 1, z written and tested, no culling, no texture transform, texture
// coordinates 0.
void set_primitive_states();
// 0x004B5FF0: no texture, culling clockwise, the first stage back to modulate.
void reset_primitive_states();
// 0x004B6030 / 0x004B6050: the device's SetTextureStageState / SetTransform.
void set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value);
void set_transform(uint32_t type, const void* matrix);
// The same, by the Gpu's names for the state and the transform (NOT ORIGINAL).
void set_texture_stage_state(uint32_t stage, GpuStageState type, uint32_t value);
void set_transform(GpuTransform type, const void* matrix);
// The surfaces drawn to: the colour surface and the depth surface.
struct RenderTargets {
    void* surface{};
    void* depth{};
};

// 0x004B6070: the render target (0) and the depth surface set; D3DERR_INVALIDCALL if either failed, else 0.
long set_render_target_and_depth(RenderTargets targets);

} // namespace render
