#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"

// NOT ORIGINAL - motion vectors for the upscaler (vulkan_upscale.h; docs/design/vulkan.md "Upscaling"). While a view is
// drawn for upscaling (rt::view_begins - the upscaler's listener - until its world_drawn), every pass on the view's
// colour target gets a second colour attachment (kMotionFormat, the target's size), and every draw of a 3D position
// (not pre-transformed) gets the fixed-function emulation's motion bit (ff_key.h VertexKey::motion): its vertex shader
// hands on where the point is this frame (the jitter taken out) and where it was the frame before, its fragment shader
// writes the difference in 0 - 1 of the viewport. The frame before's position of a vertex is
//   previous position x previous world x previous view x previous projection (unjittered),
// with each part found so:
//   - the position: the vertices the CPU wrote the frame before where render_pc names them (Gpu::MotionSource - a
//     skinned mesh's buffers take turns), else this frame's (they didn't change);
//   - the world: the transform the draw's key (Gpu::MotionSource::key: the mesh and the instance) had the frame before;
//     key 0 or a key not drawn then: this frame's (the level's meshes don't move);
//   - the view: the view's camera the frame before when the draw's view is this frame's camera; its rotation alone when
//     the draw's view is the camera's rotation (the sky, drawn from the origin); else the draw's own (no motion);
//   - the projection: the camera's the frame before for the camera's own; else the draw's (the sky's), unjittered.
// The attachment's channels: x, y the move to where the pixel was (0 - 1 of the viewport: x right, y down), z how much
// the pixel is a blended draw's (particles, glows: the largest alpha a blended draw left there - the upscaler's
// reactive mask), w unused. It is cleared to x = kNoMotion: a pixel no opaque draw covered gets the camera's motion
// from its depth afterwards (the upscaler's preparation pass).
namespace render::vulkan {

struct Backend;

inline constexpr float kNoMotion = 1000.0F; // (the attachment's clear: no draw wrote the pixel's motion)

// The draws' source of vertices and identity (Gpu::set_motion_source).
struct MotionSourceState {
    uint64_t key = 0;
    Buffer* previous = nullptr;
    uint32_t previous_offset = 0;
};

// A camera: its view and its projection without the jitter (Direct3D's row-vector matrices).
struct MotionCamera {
    bool known = false;
    Float4x4 view{};
    Float4x4 projection{};
};

struct MotionVectors {
    bool active = false;        // a view is being drawn for upscaling
    Surface* target = nullptr;  // its colour and depth targets
    Surface* depth = nullptr;
    Image image;                // the attachment (the target's size)
    bool fresh = false;         // cleared when the view's first pass opens
    Float4 jitter{};            // this frame's jitter in Direct3D's clip space (x, y)
    std::array<float, 2> output_scale{1.0F, 1.0F}; // pre-transformed positions: the output's size over the drawn's
    MotionCamera now;           // the view's camera this frame and the frame before
    MotionCamera before;
    uint64_t frame = 0;         // the backend frame `now` is from
    std::unordered_map<uint64_t, Float4x4> worlds_now; // the keys' world transforms this frame / the frame before
    std::unordered_map<uint64_t, Float4x4> worlds_before;
    MotionSourceState source;
    uint64_t draws = 0;         // draws given motion (the log's)
};

// Whether a pass opened on `colour` now takes the motion attachment (a view drawn for upscaling, its target, one sample).
[[nodiscard]] bool motion_wanted(const Backend& state, const Surface* colour, const Image* colour_image);
// The motion uniforms of a draw (VertexUniforms::previous_clip, motion_jitter), its world noted under its key.
void fill_motion_uniforms(Backend& state, const GpuStateBlock& block, VertexUniforms* uniforms);
// A pre-transformed draw's pixel uniform while a scaled view is drawn: its positions are the output's pixels, put into
// the drawn part.
void scale_pre_transformed(const Backend& state, VertexUniforms* uniforms);
// The buffer and offset the draw's previous positions come from (binding 1): the motion source's, else the draw's own.
[[nodiscard]] std::pair<VkBuffer, VkDeviceSize> previous_vertices(Backend& state, VkBuffer buffer, VkDeviceSize offset);

// (added by the camera effects) The view's motion begun - by the upscaler's view_begins, and by the motion blur's when
// no upscaler took the view: the attachment made the colour target's size (`colour`), the frame before's cameras and
// worlds kept (once a frame), this frame's camera noted (its jitter, ViewDrawn::jitter, taken out), the draws given motion
// from here to the view's world_drawn. `output`: the view's output size (pre-transformed draws scaled into the drawn
// part). False (motion not active): the attachment couldn't be made.
bool begin_motion(Backend& state, const rt::ViewDrawn& view, const Image& colour, Extent output);

// D3D row-vector matrices: a x b, and the inverse (false: singular).
[[nodiscard]] Float4x4 motion_multiply(const Float4x4& a, const Float4x4& b);
bool motion_inverse(const Float4x4& matrix, Float4x4* inverse);

} // namespace render::vulkan
