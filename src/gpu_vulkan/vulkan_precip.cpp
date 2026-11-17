// NOT ORIGINAL - the precipitation occlusion map on the device (vulkan_precip.h; engine/gfx/precip_occlusion.h).
#include "gpu_vulkan/vulkan_precip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/precip_occlusion.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportNoTracing = 0x50524301U;
constexpr uint64_t kReportFailed = 0x50524302U;
constexpr uint64_t kReportFirst = 0x50524303U;
constexpr VkFormat kMapFormat = VK_FORMAT_R32G32_UINT;
constexpr std::size_t kTexelBytes = 8;
constexpr uint32_t kGroup = 8;

// The trace: each texel of each cascade (z) - the absolute line its slot holds now; traced when its tag is another's
// (new to the window, a new generation) or it is in the frame's slice.
constexpr const char* kTraceSource = R"(#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rg32ui) uniform uimage2D u_map;
layout(set = 0, binding = 1) uniform accelerationStructureEXT u_scene;
layout(push_constant) uniform Trace {
    ivec4 first;     // xy the near window's first texel, zw the far one's
    vec4 texels;     // x the near texel, y the far one, z the camera's height, w the generation
    vec4 direction;  // xyz the fall's direction, w the frame (its slice: mod SLICES)
    vec4 slant;      // xy the slant, z the reference height, w unused
} pc;
uint tag_of(ivec2 t, uint generation)
{
    return (uint(t.x) & 0xFFFu) | ((uint(t.y) & 0xFFFu) << 12u) | ((generation & 0x7Fu) << 24u) | 0x80000000u;
}
void main()
{
    uint layer = gl_GlobalInvocationID.z;
    ivec2 slot = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(slot, ivec2(SIZE)))) return;
    ivec2 first = layer == 0u ? pc.first.xy : pc.first.zw;
    ivec2 line = first + ivec2(uvec2(slot - first) % uvec2(SIZE));
    uint generation = uint(pc.texels.w);
    uint tag = tag_of(line, generation);
    ivec2 at = ivec2(slot.x + int(layer) * SIZE, slot.y);
    uvec2 old = imageLoad(u_map, at).rg;
    uint slice = (uint(line.x) + uint(line.y) * 7u) % uint(SLICES);
    if (old.g == tag && slice != uint(pc.direction.w)) return;
    float texel = layer == 0u ? pc.texels.x : pc.texels.y;
    vec2 uv = (vec2(line) + 0.5) * texel;
    float top = pc.texels.z + ABOVE;
    float rise = top - pc.slant.z;
    vec3 origin = vec3(uv.x - pc.slant.x * rise, top, uv.y - pc.slant.y * rise);
    vec3 d = pc.direction.xyz;
    float reach = (ABOVE + BELOW) / max(-d.y, 1.0e-3);
    rayQueryEXT q;
    rayQueryInitializeEXT(q, u_scene, gl_RayFlagsOpaqueEXT, MASK, origin, 0.0, d, reach);
    while (rayQueryProceedEXT(q)) {
    }
    float height = OPEN;
    if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
        height = origin.y + d.y * rayQueryGetIntersectionTEXT(q, true);
    }
    imageStore(u_map, at, uvec4(floatBitsToUint(height), tag, 0u, 0u));
}
)";

struct TraceConstants {
    std::array<int32_t, 4> first{};
    std::array<float, 4> texels{};
    std::array<float, 4> direction{};
    std::array<float, 4> slant{};
};

struct PrecipState {
    Image map;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline trace = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE;
    bool failed = false;
    bool cleared = false;               // the map's tags zeroed since it was made
    uint64_t traced_frame = UINT64_MAX; // (the frame last traced: none yet)
    uint32_t generation = 1;
    std::array<double, 2> slant{};
    std::array<double, 3> direction{0.0, -1.0, 0.0};
    bool have_direction = false;
    std::array<float, 3> camera{};
    bool traced = false; // the map holds a trace (on)
    PrecipStats stats;
};

PrecipState& precip_state()
{
    static PrecipState own;
    return own;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

std::string trace_source()
{
    std::string source = kTraceSource;
    std::string defines = text::format(
        "#define SIZE %u\n#define SLICES %u\n#define ABOVE %.1f\n#define BELOW %.1f\n#define MASK 0x%02Xu\n"
        "#define OPEN %.1f\n",
        precip::kMapSize, precip::kRefreshSlices, static_cast<double>(precip::kTraceAbove),
        static_cast<double>(precip::kTraceBelow), precip::kTraceMask, static_cast<double>(precip::kOpenHeight));
    const std::size_t at = source.find('\n') + 1; // (after #version)
    source.insert(at, defines);
    return source;
}

bool ensure_sampler(Backend& state)
{
    PrecipState& own = precip_state();
    if (own.nearest != VK_NULL_HANDLE) {
        return true;
    }
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(state.context.device(), &sampler, nullptr, &own.nearest) != VK_SUCCESS) {
        own.nearest = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool ensure_objects(Backend& state, std::string* error)
{
    PrecipState& own = precip_state();
    if (own.trace != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .pImmutableSamplers = nullptr},
    }};
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        own.set_layout = VK_NULL_HANDLE;
        *error = "the descriptor layout";
        return false;
    }
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(TraceConstants)};
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
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, trace_source());
    if (!compiled.ok) {
        *error = compiled.log;
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
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &own.trace);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        own.trace = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

bool ensure_map(Backend& state)
{
    PrecipState& own = precip_state();
    if (own.map.image != VK_NULL_HANDLE) {
        return true;
    }
    const ImageRequest request{.format = kMapFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = precip::kMapSize * precip::kCascades, .height = precip::kMapSize},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &own.map)) {
        return false;
    }
    own.map.resting = VK_IMAGE_LAYOUT_GENERAL;
    prepare_image(state, &own.map);
    own.cleared = false;
    return true;
}

std::array<float, 2> slant_of(const std::array<double, 3>& direction)
{
    const double down = std::max(-core::at(direction, 1), 1.0e-3);
    return {static_cast<float>(core::at(direction, 0) / down), static_cast<float>(core::at(direction, 2) / down)};
}

} // namespace

void update_precip_map(Backend& state, const rt::ViewDrawn& view, const std::array<double, 3>& direction)
{
    PrecipState& own = precip_state();
    const uint64_t frame = state.frames.serial();
    if (own.traced_frame == frame || own.failed) {
        return; // (the frame's first view traced it)
    }
    own.traced_frame = frame;
    // (the map is made - every texel untraced: everything reached - whether or not it can be traced: its readers bind it)
    if (!ensure_sampler(state) || !ensure_map(state)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: precipitation: the occlusion map couldn't be made (its image) - everything is reached");
        }
        return;
    }
    if (!own.cleared) {
        VkCommandBuffer commands = outside_pass(state);
        Memory::transition(commands, &own.map, VK_IMAGE_LAYOUT_GENERAL);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                            .baseMipLevel = 0,
                                            .levelCount = 1,
                                            .baseArrayLayer = 0,
                                            .layerCount = 1};
        vkCmdClearColorImage(commands, own.map.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        const VkMemoryBarrier cleared{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                      .pNext = nullptr,
                                      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                             &cleared, 0, nullptr, 0, nullptr);
        own.cleared = true;
        own.map.used = frame;
    }
    if (!state.context.ray_tracing_enabled()) {
        own.traced = false;
        if (first_report(state, kReportNoTracing)) {
            get_log("vulkan: precipitation: no ray tracing - nothing shelters the rain (everything is reached)");
        }
        return;
    }
    const SceneView scene = current_scene(state);
    if (scene.tlas == VK_NULL_HANDLE || scene.instances == 0) {
        return; // (nothing to trace this frame: the map as it stands)
    }
    std::string error;
    if (!ensure_objects(state, &error)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: precipitation: the occlusion map couldn't be made (%s) - everything is reached",
                    error.empty() ? "its image" : error.c_str());
        }
        return;
    }
    // a new generation when the fall's slant moved (every line traced again)
    const std::array<float, 2> slant = slant_of(direction);
    if (!own.have_direction ||
        std::abs(core::at(slant, 0) - static_cast<float>(core::at(own.slant, 0))) > precip::kSlantChange ||
        std::abs(core::at(slant, 1) - static_cast<float>(core::at(own.slant, 1))) > precip::kSlantChange) {
        own.slant = {core::at(slant, 0), core::at(slant, 1)};
        own.direction = direction;
        own.have_direction = true;
        own.generation = (own.generation % 0x7FU) + 1U;
    }
    own.camera = view.camera.position;
    const std::array<float, 2> kept_slant{static_cast<float>(core::at(own.slant, 0)),
                                          static_cast<float>(core::at(own.slant, 1))};
    const precip::Window near_window = precip::window_of(own.camera, kept_slant, 0);
    const precip::Window far_window = precip::window_of(own.camera, kept_slant, 1);
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, &own.map, VK_IMAGE_LAYOUT_GENERAL);
    const VkMemoryBarrier before{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                 .pNext = nullptr,
                                 .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                 .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &before, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.trace);
    const VkDescriptorImageInfo map_info{
        .sampler = VK_NULL_HANDLE, .imageView = own.map.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSetAccelerationStructureKHR scene_info{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .pNext = nullptr,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &scene.tlas};
    std::array<VkWriteDescriptorSet, 2> writes{};
    core::at(writes, 0) = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                           .pNext = nullptr,
                           .dstSet = VK_NULL_HANDLE,
                           .dstBinding = 0,
                           .dstArrayElement = 0,
                           .descriptorCount = 1,
                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           .pImageInfo = &map_info,
                           .pBufferInfo = nullptr,
                           .pTexelBufferView = nullptr};
    core::at(writes, 1) = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                           .pNext = &scene_info,
                           .dstSet = VK_NULL_HANDLE,
                           .dstBinding = 1,
                           .dstArrayElement = 0,
                           .descriptorCount = 1,
                           .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                           .pImageInfo = nullptr,
                           .pBufferInfo = nullptr,
                           .pTexelBufferView = nullptr};
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    const TraceConstants constants{
        .first = {core::at(near_window.first, 0), core::at(near_window.first, 1), core::at(far_window.first, 0),
                  core::at(far_window.first, 1)},
        .texels = {near_window.texel, far_window.texel, core::at(own.camera, 1), static_cast<float>(own.generation)},
        .direction = {static_cast<float>(core::at(own.direction, 0)), static_cast<float>(core::at(own.direction, 1)),
                      static_cast<float>(core::at(own.direction, 2)),
                      static_cast<float>(frame % precip::kRefreshSlices)},
        .slant = {core::at(kept_slant, 0), core::at(kept_slant, 1), precip::kReferenceHeight, 0.0F}};
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    constexpr uint32_t kGroups = precip::kMapSize / kGroup;
    vkCmdDispatch(commands, kGroups, kGroups, precip::kCascades);
    const VkMemoryBarrier after{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .pNext = nullptr,
                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &after, 0, nullptr, 0, nullptr);
    state.bound = BoundState{};
    own.map.used = frame;
    own.traced = true;
    ++own.stats.updates;
    own.stats.generation = own.generation;
    own.stats.traced = true;
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: precipitation: the occlusion map traced (%u x %u texels x %u cascades of %.0f / %.0f units, "
                "the fall "
                "%.2f %.2f %.2f)",
                precip::kMapSize, precip::kMapSize, precip::kCascades, static_cast<double>(near_window.texel),
                static_cast<double>(far_window.texel), core::at(own.direction, 0), core::at(own.direction, 1),
                core::at(own.direction, 2));
    }
}

PrecipMap precip_map(Backend& state)
{
    PrecipState& own = precip_state();
    if (own.map.image == VK_NULL_HANDLE || own.nearest == VK_NULL_HANDLE) {
        return {};
    }
    if (!own.traced) { // (made, not traced: bound all the same - its readers see `on` false and never read it)
        PrecipMap untraced;
        untraced.image = {.sampler = own.nearest, .imageView = own.map.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        own.map.used = state.frames.serial();
        return untraced;
    }
    own.map.used = state.frames.serial();
    const std::array<float, 2> slant{static_cast<float>(core::at(own.slant, 0)),
                                     static_cast<float>(core::at(own.slant, 1))};
    const precip::Window near_window = precip::window_of(own.camera, slant, 0);
    const precip::Window far_window = precip::window_of(own.camera, slant, 1);
    PrecipMap map;
    map.on = true;
    map.image = {.sampler = own.nearest, .imageView = own.map.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    map.fall = {static_cast<float>(core::at(own.direction, 0)), static_cast<float>(core::at(own.direction, 1)),
                static_cast<float>(core::at(own.direction, 2)), 1.0F};
    map.slant = {core::at(slant, 0), core::at(slant, 1), precip::kReferenceHeight, 0.0F};
    map.near_window = {static_cast<float>(core::at(near_window.first, 0)),
                       static_cast<float>(core::at(near_window.first, 1)), near_window.texel,
                       precip::reach_slack(near_window.texel, slant)};
    map.far_window = {static_cast<float>(core::at(far_window.first, 0)),
                      static_cast<float>(core::at(far_window.first, 1)), far_window.texel,
                      precip::reach_slack(far_window.texel, slant)};
    return map;
}

PrecipStats precip_stats() { return precip_state().stats; }

bool read_precip_map(Backend& state, std::vector<uint32_t>* texels)
{
    PrecipState& own = precip_state();
    texels->clear();
    if (own.map.image == VK_NULL_HANDLE || !finish_work(state)) {
        return false;
    }
    std::vector<uint8_t> raw(static_cast<std::size_t>(own.map.extent.width) * own.map.extent.height * kTexelBytes);
    if (!state.memory.read_image_level(&own.map, 0, &raw)) {
        return false;
    }
    texels->resize(raw.size() / sizeof(uint32_t));
    std::memcpy(texels->data(), raw.data(), raw.size());
    return true;
}

void release_precip(Backend& state)
{
    PrecipState& own = precip_state();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        if (own.trace != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.trace, nullptr);
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.nearest != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.nearest, nullptr);
        }
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.map);
    }
    own = PrecipState{};
}

} // namespace render::vulkan
