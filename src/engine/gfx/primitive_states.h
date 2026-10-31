#pragma once

#include <cstdint>

// The device states for the engine's list of 3D primitives, and thin device calls (Nx engine, PC).
namespace render {

// 0x004B60B0: blend mode 0x11, position + one texture coordinate, no pixel shader, the texture alone (colour and
// alpha), clamped addressing, alpha test from 1, z written and tested, no culling, no texture transform, texture
// coordinates 0.
void __cdecl set_primitive_states();
// 0x004B5FF0: no texture, culling clockwise, the first stage back to modulate.
void __cdecl reset_primitive_states();
// 0x004B6030 / 0x004B6050: the device's SetTextureStageState / SetTransform.
void __cdecl set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value);
void __cdecl set_transform(uint32_t type, const void* matrix);
// 0x004B6070: the render target (0) and the depth surface set; D3DERR_INVALIDCALL if either failed, else 0.
long __cdecl set_render_target_and_depth(void* surface, void* depth);

} // namespace render
