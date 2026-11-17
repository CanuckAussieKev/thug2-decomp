#pragma once

#include <array>
#include <cstdint>
#include <volk.h>

#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_paint.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_rain.h"
#include "gpu_vulkan/vulkan_snow.h"
#include "gpu_vulkan/vulkan_surface_light.h"
#include "gpu_vulkan/vulkan_water.h"

// NOT ORIGINAL - a draw translated once into what its commands need (vulkan_draws.cpp). record_draw is prepare_draw
// (the game's state block read: the shaders' keys, the pipeline, the uniform blocks copied into the ring, the textures
// made ready to be sampled, the dynamic states) then emit_draw (the commands: the pipeline bound, the dynamic states
// set, the descriptors pushed, the vertex streams bound, the draw) - the translation is most of a draw's processor time.
// A draw kept for a view's shading (vulkan_surface_light.h, round V) is prepared once as it comes and emitted twice:
// its depth at once with a depth-only pipeline made from its own key (the same vertex shader, so the same depth), and
// shaded later - as a modern renderer submits one prepared draw list to its depth prepass and its base pass.
namespace render::vulkan {

// (NOT ORIGINAL - rt_pbr) A draw lit per pixel's own descriptors: its block and its surface map (vulkan_surface.h).
struct SurfaceDescriptors {
    bool on = false;
    bool has_map = false;
    VkDescriptorBufferInfo block{};
    VkDescriptorImageInfo map{};
    // (added by cel shading, vulkan_cel_draw.h) its block, and the scene its ray queries trace (VK_NULL_HANDLE: none)
    bool cel = false;
    VkDescriptorBufferInfo cel_block{};
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
    // (added by the detail materials - rt_detail / rt_delight, vulkan_detail.h) its block, and the kind's texture
    bool detail = false;
    bool has_detail_map = false;
    VkDescriptorBufferInfo detail_block{};
    VkDescriptorImageInfo detail_map{};
};

// A draw's dynamic states (set when they differ from the stream's).
struct DrawDynamics {
    VkViewport viewport{};
    VkRect2D scissor{};
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depth_test = false;
    bool depth_write = false;
    VkCompareOp depth_compare = VK_COMPARE_OP_ALWAYS;
    std::array<float, 2> depth_bias{};
    Float4 blend_constants{};
};

// The images a prepared draw samples that a later emission must find in the layout its descriptors name (the stages'
// textures, the surface map, the detail texture; null: none, or a texture destroyed since - surface_light_forget_image).
inline constexpr std::size_t kPreparedImages = kStageCount + 2;

struct PreparedDraw {
    PipelineKey key{};
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline hull = VK_NULL_HANDLE; // (added by cel shading: a lit object's outline drawn after it; none: no outline)
    DrawDynamics dynamics;
    std::array<VkDescriptorImageInfo, kStageCount> images{};
    VkDescriptorBufferInfo vertex_block{};
    VkDescriptorBufferInfo fragment_block{};
    SurfaceDescriptors surface;
    WaterDescriptors water;
    SnowDescriptors snow;
    PaintDescriptors paint;
    WetDescriptors wet;
    SurfaceInputDescriptors light;
    DrawCall call;
    bool motion = false; // (the upscaler's motion: the frame before's vertices at binding 1)
    VkBuffer previous_buffer = VK_NULL_HANDLE;
    VkDeviceSize previous_offset = 0;
    VkBuffer stream = VK_NULL_HANDLE; // (the relit light store's / cel shading's stream at binding 2; none: not bound)
    std::array<Image*, kPreparedImages> sampled{};
    uint64_t serial = 0; // the submission it was prepared in: its ring blocks and transitions hold only in that one
};

// The draw prepared (kNone: nothing to draw - skipped, or kept back by rt_water to be drawn later; kRingFull: its uniform
// blocks found no room). The caller holds the lock; the pass on the device's targets is open after it.
enum class Prepared : uint8_t { kDraw, kNone, kRingFull };
[[nodiscard]] Prepared prepare_draw(Backend& state, const GpuStateBlock& block, const DrawCall& call, PreparedDraw* out);
// What emit_draw draws a prepared draw as: as prepared, or its depth alone (the pipeline given, the light inputs not
// pushed).
struct Emission {
    VkPipeline pipeline = VK_NULL_HANDLE; // VK_NULL_HANDLE: the draw's own
    bool light_inputs = true;
};
// Its commands recorded in the stream (the pass on the device's targets opened when it isn't). The caller holds the lock.
void emit_draw(Backend& state, const PreparedDraw& draw, const Emission& how = {});

} // namespace render::vulkan
