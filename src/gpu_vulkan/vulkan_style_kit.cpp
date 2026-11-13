// NOT ORIGINAL - what the vhs, retro and watercolour styles' passes share (vulkan_style_kit.h).
#include "gpu_vulkan/vulkan_style_kit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_style_looks.h"
#include "platform/run_log.h"

namespace render::vulkan::look_kit {

namespace {

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr uint32_t kTimerPairs = 8;
constexpr uint64_t kTimingLogEvery = 300;
constexpr std::size_t kConstantBytes = 128;
static_assert(sizeof(LookConstants) == kConstantBytes, "the shaders' push-constant block");

constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// (the twins below are these, operation for operation)
constexpr const char* kCommon = R"(#version 460
layout(push_constant) uniform Look {
    ivec4 rect; // the viewport in the target: x, y, width, height
    ivec4 info;
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 d;
    vec4 e;
    vec4 f;
} pc;
layout(set = 0, binding = 0) uniform sampler2D u_source;
layout(set = 0, binding = 1) uniform sampler2D u_a;
layout(set = 0, binding = 2) uniform sampler2D u_b;

float finite(float c) { return c >= 0.0 ? min(c, 1.0) : 0.0; } // (not a number fails the comparison: 0)
vec3 finite3(vec3 c) { return vec3(finite(c.r), finite(c.g), finite(c.b)); }

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hash01(uint x) { return float(hash(x) >> 8) * (1.0 / 16777216.0); }
uint hash2(int column, int row, uint seed) { return hash(uint(column) * 0x8da6b343u ^ hash(uint(row) * 0xd8163841u + seed)); }
float value_noise(vec2 at, uint seed)
{
    vec2 corner = floor(at);
    vec2 f = at - corner;
    vec2 u = f * f * (3.0 - 2.0 * f);
    ivec2 k = ivec2(corner);
    float n00 = hash01(hash2(k.x, k.y, seed));
    float n10 = hash01(hash2(k.x + 1, k.y, seed));
    float n01 = hash01(hash2(k.x, k.y + 1, seed));
    float n11 = hash01(hash2(k.x + 1, k.y + 1, seed));
    return mix(mix(n00, n10, u.x), mix(n01, n11, u.x), u.y);
}

// The viewport's colour at a place in it (pixels from its corner, pixel centres at + 0.5), filtered, clamped inside it.
vec3 source_at(vec2 local)
{
    vec2 at = clamp(local, vec2(0.5), vec2(pc.rect.zw) - 0.5) + vec2(pc.rect.xy);
    return finite3(textureLod(u_source, at / vec2(textureSize(u_source, 0)), 0.0).rgb);
}
)";

struct KitState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    Image source;
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0; // (negative: no timestamps here)
    std::array<uint64_t, kTimerPairs> timer_serials{};
    uint64_t timed = 0;
    uint64_t window_samples = 0;
    double window_milliseconds = 0.0;
    bool timing = false; // this pass's pair begun
    const char* name = "";  // the style the last pass was (the timing's log)
    uint32_t pair = 0;
};

KitState& kit()
{
    static KitState state;
    return state;
}

VkShaderModule make_module(VkDevice device, RtShaderStage stage, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(stage, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                        .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    return module;
}

// The timestamps' pool (once; none when the device can't time).
void ensure_timer(Backend& state)
{
    KitState& own = kit();
    if (own.timer != VK_NULL_HANDLE || own.tick_milliseconds < 0.0) {
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimerPairs * 2,
                                     .pipelineStatistics = 0};
    if (!(properties.limits.timestampPeriod > 0.0F) ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer) != VK_SUCCESS) {
        own.timer = VK_NULL_HANDLE;
        own.tick_milliseconds = -1.0;
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

// A pair's last result (when the device wrote both) added to the timing; logged every kTimingLogEvery.
void read_timer(Backend& state, uint32_t pair, const char* name, const std::array<uint32_t, 4>& shown)
{
    KitState& own = kit();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, 2> stamps{};
    const VkResult got =
        vkGetQueryPoolResults(state.context.device(), own.timer, pair * 2, 2, sizeof(stamps), stamps.data(), sizeof(Stamp),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((got != VK_SUCCESS && got != VK_NOT_READY) || stamps.front().available == 0 || stamps.back().available == 0 ||
        stamps.back().value < stamps.front().value) {
        return;
    }
    ++own.window_samples;
    own.window_milliseconds += static_cast<double>(stamps.back().value - stamps.front().value) * own.tick_milliseconds;
    if (own.window_samples >= kTimingLogEvery) {
        get_log("vulkan: rt_style %s: the passes take %.3f ms on the device (%ux%u; the last %llu passes' average)", name,
                own.window_milliseconds / static_cast<double>(own.window_samples), core::at(shown, 2), core::at(shown, 3),
                static_cast<unsigned long long>(own.window_samples));
        own.window_samples = 0;
        own.window_milliseconds = 0.0;
    }
}

void push_descriptors(VkCommandBuffer commands, VkPipelineBindPoint point, const Bound& bound)
{
    const KitState& own = kit();
    std::array<VkDescriptorImageInfo, kBindingCount> infos{};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    uint32_t count = 0;
    const auto add = [&](uint32_t binding, const Image* image) {
        if (image == nullptr) {
            return;
        }
        const bool storage = binding == kBindingOut;
        const VkImageLayout layout =
            image == &own.source ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo& info = core::at(infos, binding);
        info = {.sampler = storage ? VK_NULL_HANDLE : own.sampler, .imageView = image->view, .imageLayout = layout};
        VkWriteDescriptorSet& write = core::at(writes, count++);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &info;
    };
    add(kBindingSource, bound.source);
    add(kBindingA, bound.a);
    add(kBindingB, bound.b);
    add(kBindingOut, bound.out);
    vkCmdPushDescriptorSetKHR(commands, point, own.layout, 0, count, writes.data());
}

} // namespace

const char* glsl_common() { return kCommon; }

bool ensure_common(Backend& state, std::string* error)
{
    KitState& own = kit();
    if (own.vertex != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
        for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
            core::at(bindings, binding) = {.binding = binding,
                                           .descriptorType = binding == kBindingOut
                                                                 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                                 : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           .descriptorCount = 1,
                                           .stageFlags = kStages,
                                           .pImmutableSamplers = nullptr};
        }
        const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                       .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                       .pBindings = bindings.data()};
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0F;
        if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
            vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS) {
            *error = "the descriptor layout or the sampler";
            return false;
        }
        const VkPushConstantRange constants{.stageFlags = kStages, .offset = 0, .size = sizeof(LookConstants)};
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
    }
    own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
    return own.vertex != VK_NULL_HANDLE;
}

VkPipeline make_compute(Backend& state, const std::string& source, std::string* error)
{
    const VkDevice device = state.context.device();
    const VkShaderModule module = make_module(device, RtShaderStage::kCompute, source, error);
    if (module == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
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
                                           .layout = kit().layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateComputePipelines failed";
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline fragment_pipeline(Backend& state, std::vector<FragmentPipeline>* made, const std::string& source, VkFormat format,
                             VkSampleCountFlagBits samples, std::string* error)
{
    for (const FragmentPipeline& pipeline : *made) {
        if (pipeline.format == format && pipeline.samples == samples) {
            return pipeline.pipeline;
        }
    }
    const KitState& own = kit();
    const VkDevice device = state.context.device();
    const VkShaderModule fragment = make_module(device, RtShaderStage::kFragment, source, error);
    if (fragment == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = own.vertex,
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment,
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = own.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (result != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    made->push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

void destroy_pipelines(Backend& state, std::vector<FragmentPipeline>* made)
{
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const FragmentPipeline& pipeline : *made) {
            vkDestroyPipeline(device, pipeline.pipeline, nullptr);
        }
    }
    made->clear();
}

void destroy_pipeline(Backend& state, VkPipeline* pipeline)
{
    const VkDevice device = state.context.device();
    if (device != nullptr && *pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, *pipeline, nullptr);
    }
    *pipeline = VK_NULL_HANDLE;
}

namespace {

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
    if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
        image->resting = VK_IMAGE_LAYOUT_GENERAL;
    }
    prepare_image(state, image);
    return true;
}

} // namespace

bool ensure_work_image(Backend& state, Image* image, Extent extent)
{
    return ensure_image(state, image, kWorkFormat, extent,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
}

void destroy_image(Backend& state, Image* image)
{
    if (state.memory.ready()) {
        state.memory.destroy_image(image);
    }
}

VkCommandBuffer begin(Backend& state, const LookTarget& target, const LookConstants& constants,
                      std::initializer_list<Image*> work)
{
    KitState& own = kit();
    Image* const colour = target.colour;
    if (!ensure_image(state, &own.source, colour->format, colour->extent,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return VK_NULL_HANDLE;
    }
    VkCommandBuffer commands = outside_pass(state);
    // the timing's first stamp, when a pair is free (its last result read first)
    ensure_timer(state);
    own.pair = static_cast<uint32_t>(own.timed % kTimerPairs);
    uint64_t& written = core::at(own.timer_serials, own.pair);
    own.timing = own.timer != VK_NULL_HANDLE && (written == 0 || state.frames.completed() >= written);
    if (own.timing) {
        if (written != 0) {
            read_timer(state, own.pair, own.name, target.shown);
        }
        written = state.frames.serial();
        vkCmdResetQueryPool(commands, own.timer, own.pair * 2, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timer, own.pair * 2);
    }
    // the viewport's colour copied out (resolved when multisampled)
    const std::array<uint32_t, 4>& shown = target.shown;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(shown, 0)), .y = static_cast<int32_t>(core::at(shown, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(shown, 2), .height = core::at(shown, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{
            .srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{
            .srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.source.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    Memory::transition(commands, &own.source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // the work images: the last pass's reads of them done before they are written again
    for (Image* image : work) {
        if (image->image != VK_NULL_HANDLE) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL);
        }
    }
    const VkMemoryBarrier reads_done{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .pNext = nullptr,
                                     .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &reads_done, 0, nullptr, 0, nullptr);
    vkCmdPushConstants(commands, own.layout, kStages, 0, sizeof(LookConstants), &constants);
    return commands;
}

const Image* source() { return &kit().source; }

void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, uint32_t width, uint32_t height)
{
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_COMPUTE, bound);
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

void draw(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, const LookTarget& target)
{
    Image* const colour = target.colour;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(target.shown, 0)),
                                   .y = static_cast<int32_t>(core::at(target.shown, 1))},
                        .extent = {.width = core::at(target.shown, 2), .height = core::at(target.shown, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    push_descriptors(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, bound);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

void end(Backend& state, VkCommandBuffer commands, const LookTarget& target, const char* name,
         std::initializer_list<Image*> work)
{
    KitState& own = kit();
    if (own.timing) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timer, (own.pair * 2) + 1);
        ++own.timed;
        own.timing = false;
        own.name = name; // (the pair's result is read - and logged - when it comes round again)
    }
    if (target.colour->layout != target.colour->resting) {
        Memory::transition(commands, target.colour, target.colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    target.colour->used = serial;
    own.source.used = serial;
    for (Image* image : work) {
        if (image->image != VK_NULL_HANDLE) {
            image->used = serial;
        }
    }
}

void release_common(Backend& state)
{
    KitState& own = kit();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        if (own.vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, own.vertex, nullptr);
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
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
    }
    destroy_image(state, &own.source);
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.timer = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.timer_serials = {};
    own.timing = false;
}

// --- the processor twins' helpers ------------------------------------------------------------------------------------

float finite(float value) { return value >= 0.0F ? std::min(value, 1.0F) : 0.0F; }

LookRgb finite3(const LookRgb& colour)
{
    return {finite(core::at(colour, 0)), finite(core::at(colour, 1)), finite(core::at(colour, 2))};
}

float as_half(float value)
{
    constexpr float kLargestHalf = 65504.0F;
    constexpr int kMantissaBits = 11;   // (10 stored and the leading one)
    constexpr int kLeastExponent = -13; // (frexp's for the smallest normal half: 2^-14 = 0.5 x 2^-13)
    if (!std::isfinite(value) || value == 0.0F) {
        return value;
    }
    if (std::abs(value) > kLargestHalf) {
        return std::copysign(std::numeric_limits<float>::infinity(), value);
    }
    int exponent = 0;
    (void)std::frexp(value, &exponent);
    exponent = std::max(exponent, kLeastExponent);
    const float quantum = std::ldexp(1.0F, exponent - kMantissaBits);
    return std::nearbyint(value / quantum) * quantum;
}

LookRgb as_half3(const LookRgb& colour)
{
    return {as_half(core::at(colour, 0)), as_half(core::at(colour, 1)), as_half(core::at(colour, 2))};
}

float smoothstep(float low, float high, float value)
{
    constexpr float kThree = 3.0F;
    constexpr float kTwo = 2.0F;
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return along * along * (kThree - (kTwo * along));
}

float mix(float from, float to, float part) { return from + ((to - from) * part); }

LookRgb mix3(const LookRgb& from, const LookRgb& to, float part)
{
    LookRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = mix(core::at(from, channel), core::at(to, channel), part);
    }
    return out;
}

uint32_t hash(uint32_t value)
{
    constexpr uint32_t kFirst = 0x7feb352dU;
    constexpr uint32_t kSecond = 0x846ca68bU;
    constexpr uint32_t kShiftA = 16;
    constexpr uint32_t kShiftB = 15;
    value ^= value >> kShiftA;
    value *= kFirst;
    value ^= value >> kShiftB;
    value *= kSecond;
    value ^= value >> kShiftA;
    return value;
}

float hash01(uint32_t value)
{
    constexpr uint32_t kDropped = 8;
    constexpr float kSteps = 1.0F / 16777216.0F;
    return static_cast<float>(hash(value) >> kDropped) * kSteps;
}

uint32_t hash2(int32_t column, int32_t row, uint32_t seed)
{
    constexpr uint32_t kColumns = 0x8da6b343U;
    constexpr uint32_t kRows = 0xd8163841U;
    return hash((static_cast<uint32_t>(column) * kColumns) ^ hash((static_cast<uint32_t>(row) * kRows) + seed));
}

float value_noise(LookPoint at, uint32_t seed)
{
    constexpr float kThree = 3.0F;
    constexpr float kTwo = 2.0F;
    const float corner_x = std::floor(at.x);
    const float corner_y = std::floor(at.y);
    const float fx = at.x - corner_x;
    const float fy = at.y - corner_y;
    const float ux = fx * fx * (kThree - (kTwo * fx));
    const float uy = fy * fy * (kThree - (kTwo * fy));
    const auto kx = static_cast<int32_t>(corner_x);
    const auto ky = static_cast<int32_t>(corner_y);
    const float n00 = hash01(hash2(kx, ky, seed));
    const float n10 = hash01(hash2(kx + 1, ky, seed));
    const float n01 = hash01(hash2(kx, ky + 1, seed));
    const float n11 = hash01(hash2(kx + 1, ky + 1, seed));
    return mix(mix(n00, n10, ux), mix(n01, n11, ux), uy);
}

const LookRgb& texel(const std::vector<LookRgb>& image, Extent size, int32_t column, int32_t row)
{
    const auto across = static_cast<std::size_t>(std::clamp(column, 0, static_cast<int32_t>(size.width) - 1));
    const auto down = static_cast<std::size_t>(std::clamp(row, 0, static_cast<int32_t>(size.height) - 1));
    return core::at(image, (down * size.width) + across);
}

LookRgb bilinear(const std::vector<LookRgb>& image, Extent size, LookPoint at)
{
    constexpr float kHalf = 0.5F;
    const float place_x = at.x - kHalf;
    const float place_y = at.y - kHalf;
    const float floor_x = std::floor(place_x);
    const float floor_y = std::floor(place_y);
    const float part_x = place_x - floor_x;
    const float part_y = place_y - floor_y;
    const auto column = static_cast<int32_t>(floor_x);
    const auto row = static_cast<int32_t>(floor_y);
    const LookRgb& top_left = texel(image, size, column, row);
    const LookRgb& top_right = texel(image, size, column + 1, row);
    const LookRgb& bottom_left = texel(image, size, column, row + 1);
    const LookRgb& bottom_right = texel(image, size, column + 1, row + 1);
    LookRgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = mix(mix(core::at(top_left, channel), core::at(top_right, channel), part_x),
                                     mix(core::at(bottom_left, channel), core::at(bottom_right, channel), part_x), part_y);
    }
    return out;
}

Extent size_of(const LookConstants& constants)
{
    return {.width = static_cast<uint32_t>(std::max(0, core::at(constants.rect, 2))),
            .height = static_cast<uint32_t>(std::max(0, core::at(constants.rect, 3)))};
}

} // namespace render::vulkan::look_kit
