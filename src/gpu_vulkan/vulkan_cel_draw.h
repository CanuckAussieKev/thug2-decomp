#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - cel shading's side in the Vulkan Gpu (engine/gfx/rt_cel.h; docs/design/vulkan.md "Cel shading"):
//   - which draws are banded and how (cel_draw: the surface bits of ff_key.h - kSurfaceCel and its stream, shadow and
//     hull) and what their shaders read (fill_cel_uniforms: CelUniforms; the frame's scene for the ray queries);
//   - the world's draws' second vertex stream (render_pc names it per mesh: Gpu::CelSource);
//   - the world's outlines: a pass over each viewport after the upscaler (rt::ViewOrder::kCel, world_drawn): lines
//     where the linear depth bends or steps (the toon filter's measure - vulkan_style.h - one depth pixel apart x
//     rt_cel_outline_width, faded out with the distance), the ink the pixel's own colour darkened (rt::cel_ink);
//   - which of the other effects step aside: the ambient occlusion and the bounce while the mode is on, the ray-traced
//     shadows' pass while the draws trace their own.
// The caller holds the backend's lock throughout.
namespace render::vulkan {

struct Backend;
struct Buffer;
struct DrawCall;

// A draw's cel shading: its surface bits (0: none - the mode off, or a draw it leaves) and the stream to bind.
struct CelDraw {
    uint8_t flags = 0;
    Buffer* stream = nullptr; // (kSurfaceCelStream)
    bool hull = false;        // an outline hull drawn after it (kSurfaceLit draws that don't blend)
};
[[nodiscard]] CelDraw cel_draw(Backend& state, const GpuStateBlock& block, const ShaderKey& key, const DrawCall& call);
// The draw's CelUniforms (its view's camera; the scene's sun).
void fill_cel_uniforms(const Backend& state, const GpuStateBlock& block, CelUniforms* uniforms);
// The stream's buffer and offset to bind (the current version's).
[[nodiscard]] VkBuffer cel_stream_buffer(Backend& state, Buffer* stream);
// The frame's top-level acceleration structure (the scene's builder sets it each frame; VK_NULL_HANDLE: none).
void cel_set_scene_locked(VkAccelerationStructureKHR tlas);
[[nodiscard]] VkAccelerationStructureKHR cel_scene();
// render_pc's Gpu::CelSource (vulkan_gpu.cpp): the stream for the draws from `vertices` at `offset`.
void set_cel_source_locked(Backend& state, const Gpu::CelSource& source);

// Whether the mode is on (the effects that step aside ask).
[[nodiscard]] bool cel_on();
// Whether the draws trace the sun's shadows themselves now (the relit world's stream, ray tracing, a scene, a sun): the
// ray-traced shadows' pass then skips.
[[nodiscard]] bool cel_owns_shadows(const Backend& state);

// The world's outlines' pass for a view (the listener's work; also the check's). False: nothing recorded.
bool record_cel_outline_pass(Backend& state, const rt::ViewDrawn& view);

// What the draws were given since the device was made (the log; the check).
struct CelCounts {
    uint64_t world_stream = 0; // the relit world's draws with the stream
    uint64_t world_bake = 0;   // the world's draws banding their vertex colour
    uint64_t lit = 0;          // draws lit by lights
    uint64_t shadowed = 0;     // ... of all of them, those that trace the sun's visibility
    uint64_t hulls = 0;
    uint64_t outline_passes = 0;
    double outline_milliseconds = 0.0; // the outlines' pass on the device, the last measured
};
[[nodiscard]] CelCounts cel_counts();
void count_cel_draw(uint8_t flags, bool hull);

// The pass's objects released (the device's shutdown).
void release_cel(Backend& state);

} // namespace render::vulkan
