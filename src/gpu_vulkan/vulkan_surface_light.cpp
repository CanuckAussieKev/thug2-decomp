// NOT ORIGINAL - round V: the world lit once per surface (vulkan_surface_light.h; docs/design/vulkan.md "Round V").
#include "gpu_vulkan/vulkan_surface_light.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/surface_trail.h"
#include "engine/gfx/water_surfaces.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_prepared_draw.h"
#include "gpu_vulkan/vulkan_restir_gi.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "gpu_vulkan/vulkan_water.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportFirstView = 0x52560001U;
constexpr uint64_t kReportNotHere = 0x52560002U;
constexpr uint64_t kReportFailed = 0x52560003U;
constexpr uint64_t kReportLeftOver = 0x52560004U;
constexpr uint64_t kReportNotRelit = 0x52560005U;

constexpr uint32_t kBlendZero = 1;         // D3DBLEND_ZERO
constexpr uint32_t kBlendOne = 2;          // D3DBLEND_ONE
constexpr uint32_t kBlendSourceAlpha = 5;  // D3DBLEND_SRCALPHA
constexpr uint32_t kBlendInverseAlpha = 6; // D3DBLEND_INVSRCALPHA
constexpr uint32_t kBlendOpAdd = 1;        // D3DBLENDOP_ADD
constexpr uint32_t kGroupSide = 8;
constexpr uint32_t kBeginBindings = 12;
constexpr std::size_t kMostKept = 16384; // (a view's world draws: a few hundred; past this, drawn as they come)

// The inputs begun: the prepass's depth copied (read as the depth surface's), the visibility "not traced", nothing
// occluded, no lamps - over the view's rectangle (rect: x, y, width, height).
constexpr const char* kBeginSource = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_depth;
layout(set = 0, binding = 1, r32f) uniform writeonly image2D o_depth;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_visibility;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_lamps;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D o_lamps_on;
layout(set = 0, binding = 5, rgba16f) uniform writeonly image2D o_decals;
layout(set = 0, binding = 6, rgba32ui) uniform writeonly uimage2D o_guides;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D o_specular;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D o_layer;
layout(set = 0, binding = 9, r32f) uniform writeonly image2D o_front_depth;
layout(set = 0, binding = 10, rgba16f) uniform writeonly image2D o_front_light;
layout(set = 0, binding = 11, rgba16f) uniform writeonly image2D o_front_sun;
layout(push_constant) uniform Pass { ivec4 rect; } pc;
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.rect.zw))) {
        return;
    }
    ivec2 p = pc.rect.xy + local;
    imageStore(o_depth, p, vec4(texelFetch(u_depth, p, 0).r, 0.0, 0.0, 0.0));
    imageStore(o_visibility, p, vec4(-1.0, 0.0, 1.0, 0.0));
    imageStore(o_lamps, p, vec4(0.0));
    imageStore(o_lamps_on, p, vec4(0.0));
    imageStore(o_decals, p, vec4(0.0, 0.0, 0.0, 1.0)); // (round DB: no layer - the surface's own material)
    imageStore(o_layer, p, vec4(0.0, 0.0, 0.0, 1.0));
    imageStore(o_guides, p, uvec4(0u));
    imageStore(o_specular, p, vec4(0.0));
    imageStore(o_front_depth, p, vec4(1.0, 0.0, 0.0, 0.0)); // (round FL: no front layer until its pass finds one)
    imageStore(o_front_light, p, vec4(0.0));
    imageStore(o_front_sun, p, vec4(0.0));
}
)";

// A draw kept for the view's shading: prepared once as it came (vulkan_prepared_draw.h - its key, pipeline, uniform
// blocks, descriptors and states as the shading draws it), its depth emitted from that at once and its shading later.
// With it the game's state block and call, and what record_draw reads besides them - the per-draw state render_pc names
// before each draw (the light store's source, the trails' ground, the water's material and body, the motion's source)
// - recorded again from them at the shading when it wasn't prepared (water rt_water shades: its copy behind the water
// is made when it is drawn) or its preparation no longer holds (the frame's work was submitted since - a read-back
// mid-view: the ring's blocks and the transitions were the submission's).
struct KeptDraw {
    GpuStateBlock block;
    DrawCall call;
    CelSourceNamed cel;
    bool ground = false;
    bool character = false;
    const GpuTexture* water_texture = nullptr;
    uint32_t water_material = 0;
    water::WaterBody water_body;
    bool water_waves = false;
    MotionSourceState motion;
    bool ready = false; // prepared
    bool water = false; // (round FL) rt_water's (recorded at the shading - its front-layer depth prepared on its own)
    PreparedDraw prepared;
};

// (round FL) The view-wide descriptor sets (set 1, ff_uniforms.h kLightInputsSet): a pool per frame slot, reset when its
// frame comes round (its last frame's work finished), a set made in it whenever the inputs' images change.
constexpr uint32_t kSetsPerFrame = 16;
struct SetSlot {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    uint64_t frame = 0;
    std::vector<std::pair<std::array<VkImageView, kLightInputCount>, VkDescriptorSet>> made; // (this frame's, by images)
};

enum class Phase : uint8_t {
    kIdle,    // no view lit per surface: draws as they come
    kKeeping, // a view begun: its world's draws kept (once decided)
    kInputs,  // its inputs begun: the passes write them
};

struct LightState {
    Phase phase = Phase::kIdle;
    SurfaceDrawRole role = SurfaceDrawRole::kAsItComes;
    bool decided = false;      // whether this view is lit per surface was decided (at its first world draw)
    bool on = false;           // ... and it is (its inputs' images made then)
    rt::ViewDrawn view;
    Surface* target = nullptr; // the view's colour target and depth at its beginning
    Surface* depth = nullptr;
    std::vector<KeptDraw> kept;
    Image depth_copy;
    Image visibility;
    Image lamps;
    Image lamps_on;
    Image decals;
    Image layer_material; // (round DB: the decals' roughness and F0 layer - SurfaceInputs::layer)
    Image guides;
    Image specular; // (path tracing, stage 5: the environment's specular - ReSTIR GI's; 0, a 0 where none)
    uint64_t guides_frame = 0; // the frame whose view wrote them (0: none)
    bool guides_forced = false; // (the checks': written without Ray Reconstruction)
    // (round RG) Ray Reconstruction's transparency layer: what the view's transparent kept draws (blended without writing
    // depth - glows, light cones and shafts, haze cards) laid over the surfaces, drawn apart from the colour (a render
    // target of the colour's format and size): premultiplied light, its alpha how much of what is behind they hide
    Surface layer;
    uint64_t layer_frame = 0;  // the frame whose view drew it (0: none)
    bool layer_forced = false; // (the checks': drawn without Ray Reconstruction)
    bool in_layer = false;     // its draws being recorded (vulkan_draws.cpp's pipeline key: the layer's alpha blend)
    std::vector<DeviceBuffer> kept_buffers; // (kept draws' vertices and indices whose buffers were written again)
    std::unordered_set<VkBuffer> kept_reads; // the buffers the kept draws read now
    // (round FL) the front layer (vulkan_front_layer.h): the see-through draws' depth (the view's depth format and size),
    // the layer's depth, light and the disc's light - two of each, this view's and the last lit one's (its history)
    Surface front;
    std::array<Image, 2> front_depth;
    std::array<Image, 2> front_light;
    std::array<Image, 2> front_sun;
    std::size_t front_now = 0;
    bool front_on = false;      // this view's see-through draws read the front layer (ReSTIR GI lights the view)
    bool front_history = false; // the other images hold the last view's lit layer
    bool front_lit = false;     // this view's layer was lit (the front layer's pass)
    std::array<SetSlot, Context::kFramesInFlight> sets;
    VkDescriptorSet view_set = VK_NULL_HANDLE; // this view's
    SurfaceInputs inputs;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline begin = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE; // (nearest, clamped: the depth, and the inputs for the draws)
    bool failed = false;
    SurfaceLightStats stats;
};

LightState& light_state()
{
    static LightState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// What a draw's blend does: nothing (off, or one x the source + zero x what is there - the game's opaque passes drawn
// with the blend on), an alpha blend (by the source's alpha), or something else (an added glow, a multiplied mark).
enum class Blend : uint8_t { kNone, kAlpha, kOther };
Blend blend_of(const GpuStateBlock& block)
{
    if (block.render_state(GpuRenderState::kAlphaBlendEnable) == 0) {
        return Blend::kNone;
    }
    const uint32_t source = block.render_state(GpuRenderState::kSrcBlend);
    const uint32_t dest = block.render_state(GpuRenderState::kDestBlend);
    if (source == kBlendOne && dest == kBlendZero) {
        return Blend::kNone;
    }
    return source == kBlendSourceAlpha && dest == kBlendInverseAlpha ? Blend::kAlpha : Blend::kOther;
}

// (round RG) Whether a draw writes depth, as the prepass takes it: the surface the light inputs are made for - every
// draw that writes depth but an added or multiplied one (an alpha-blended one by its mostly opaque part).
bool surface_draw(const GpuStateBlock& block)
{
    return block.render_state(GpuRenderState::kZEnable) != 0 && block.render_state(GpuRenderState::kZWriteEnable) != 0 &&
           blend_of(block) != Blend::kOther;
}

// (round RG) Whether a draw is a transparency laid over the surfaces: blended by its alpha without writing depth (a
// haze card, a light shaft), or adding light (a glow, a light cone, a flare: what is there kept whole, x one, its colour
// added). Not a multiplied mark (a factor on what is there, left in the colour).
bool transparency(const GpuStateBlock& block)
{
    const Blend blend = blend_of(block);
    if (blend == Blend::kAlpha) {
        return !surface_draw(block);
    }
    return blend == Blend::kOther && block.render_state(GpuRenderState::kDestBlend) == kBlendOne &&
           block.render_state(GpuRenderState::kBlendOp) == kBlendOpAdd;
}

// (round FL) Whether a draw is a see-through surface the front layer may hold: blended by its alpha, writing no depth
// (glass, a soft overlay - its light is a surface's, unlike a glow's added light). Lit per pixel and of the light model's
// world or objects too (surface_light_finish's kLightFront).
bool see_through(const GpuStateBlock& block) { return blend_of(block) == Blend::kAlpha && !surface_draw(block); }

// A kept draw's prepass pipeline: its shaded pipeline's key (`shaded`) with the fragment half depth alone
// (depth_only_fragment - `half`: a blended draw's) and nothing written to or blended into the colour; the vertex half,
// the targets and the rest the shaded draw's. VK_NULL_HANDLE when it can't be made (reported once).
VkPipeline depth_pipeline(Backend& state, const PipelineKey& shaded, bool half)
{
    PipelineKey key = shaded;
    key.shaders.fragment = depth_only_fragment(shaded.shaders.fragment, half);
    key.blend = 0;
    key.source_blend = 0;
    key.dest_blend = 0;
    key.blend_operation = 0;
    key.write_mask = 0;
    std::string error;
    const VkPipeline pipeline = state.pipelines.pipeline(key, &error);
    if (pipeline == VK_NULL_HANDLE && first_report(state, kReportFailed + 0x20)) {
        get_log("vulkan: per-surface lighting: a prepass pipeline couldn't be made (%s) - that draw's depth left out: %s / %s",
                error.c_str(), describe(key.shaders.vertex).c_str(), describe(key.shaders.fragment).c_str());
    }
    return pipeline;
}

Image* image_of_surface(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return surface->level == 0 ? &surface->texture->image : nullptr;
    case SurfaceKind::kRenderTarget:
    case SurfaceKind::kDepth:
        return &surface->image;
    case SurfaceKind::kSystemMemory:
        break;
    }
    return nullptr;
}

bool ensure_objects(Backend& state, std::string* error)
{
    LightState& own = light_state();
    if (own.begin != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBeginBindings> bindings{};
    for (uint32_t binding = 0; binding < kBeginBindings; ++binding) {
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                                      : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                       .descriptorCount = 1,
                                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = kBeginBindings,
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(std::array<int32_t, 4>)};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
        vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS) {
        *error = "the descriptor layout or the sampler";
        return false;
    }
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &constants};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, kBeginSource);
    if (!compiled.ok) {
        *error = "the inputs' shader doesn't compile: " + compiled.log;
        return false;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return false;
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = module,
                                                     .pName = "main",
                                                     .pSpecializationInfo = nullptr},
                                           .layout = own.layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &own.begin);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        own.begin = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

bool ensure_image(Backend& state, Image* image, VkFormat format, Extent extent, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->format == format && image->extent.width == extent.width &&
        image->extent.height == extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    image->resting = VK_IMAGE_LAYOUT_GENERAL;
    prepare_image(state, image);
    return true;
}

// The images the view's descriptor set holds (its inputs; round FL: the front layer's this view's too).
std::array<Image*, kLightInputCount> input_images()
{
    LightState& own = light_state();
    const std::size_t now = own.front_now;
    return {&own.visibility, &own.lamps,   &own.lamps_on, &own.depth_copy, &own.decals, &own.guides, &own.specular,
            &own.layer_material, &core::at(own.front_depth, now), &core::at(own.front_light, now),
            &core::at(own.front_sun, now)};
}

// (round FL) The front layer's depth attachment: the view's depth surface's format, size and samples (the kept draws'
// pipelines draw into it as into the view's depth), samplable.
bool ensure_front_surface(Backend& state)
{
    LightState& own = light_state();
    const Surface* const depth = own.depth;
    Image* const depth_image = image_of_surface(own.depth);
    if (depth == nullptr || depth_image == nullptr) {
        return false;
    }
    Image& image = own.front.image;
    if (image.image != VK_NULL_HANDLE && image.format == depth_image->format &&
        image.extent.width == depth_image->extent.width && image.extent.height == depth_image->extent.height) {
        return true;
    }
    retire_image(state, &image);
    const ImageRequest request{.format = depth_image->format,
                               .aspect = depth_image->aspect,
                               .extent = depth_image->extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &image)) {
        return false;
    }
    image.resting = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    prepare_image(state, &image);
    own.front.kind = SurfaceKind::kDepth;
    own.front.plan = depth->plan;
    own.front.d3d_format = depth->d3d_format;
    own.front.extent = depth->extent;
    own.front.samples = depth->samples;
    own.front.quality = depth->quality;
    return true;
}

// (round FL) The view's light inputs as set 1 (ff_uniforms.h kLightInputsSet): its frame slot's set when its images are
// the same, else a new one from the slot's pool (reset as its frame comes round - the slot's last frame is finished
// then). VK_NULL_HANDLE when it can't be made.
VkDescriptorSet light_view_set(Backend& state)
{
    LightState& own = light_state();
    const VkDevice device = state.context.device();
    SetSlot& slot = core::at(own.sets, state.frames.slot_index());
    if (slot.pool == VK_NULL_HANDLE) {
        const std::array<VkDescriptorPoolSize, 2> sizes{{
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = kSetsPerFrame * (kLightInputCount - 1)},
            {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = kSetsPerFrame},
        }};
        const VkDescriptorPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .pNext = nullptr,
                                              .flags = 0,
                                              .maxSets = kSetsPerFrame,
                                              .poolSizeCount = static_cast<uint32_t>(sizes.size()),
                                              .pPoolSizes = sizes.data()};
        if (vkCreateDescriptorPool(device, &info, nullptr, &slot.pool) != VK_SUCCESS) {
            slot.pool = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
    }
    if (slot.frame != state.frames.frame()) {
        vkResetDescriptorPool(device, slot.pool, 0);
        slot.frame = state.frames.frame();
        slot.made.clear();
    }
    const std::array<Image*, kLightInputCount> images = input_images();
    static_assert(kLightVisibilityBinding == 0 && kLightLampsBinding == 1 && kLightLampsOnBinding == 2 &&
                      kLightDepthBinding == 3 && kLightDecalsBinding == 4 && kLightGuidesBinding == 5 &&
                      kLightSpecularBinding == 6 && kLightLayerBinding == 7 && kLightFrontDepthBinding == 8 &&
                      kLightFrontBinding == 9 && kLightFrontSunBinding == 10,
                  "the images in the bindings' order");
    std::array<VkImageView, kLightInputCount> views{};
    for (std::size_t i = 0; i < images.size(); ++i) {
        core::at(views, i) = core::at(images, i)->view;
    }
    for (const auto& [made_views, made_set] : slot.made) {
        if (made_views == views) {
            return made_set;
        }
    }
    const VkDescriptorSetLayout layout = state.pipelines.light_set_layout();
    const VkDescriptorSetAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                               .pNext = nullptr,
                                               .descriptorPool = slot.pool,
                                               .descriptorSetCount = 1,
                                               .pSetLayouts = &layout};
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (layout == VK_NULL_HANDLE || vkAllocateDescriptorSets(device, &allocate, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    std::array<VkDescriptorImageInfo, kLightInputCount> infos{};
    std::array<VkWriteDescriptorSet, kLightInputCount> writes{};
    for (uint32_t i = 0; i < kLightInputCount; ++i) {
        const bool storage = i == kLightGuidesBinding;
        core::at(infos, i) = {.sampler = storage ? VK_NULL_HANDLE : own.sampler,
                              .imageView = core::at(views, i),
                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet& write = core::at(writes, i);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = i;
        write.descriptorCount = 1;
        write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &core::at(infos, i);
    }
    vkUpdateDescriptorSets(device, kLightInputCount, writes.data(), 0, nullptr);
    slot.made.emplace_back(views, set);
    return set;
}

/// The view's input images (the colour target's size) and the pipeline that begins them made - before its first draw is
// kept: the kept draws are prepared with the inputs' descriptors. False: they couldn't be (reported once).
bool make_inputs(Backend& state)
{
    LightState& own = light_state();
    std::string error;
    Image* const colour = image_of_surface(own.target);
    constexpr VkImageUsageFlags kUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (colour == nullptr || !ensure_objects(state, &error)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: per-surface lighting couldn't be made (%s) - the passes draw over the colour as before",
                    error.empty() ? "its targets" : error.c_str());
        }
        return false;
    }
    const Extent extent{.width = colour->extent.width, .height = colour->extent.height};
    if (!ensure_image(state, &own.depth_copy, VK_FORMAT_R32_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !ensure_image(state, &own.visibility, kHdrFormat, extent, kUsage) ||
        !ensure_image(state, &own.lamps, kHdrFormat, extent, kUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !ensure_image(state, &own.lamps_on, kHdrFormat, extent, kUsage) ||
        !ensure_image(state, &own.specular, kHdrFormat, extent, kUsage) ||
        !ensure_image(state, &own.decals, kHdrFormat, extent,
                      kUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !ensure_image(state, &own.layer_material, kHdrFormat, extent,
                      kUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !ensure_image(state, &own.guides, VK_FORMAT_R32G32B32A32_UINT, extent,
                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: per-surface lighting: its images couldn't be made - the passes draw over the colour as before");
        }
        return false;
    }
    // (round FL) the front layer's images: a new size starts without history
    const VkImage before = own.front_depth.front().image;
    bool front_made = ensure_front_surface(state);
    for (std::size_t k = 0; k < 2 && front_made; ++k) {
        front_made = ensure_image(state, &core::at(own.front_depth, k), VK_FORMAT_R32_SFLOAT, extent,
                                  kUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                     ensure_image(state, &core::at(own.front_light, k), kHdrFormat, extent,
                                  kUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                     ensure_image(state, &core::at(own.front_sun, k), kHdrFormat, extent,
                                  kUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    }
    if (!front_made) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: per-surface lighting: the front layer's images couldn't be made - the passes draw over the "
                    "colour as before");
        }
        return false;
    }
    if (own.front_depth.front().image != before) {
        own.front_lit = false;
    }
    own.front_now ^= 1U; // (this view's layer into the other images: the last one's is its history)
    own.front_history = own.front_lit;
    own.front_lit = false;
    own.view_set = light_view_set(state);
    if (own.view_set == VK_NULL_HANDLE) {
        if (first_report(state, kReportFailed + 0x30)) {
            get_log("vulkan: per-surface lighting: the light inputs' descriptor set couldn't be made - this view's passes "
                    "draw over the colour as before");
        }
        return false;
    }
    return true;
}

// Whether the view now begun can be lit per surface (asked at its first world draw: by then its targets are bound and
// the frame's scene is built) - its inputs' images made when it can.
bool view_can_be_lit(Backend& state)
{
    LightState& own = light_state();
    if (own.failed || !state.context.ray_tracing_enabled() || !state.linear_phase) {
        if (!own.failed && first_report(state, kReportNotRelit + 1)) {
            get_log("vulkan: per-surface lighting: a view not lit per surface (%s) - drawn as before",
                    state.linear_phase ? "no ray tracing" : "not in the linear frame");
        }
        return false;
    }
    Image* const colour = image_of_surface(own.target);
    Image* const depth = image_of_surface(own.depth);
    if (colour == nullptr || depth == nullptr || colour->format != kHdrFormat || colour->samples != VK_SAMPLE_COUNT_1_BIT ||
        depth->samples != VK_SAMPLE_COUNT_1_BIT || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || depth->view == VK_NULL_HANDLE ||
        depth->extent.width < colour->extent.width || depth->extent.height < colour->extent.height) {
        if (first_report(state, kReportNotHere)) {
            get_log("vulkan: per-surface lighting: a view whose targets can't take it (multisampled, not the linear "
                    "frame, or no samplable depth) - its passes draw over the colour as before");
        }
        return false;
    }
    // (cel shading bands the light itself - its own path)
    const bool can = current_scene(state).tlas != VK_NULL_HANDLE && rt::cel_world().relit && !rt::cel_settings().on;
    if (!can && first_report(state, kReportNotRelit)) {
        get_log("vulkan: per-surface lighting: a view not lit per surface (%s) - drawn as before",
                current_scene(state).tlas == VK_NULL_HANDLE ? "no scene to trace yet"
                : !rt::cel_world().relit                    ? "the level isn't relit"
                                                            : "cel shading");
    }
    // (round FL) the front layer is lit where ReSTIR GI publishes the frame's light list and cache (vulkan_front_layer.h)
    own.front_on = can && restir_gi_chosen(state);
    return can && make_inputs(state);
}

// The view's depth (or `depth`: round FL's front layer's) cleared to the far plane (as the draws' passes, its stencil
// isn't attached) - outside a pass.
void clear_depth(Backend& state, Image* depth = nullptr)
{
    if (depth == nullptr) {
        depth = image_of_surface(light_state().depth);
    }
    if (depth == nullptr || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0) {
        return;
    }
    // (a pass of its own that clears the depth as it opens - the depth surfaces are attachments, not transfer targets)
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_info.imageView = depth->attachment_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_info.clearValue.depthStencil = {.depth = 1.0F, .stencil = 0};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {.offset = {.x = 0, .y = 0}, .extent = {.width = depth->extent.width, .height = depth->extent.height}};
    info.layerCount = 1;
    info.pDepthAttachment = &depth_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdEndRendering(commands);
    depth->used = state.frames.serial();
    state.bound = BoundState{};
}

// Whether a kept draw's preparation still holds: prepared in this submission (a read-back mid-view submits the frame's
// work and starts the ring again).
bool prepared_now(const Backend& state, const KeptDraw& draw)
{
    return draw.ready && draw.prepared.serial == state.frames.serial();
}

// The textures the prepared draws sample put back in the layout their descriptors name (each was there when it was
// prepared; a texture drawn into or written since has moved) - outside a pass, before the draws.
void restore_sampled_layouts(Backend& state, const std::vector<KeptDraw>& kept)
{
    VkCommandBuffer commands = VK_NULL_HANDLE;
    for (const KeptDraw& draw : kept) {
        if (!prepared_now(state, draw)) {
            continue;
        }
        for (Image* image : draw.prepared.sampled) {
            if (image == nullptr || image->image == VK_NULL_HANDLE || image->layout == image->resting) {
                continue;
            }
            if (commands == VK_NULL_HANDLE) {
                commands = outside_pass(state);
            }
            Memory::transition(commands, image, image->resting);
        }
    }
}

// (round RG) The view's transparency layer made (the colour target's format and size) and cleared; false when it
// couldn't be (the transparencies then stay in the colour, as before).
bool begin_layer(Backend& state)
{
    LightState& own = light_state();
    const Image* const colour = image_of_surface(own.target);
    if (colour == nullptr || colour->samples != VK_SAMPLE_COUNT_1_BIT) {
        return false;
    }
    Image& image = own.layer.image;
    const Extent extent{.width = colour->extent.width, .height = colour->extent.height};
    constexpr VkImageUsageFlags kLayerUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!ensure_image(state, &image, colour->format, extent, kLayerUsage)) {
        return false;
    }
    own.layer.kind = SurfaceKind::kRenderTarget;
    own.layer.extent = extent;
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, &image, VK_IMAGE_LAYOUT_GENERAL);
    const VkClearColorValue none{};
    const VkImageSubresourceRange all{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    vkCmdClearColorImage(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, &none, 1, &all);
    Memory::transition(commands, &image, VK_IMAGE_LAYOUT_GENERAL, true);
    image.used = state.frames.serial();
    return true;
}

// The kept draws shaded in their order with the view's light inputs: each prepared one emitted as prepared, the others
// recorded again from their state blocks with render_pc's per-draw state named for each (and put back after).
void draw_kept(Backend& state)
{
    LightState& own = light_state();
    std::vector<KeptDraw> kept;
    kept.swap(own.kept);
    own.kept_reads.clear();
    restore_sampled_layouts(state, kept);
    // The depth cleared: the draws are made again as the game made them, each with its own depth test and write, in its
    // order - the prepass's depth (the nearest surface, translucent ones' mostly opaque parts too: what the inputs were
    // made for) would hide what a translucent layer drawn later lets through (a shirt's print, a road's edge).
    if (!kept.empty()) {
        clear_depth(state);
    }
    // (round RG) Under Ray Reconstruction the transparencies are drawn last, into its transparency layer (NVIDIA's DLSS-RR
    // Integration Guide 3.4.10, "Transparency Overlay": particles and other transparent effects rendered into a buffer
    // of their own instead of the input colour - upscaled, not denoised, and laid over the denoised picture). A glow's
    // or a haze card's light is no surface's lit by the guides' albedo, and denoised as one it was taken for noise
    // (Training's far hall: black blotches with burning edges round the hanging lights and their light shafts). Drawn
    // after every surface, against their whole depth, in their own order - the layer is laid over everything, as Ray
    // Reconstruction composes it.
    const bool layered = (ray_reconstruction_active() || own.layer_forced) &&
                         std::ranges::any_of(kept, [](const KeptDraw& draw) { return transparency(draw.block); }) &&
                         begin_layer(state);
    const bool any_recorded = layered || std::ranges::any_of(kept, [&state](const KeptDraw& draw) {
                                  return !prepared_now(state, draw);
                              });
    const CelSourceNamed cel_before = any_recorded ? cel_source_now() : CelSourceNamed{};
    const bool ground_before = surface_trail::current_ground();
    const bool character_before = surface_trail::current_character();
    const GpuTexture* const water_before = water::current_texture();
    const uint32_t material_before = water::current_material();
    const water::WaterBody body_before = water::current_body();
    const bool waves_before = water::current_waves();
    const MotionSourceState motion_before = state.motion.source;
    own.role = SurfaceDrawRole::kShaded;
    const auto record_again = [&state, &own](const KeptDraw& draw) {
        cel_source_put(draw.cel);
        surface_trail::set_current_ground(draw.ground);
        surface_trail::set_current_character(draw.character);
        water::set_current(draw.water_texture, draw.water_material);
        water::set_current_body(draw.water_body);
        water::set_current_waves(draw.water_waves);
        state.motion.source = draw.motion;
        (void)record_draw(state, draw.block, draw.call);
        ++own.stats.recorded;
    };
    for (KeptDraw& draw : kept) {
        if (layered && transparency(draw.block)) {
            continue;
        }
        ++own.stats.shaded;
        if (prepared_now(state, draw)) {
            emit_draw(state, draw.prepared);
            continue;
        }
        record_again(draw);
    }
    if (layered) {
        // into the layer, recorded again for its target (no motion attachment there: what they cover is the layer's
        // alpha, not the reactive mask's) with its alpha blend (vulkan_pipelines.h PipelineKey::layer)
        Surface* const colour = core::at(state.targets, 0);
        core::at(state.targets, 0) = &own.layer;
        state.bound = BoundState{};
        own.in_layer = true;
        for (KeptDraw& draw : kept) {
            if (transparency(draw.block)) {
                ++own.stats.shaded;
                ++own.stats.layered;
                record_again(draw);
            }
        }
        own.in_layer = false;
        (void)outside_pass(state); // (the layer's pass ended)
        core::at(state.targets, 0) = colour;
        state.bound = BoundState{};
        own.layer_frame = state.frames.serial();
    }
    own.role = SurfaceDrawRole::kAsItComes;
    for (DeviceBuffer& copy : own.kept_buffers) {
        retire_buffer(state, &copy); // (freed once this frame's submission is done)
    }
    own.kept_buffers.clear();
    if (any_recorded) {
        cel_source_put(cel_before);
        surface_trail::set_current_ground(ground_before);
        surface_trail::set_current_character(character_before);
        water::set_current(water_before, material_before);
        water::set_current_body(body_before);
        water::set_current_waves(waves_before);
        state.motion.source = motion_before;
    }
    kept.clear();
    if (own.kept.empty()) {
        own.kept.swap(kept); // (the vector's storage kept for the next view)
    }
}

bool begin_inputs(Backend& state);

// A view's leftovers shaded (a view that ended without its opaque_drawn): its inputs begun - nothing traced, no lamps,
// no decals - and its draws shaded with them.
void flush(Backend& state)
{
    LightState& own = light_state();
    if (!own.kept.empty()) {
        if (first_report(state, kReportLeftOver)) {
            get_log("vulkan: per-surface lighting: a view's kept draws shaded without their passes' inputs (the view ended "
                    "before its opaque world was told)");
        }
        if (own.phase == Phase::kKeeping) {
            (void)begin_inputs(state);
        }
        VkCommandBuffer commands = outside_pass(state);
        for (Image* image : input_images()) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
        }
        state.bound = BoundState{};
        draw_kept(state);
    }
    own.phase = Phase::kIdle;
    own.inputs = {};
    own.decided = false;
    own.on = false;
}

// ViewOrder::kSurfaceInputs: the inputs begun from the prepass's depth.
bool begin_inputs(Backend& state)
{
    LightState& own = light_state();
    Image* const colour = image_of_surface(own.target);
    Image* const depth = image_of_surface(own.depth);
    if (colour == nullptr || depth == nullptr || own.begin == VK_NULL_HANDLE || own.visibility.image == VK_NULL_HANDLE) {
        return false;
    }
    const Extent extent{.width = own.visibility.extent.width, .height = own.visibility.extent.height};
    const uint32_t left = std::min(core::at(own.view.rect, 0), extent.width);
    const uint32_t top = std::min(core::at(own.view.rect, 1), extent.height);
    const uint32_t width = std::min(core::at(own.view.rect, 2), extent.width - left);
    const uint32_t height = std::min(core::at(own.view.rect, 3), extent.height - top);
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    for (Image* image : input_images()) {
        Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.begin);
    const VkDescriptorImageInfo depth_info{
        .sampler = own.sampler, .imageView = depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const std::size_t now = own.front_now;
    const std::array<VkDescriptorImageInfo, kBeginBindings - 1> storage{{
        {.sampler = VK_NULL_HANDLE, .imageView = own.depth_copy.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.visibility.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.lamps.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.lamps_on.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.decals.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.guides.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.specular.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.layer_material.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = core::at(own.front_depth, now).view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = core::at(own.front_light, now).view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = core::at(own.front_sun, now).view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    }};
    std::array<VkWriteDescriptorSet, kBeginBindings> writes{};
    for (uint32_t binding = 0; binding < kBeginBindings; ++binding) {
        VkWriteDescriptorSet& write = core::at(writes, binding);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = binding == 0 ? &depth_info : &core::at(storage, binding - 1);
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, kBeginBindings, writes.data());
    const std::array<int32_t, 4> rect{static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width),
                                      static_cast<int32_t>(height)};
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), rect.data());
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &written,
                         0, nullptr, 0, nullptr);
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    for (Image* image : input_images()) {
        image->used = serial;
    }
    depth->used = serial;
    own.inputs = {.visibility = &own.visibility,
                  .lamps = &own.lamps,
                  .lamps_on = &own.lamps_on,
                  .depth = &own.depth_copy,
                  .decals = &own.decals,
                  .layer = &own.layer_material,
                  .guides = &own.guides,
                  .specular = &own.specular};
    return true;
}

// The listener: the view's beginning, its inputs begun, its draws shaded, its end.
class LightListener final : public rt::ViewListener {
public:
    explicit LightListener(bool shading) : shading_(shading) {}
    [[nodiscard]] int order() const override
    {
        return shading_ ? rt::ViewOrder::kSurfaceShading : rt::ViewOrder::kSurfaceInputs;
    }
    void view_begins(const rt::ViewDrawn& view) override
    {
        if (shading_) {
            return;
        }
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.device_made || !state.draws_made) {
            return;
        }
        flush(state);
        LightState& own = light_state();
        own.phase = Phase::kKeeping;
        own.view = view;
        own.target = core::at(state.targets, 0);
        own.depth = state.depth;
        own.decided = false;
        own.on = false;
    }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        LightState& own = light_state();
        if (!state.device_made || !state.draws_made) {
            return;
        }
        if (!shading_) {
            if (own.phase != Phase::kKeeping || !own.on) {
                flush(state);
                return;
            }
            if (!begin_inputs(state)) {
                flush(state);
                return;
            }
            own.phase = Phase::kInputs;
            return;
        }
        if (own.phase != Phase::kInputs) {
            return;
        }
        // the passes' writes read by the draws
        VkCommandBuffer commands = outside_pass(state);
        for (Image* image : input_images()) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
        }
        own.guides_frame = ray_reconstruction_active() || own.guides_forced ? state.frames.serial() : 0;
        own.phase = Phase::kIdle;
        state.bound = BoundState{}; // (the inputs pushed afresh: draw_kept's draws then keep them)
        const SurfaceLightStats before = own.stats;
        const std::size_t kept = own.kept.size();
        draw_kept(state);
        if (own.stats.views <= 3 || own.stats.views % 3600 == 0) {
            get_log("vulkan: per-surface lighting: view %llu: %zu draws kept, %llu prepassed so far, %llu shaded now (%llu of "
                    "them recorded again); %llu world draws into other targets, %llu buffers kept before a write so far",
                    static_cast<unsigned long long>(own.stats.views), kept,
                    static_cast<unsigned long long>(own.stats.prepassed),
                    static_cast<unsigned long long>(own.stats.shaded - before.shaded),
                    static_cast<unsigned long long>(own.stats.recorded - before.recorded),
                    static_cast<unsigned long long>(own.stats.elsewhere),
                    static_cast<unsigned long long>(own.stats.rewritten));
        }
        own.inputs = {};
        own.decided = false;
        own.on = false;
        // (Ray Reconstruction's albedo guide: the world now shaded - vulkan_upscale.h)
        record_reconstruction_albedo(state, own.view);
    }
    void world_drawn(const rt::ViewDrawn& /*view*/) override
    {
        if (shading_) {
            return;
        }
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            flush(state);
        }
    }

private:
    bool shading_ = false;
};

LightListener& inputs_listener()
{
    static LightListener instance(false);
    return instance;
}

LightListener& shading_listener()
{
    static LightListener instance(true);
    return instance;
}

} // namespace

void install_surface_light()
{
    rt::add_view_listener(&inputs_listener());
    rt::add_view_listener(&shading_listener());
}

void uninstall_surface_light()
{
    rt::remove_view_listener(&inputs_listener());
    rt::remove_view_listener(&shading_listener());
}

void release_surface_light(Backend& state)
{
    LightState& own = light_state();
    own.kept.clear();
    own.phase = Phase::kIdle;
    own.role = SurfaceDrawRole::kAsItComes;
    own.inputs = {};
    for (Image* image : {&own.depth_copy, &own.visibility, &own.lamps, &own.lamps_on, &own.decals, &own.layer_material,
                         &own.guides, &own.specular, &own.layer.image, &own.front.image, &own.front_depth.front(),
                         &own.front_depth.back(), &own.front_light.front(), &own.front_light.back(),
                         &own.front_sun.front(), &own.front_sun.back()}) {
        if (image->image != VK_NULL_HANDLE) {
            state.memory.destroy_image(image);
        }
        *image = Image{};
    }
    own.layer_frame = 0;
    own.front_history = false;
    own.front_lit = false;
    own.view_set = VK_NULL_HANDLE;
    const VkDevice device = state.context.device();
    if (device != VK_NULL_HANDLE) {
        for (SetSlot& slot : own.sets) { // (round FL: the view-wide descriptor sets)
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, slot.pool, nullptr);
            }
            slot = SetSlot{};
        }
        if (own.begin != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.begin, nullptr);
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
    }
    own.begin = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.failed = false;
}

bool surface_light_keep(Backend& state, const GpuStateBlock& block, const DrawCall& call)
{
    LightState& own = light_state();
    if (own.phase != Phase::kKeeping || own.role != SurfaceDrawRole::kAsItComes || !rt::world_surfaces_drawing()) {
        return false;
    }
    if (core::at(state.targets, 0) != own.target || state.depth != own.depth) {
        own.stats.elsewhere += own.on ? 1U : 0U;
        return false;
    }
    if (!own.decided) {
        own.decided = true;
        own.on = view_can_be_lit(state);
        if (own.on) {
            ++own.stats.views;
            if (first_report(state, kReportFirstView)) {
                get_log("vulkan: per-surface lighting: the first view lit per surface (%ux%u) - its world kept, its "
                        "depth drawn first, its light inputs made, then its draws shaded",
                        core::at(own.view.rect, 2), core::at(own.view.rect, 3));
            }
        }
    }
    if (!own.on || own.kept.size() >= kMostKept) {
        return false;
    }
    KeptDraw& kept = own.kept.emplace_back();
    kept.block = block;
    kept.call = call;
    own.kept_reads.insert(call.vertex_buffer);
    if (call.indexed) {
        own.kept_reads.insert(call.index_buffer);
    }
    kept.cel = cel_source_now();
    kept.ground = surface_trail::current_ground();
    kept.character = surface_trail::current_character();
    kept.water_texture = water::current_texture();
    kept.water_material = water::current_material();
    kept.water_body = water::current_body();
    kept.water_waves = water::current_waves();
    kept.motion = state.motion.source;
    kept.ready = false;
    ++own.stats.kept;
    // (water rt_water shades: recorded at the shading, its copy behind the water made then - never in the prepass)
    if (water_candidate(state, block)) {
        kept.water = true;
        return true;
    }
    // Prepared once as the shading will draw it (its key with the light inputs, its pipeline, blocks and descriptors)
    own.role = SurfaceDrawRole::kShaded;
    const Prepared prepared = prepare_draw(state, block, call, &kept.prepared);
    own.role = SurfaceDrawRole::kAsItComes;
    if (prepared != Prepared::kDraw) {
        own.kept.pop_back(); // (nothing to draw: the shading would have skipped it too)
        --own.stats.kept;
        return true;
    }
    kept.ready = true;
    // The prepass: the nearest surface the light inputs are made for - every draw that writes depth but an added or
    // multiplied one, an alpha-blended one by its mostly opaque part - emitted at once from the same preparation with its
    // depth-only pipeline (no colour, no blend, no light inputs; the draws are shaded later over a cleared depth)
    const Blend blend = blend_of(block);
    const bool writes_depth = block.render_state(GpuRenderState::kZEnable) != 0 &&
                              block.render_state(GpuRenderState::kZWriteEnable) != 0 && blend != Blend::kOther;
    if (writes_depth) {
        const VkPipeline depth = depth_pipeline(state, kept.prepared.key, blend != Blend::kNone);
        if (depth != VK_NULL_HANDLE) {
            emit_draw(state, kept.prepared, {.pipeline = depth, .light_inputs = false});
            ++own.stats.prepassed;
        }
    }
    return true;
}

SurfaceDrawRole surface_draw_role() { return light_state().role; }

void surface_light_force(const GpuStateBlock& block, bool relit, ShaderKey* shaders)
{
    (void)block;
    if (light_state().role == SurfaceDrawRole::kAsItComes) {
        return;
    }
    FragmentKey& fragment = shaders->fragment;
    if (fragment.surface != 0 || fragment.water != 0U || fvf_pre_transformed(shaders->vertex.fvf)) {
        return; // (lit per pixel already, water, or not a 3D draw)
    }
    const VertexKey& vertex = shaders->vertex;
    if (relit) {
        fragment.surface = kSurfaceLighting; // (record_draw adds kSurfaceRelit with the store's words)
    } else if (vertex.lighting && vertex.light_count > 0 && (vertex.fvf & kFvfNormal) != 0U) {
        fragment.surface = kSurfaceLighting | kSurfaceLit;
    } else {
        return;
    }
    *shaders = normalised(*shaders); // (a program that can't take it: cleared again)
}

void surface_light_finish(Backend& state, const GpuStateBlock& block, ShaderKey* shaders, SurfaceInputDescriptors* out)
{
    *out = {};
    LightState& own = light_state();
    FragmentKey& fragment = shaders->fragment;
    // (the view's inputs' images: made when it was found lit per surface - the draws are prepared with them before the
    // passes write them)
    if (own.role != SurfaceDrawRole::kShaded || !own.on || own.visibility.image == VK_NULL_HANDLE || own.view_set == VK_NULL_HANDLE ||
        fvf_pre_transformed(shaders->vertex.fvf) || (fragment.surface & (kSurfaceCel | kSurfaceCelHull)) != 0U) {
        return;
    }
    if (fragment.water != 0U) {
        // (round FL) rt_water's water reads the front layer alone - its body lit by the light list there
        if (own.front_on) {
            fragment.light = kLightFront;
            out->on = true;
            out->set = own.view_set;
        }
        return;
    }
    // (round RG) A draw that adds or multiplies - a glow, a light cone, a flare, a multiplied mark - is never the surface
    // the inputs were made for (the prepass leaves it out), even where its depth meets that surface's: it reads none of
    // them (lit per pixel, it added the surface's lamps and traced specular a second time).
    if (blend_of(block) == Blend::kOther) {
        return;
    }
    if ((fragment.surface & kSurfaceLighting) != 0U) {
        fragment.light = kLightInputs;
    } else if (fragment.surface == 0) {
        // (a colour lit already: its light by shares)
        fragment.light = kLightShare;
    } else {
        return;
    }
    out->on = true;
    out->set = own.view_set;
    // (Ray Reconstruction's guides: written while it denoises the view - by the surfaces the prepass drew alone: a
    // transparency over one leaves its guides, its light laid over in the transparency layer - NVIDIA's DLSS-RR
    // Integration Guide 3.4.1: the guides describe the opaque surface)
    if ((ray_reconstruction_active() || own.guides_forced) && surface_draw(block)) {
        fragment.light = static_cast<uint8_t>(fragment.light | kLightGuides);
    }
    // (round FL) a see-through surface lit by the light model - the relit world's glass and overlays, an object's
    // windows - reads the front layer where it is the nearest see-through surface (its prepass: surface_light_front_prepass)
    if (own.front_on && fragment.light == kLightInputs && see_through(block) &&
        (fragment.surface & (kSurfaceRelit | kSurfaceLit)) != 0U) {
        fragment.light = static_cast<uint8_t>(fragment.light | kLightFront);
    }
    (void)state;
}

bool surface_light_front_prepass(Backend& state, FrontLayerImages* out)
{
    *out = {};
    LightState& own = light_state();
    if (own.phase != Phase::kInputs || !own.front_on || own.front.image.image == VK_NULL_HANDLE ||
        own.inputs.depth == nullptr) {
        return false;
    }
    const auto drawn_here = [&state](const KeptDraw& draw) {
        return draw.water ||
               (prepared_now(state, draw) && (draw.prepared.key.shaders.fragment.light & kLightFront) != 0U);
    };
    if (std::ranges::none_of(own.kept, drawn_here)) {
        return false;
    }
    restore_sampled_layouts(state, own.kept);
    clear_depth(state, &own.front.image);
    // The see-through draws' depth alone into the front layer's attachment, nearest kept (each tests and writes depth;
    // all of a draw's pixels - the layer is where the nearest see-through surface is, however clear): the prepared ones
    // emitted from their preparations with their depth-only pipelines, rt_water's prepared now as plain draws (the
    // water's rest surface - its shading finds the layer along the view within its waves' reach).
    Surface* const depth_before = state.depth;
    state.depth = &own.front;
    state.bound = BoundState{};
    const CelSourceNamed cel_before = cel_source_now();
    const bool ground_before = surface_trail::current_ground();
    const bool character_before = surface_trail::current_character();
    const GpuTexture* const water_before = water::current_texture();
    const uint32_t material_before = water::current_material();
    const water::WaterBody body_before = water::current_body();
    const bool waves_before = water::current_waves();
    const MotionSourceState motion_before = state.motion.source;
    uint32_t drawn = 0;
    for (const KeptDraw& draw : own.kept) {
        if (!drawn_here(draw)) {
            continue;
        }
        PreparedDraw front;
        if (draw.water) {
            cel_source_put(draw.cel);
            surface_trail::set_current_ground(draw.ground);
            surface_trail::set_current_character(draw.character);
            water::set_current(draw.water_texture, draw.water_material);
            water::set_current_body(draw.water_body);
            water::set_current_waves(draw.water_waves);
            state.motion.source = draw.motion;
            own.role = SurfaceDrawRole::kFrontDepth;
            const Prepared prepared = prepare_draw(state, draw.block, draw.call, &front);
            own.role = SurfaceDrawRole::kAsItComes;
            if (prepared != Prepared::kDraw) {
                continue;
            }
        } else {
            front = draw.prepared;
        }
        front.dynamics.depth_test = true;
        front.dynamics.depth_write = true;
        front.dynamics.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
        const VkPipeline pipeline = depth_pipeline(state, front.key, false);
        if (pipeline != VK_NULL_HANDLE) {
            emit_draw(state, front, {.pipeline = pipeline, .light_inputs = false});
            ++drawn;
        }
    }
    cel_source_put(cel_before);
    surface_trail::set_current_ground(ground_before);
    surface_trail::set_current_character(character_before);
    water::set_current(water_before, material_before);
    water::set_current_body(body_before);
    water::set_current_waves(waves_before);
    state.motion.source = motion_before;
    (void)outside_pass(state); // (the front layer's pass ended)
    state.depth = depth_before;
    state.bound = BoundState{};
    own.stats.front += drawn;
    if (drawn == 0) {
        return false;
    }
    const std::size_t now = own.front_now;
    const std::size_t before = now ^ 1U;
    out->drawn = &own.front.image;
    out->opaque = own.inputs.depth;
    out->depth = {&core::at(own.front_depth, now), &core::at(own.front_depth, before)};
    out->light = {&core::at(own.front_light, now), &core::at(own.front_light, before)};
    out->sun = {&core::at(own.front_sun, now), &core::at(own.front_sun, before)};
    out->history = own.front_history;
    return true;
}

void surface_light_front_lit() { light_state().front_lit = true; }

bool surface_light_front_images(FrontLayerImages* out)
{
    LightState& own = light_state();
    *out = {};
    if (own.front_depth.front().image == VK_NULL_HANDLE) {
        return false;
    }
    const std::size_t now = own.front_now;
    const std::size_t before = now ^ 1U;
    out->drawn = &own.front.image;
    out->opaque = &own.depth_copy;
    out->depth = {&core::at(own.front_depth, now), &core::at(own.front_depth, before)};
    out->light = {&core::at(own.front_light, now), &core::at(own.front_light, before)};
    out->sun = {&core::at(own.front_sun, now), &core::at(own.front_sun, before)};
    out->history = own.front_lit;
    return true;
}

Image* surface_light_lamps_input()
{
    LightState& own = light_state();
    return own.lamps.image != VK_NULL_HANDLE ? &own.lamps : nullptr;
}

const SurfaceInputs* surface_inputs(Backend& state)
{
    (void)state;
    const LightState& own = light_state();
    return own.phase == Phase::kInputs && own.inputs.visibility != nullptr ? &own.inputs : nullptr;
}

bool surface_light_view(const Backend& state)
{
    (void)state;
    const LightState& own = light_state();
    return own.on && (own.phase == Phase::kKeeping || own.phase == Phase::kInputs);
}

void surface_light_buffer_rewritten(Backend& state, VkBuffer buffer, VkDeviceSize bytes, VkCommandBuffer commands,
                                    const void* mapped)
{
    LightState& own = light_state();
    if (own.kept.empty() || buffer == VK_NULL_HANDLE || bytes == 0) {
        return;
    }
    if (!own.kept_reads.contains(buffer)) {
        return;
    }
    DeviceBuffer copy;
    constexpr VkBufferUsageFlags kUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!state.memory.create_buffer(bytes, kUsage, commands != nullptr ? BufferPlace::kDevice : BufferPlace::kUpload,
                                    &copy)) {
        if (first_report(state, kReportFailed + 0x10)) {
            get_log("vulkan: per-surface lighting: a kept draw's buffer couldn't be kept before it was written again");
        }
        return;
    }
    if (commands != nullptr) {
        const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = bytes};
        vkCmdCopyBuffer(commands, buffer, copy.buffer, 1, &region);
        // (the copy read before the write that follows it writes)
        const VkMemoryBarrier done{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                   .pNext = nullptr,
                                   .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                                                    VK_ACCESS_INDEX_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &done, 0, nullptr,
                             0, nullptr);
    } else if (mapped != nullptr && copy.mapped != nullptr) {
        std::memcpy(copy.mapped, mapped, static_cast<std::size_t>(bytes));
        state.memory.flush(copy, 0, bytes);
    }
    for (KeptDraw& kept : own.kept) {
        for (DrawCall* call : {&kept.call, &kept.prepared.call}) {
            if (call->vertex_buffer == buffer) {
                call->vertex_buffer = copy.buffer;
            }
            if (call->indexed && call->index_buffer == buffer) {
                call->index_buffer = copy.buffer;
            }
        }
    }
    own.kept_buffers.push_back(copy);
    own.kept_reads.erase(buffer); // (no kept draw reads it now)
    ++own.stats.rewritten;
}

void set_surface_light_guides_forced(bool forced) { light_state().guides_forced = forced; }

void set_surface_light_layer_forced(bool forced) { light_state().layer_forced = forced; }

Image* surface_light_layer(Backend& state)
{
    LightState& own = light_state();
    return own.layer_frame != 0 && own.layer_frame == state.frames.serial() && own.layer.image.image != VK_NULL_HANDLE
               ? &own.layer.image
               : nullptr;
}

bool surface_light_in_layer() { return light_state().in_layer; }

Image* surface_light_guides(Backend& state)
{
    LightState& own = light_state();
    return own.guides_frame != 0 && own.guides_frame == state.frames.serial() && own.guides.image != VK_NULL_HANDLE
               ? &own.guides
               : nullptr;
}

void surface_light_forget_buffer(const void* buffer)
{
    for (KeptDraw& kept : light_state().kept) {
        if (buffer == nullptr || kept.cel.vertices == buffer || kept.cel.stream == buffer) {
            kept.cel = {};
        }
        if (buffer == nullptr || kept.motion.previous == buffer) {
            kept.motion.previous = nullptr;
        }
    }
}

void surface_light_forget_image(const Image* image)
{
    for (KeptDraw& kept : light_state().kept) {
        for (Image*& sampled : kept.prepared.sampled) {
            if (sampled == image) {
                sampled = nullptr;
            }
        }
    }
}

SurfaceLightStats surface_light_stats() { return light_state().stats; }

} // namespace render::vulkan
