// NOT ORIGINAL - HDR rendering, tone mapping and colour grading (vulkan_hdr.h; docs/design/vulkan.md "HDR").
#include "gpu_vulkan/vulkan_hdr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/file/disk.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportNotFloat = 0x48445201U;
constexpr uint64_t kReportPipeline = 0x48445202U;
constexpr uint64_t kReportFirstPass = 0x48445203U;
constexpr uint64_t kReportCopy = 0x48445204U;

constexpr float kLargest = 65504.0F;         // the largest 16-bit float
constexpr float kBeyondWhite = 2.0F * kLargest; // (an infinity read back)
constexpr float kGamma = 2.2F;               // the colours taken as gamma 2.2 for the exposure and ACES
constexpr float kWhiteMargin = 0.5F / 255.0F; // the white point shown within half a step of white
constexpr float kLeastWhite = 1.0F;
constexpr float kMostWhite = 8.0F;
constexpr float kMostStops = 16.0F;
constexpr float kMostSaturation = 4.0F;
constexpr float kLeastContrast = 0.1F;
constexpr float kMostContrast = 4.0F;
constexpr float kMidGrey = 0.5F;
constexpr int kBisections = 60;
constexpr uint32_t kLeastLut = 2;
constexpr uint32_t kMostLut = 128;
constexpr std::array<float, 3> kLuma{0.2126F, 0.7152F, 0.0722F}; // Rec. 709
// Narkowicz's ACES fit: x (a x + b) / (x (c x + d) + e)
constexpr float kAcesA = 2.51F;
constexpr float kAcesB = 0.03F;
constexpr float kAcesC = 2.43F;
constexpr float kAcesD = 0.59F;
constexpr float kAcesE = 0.14F;
constexpr std::size_t kHdrPixelBytes = 8;
constexpr std::size_t kBgraPixelBytes = 4;
constexpr float kByteMax = 255.0F;

// The full-screen triangle (three vertices, no buffers).
constexpr const char* kVertexSource = R"(#version 450
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The fragment shader (vulkan_hdr.h's curve; tone_map_reference is its twin): reads the viewport's copy, writes the
// colour target's rgb. MULTISAMPLED is defined before it for a multisampled target.
constexpr const char* kFragmentBody = R"(
#ifdef MULTISAMPLED
layout(set = 0, binding = 0) uniform sampler2DMS u_colour;
#else
layout(set = 0, binding = 0) uniform sampler2D u_colour;
#endif
layout(std430, set = 0, binding = 1) readonly buffer Lut { vec4 entries[]; } u_lut;
layout(push_constant) uniform Pass {
    vec4 grade;     // x: the exposure's factor; y: saturation; z: contrast; w: the curve (0, 1)
    vec4 shape;     // x: the knee; y: 1 - the knee; z: ACES's scale; w: samples
    vec4 lut;       // x: the table's size (0: none)
    vec4 lut_min;   // the domain's low corner
    vec4 lut_scale; // 1 / (the domain's high corner - low corner)
} pc;
layout(location = 0) out vec4 o_colour;

const float kLargest = 65504.0;

float finite(float c)
{
    return c >= 0.0 ? min(c, kLargest) : 0.0; // (not a number fails the comparison: 0)
}

float shoulder(float c)
{
    if (c <= pc.shape.x) {
        return c;
    }
    if (pc.shape.y <= 0.0) {
        return 1.0;
    }
    return 1.0 - pc.shape.y * exp(-(c - pc.shape.x) / pc.shape.y);
}

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 lut_entry(ivec3 i)
{
    int n = int(pc.lut.x);
    return u_lut.entries[i.x + n * (i.y + n * i.z)].rgb;
}

vec3 graded(vec3 c)
{
    int n = int(pc.lut.x);
    vec3 u = clamp((c - pc.lut_min.xyz) * pc.lut_scale.xyz, 0.0, 1.0) * float(n - 1);
    ivec3 i = min(ivec3(u), ivec3(n - 2));
    vec3 f = u - vec3(i);
    vec3 c00 = mix(lut_entry(i), lut_entry(i + ivec3(1, 0, 0)), f.x);
    vec3 c10 = mix(lut_entry(i + ivec3(0, 1, 0)), lut_entry(i + ivec3(1, 1, 0)), f.x);
    vec3 c01 = mix(lut_entry(i + ivec3(0, 0, 1)), lut_entry(i + ivec3(1, 0, 1)), f.x);
    vec3 c11 = mix(lut_entry(i + ivec3(0, 1, 1)), lut_entry(i + ivec3(1, 1, 1)), f.x);
    return mix(mix(c00, c10, f.y), mix(c01, c11, f.y), f.z);
}

vec3 tone(vec3 c)
{
    c = vec3(finite(c.r), finite(c.g), finite(c.b));
    c *= pc.grade.x;
    if (pc.grade.y != 1.0) {
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = max(vec3(luma) + (c - vec3(luma)) * pc.grade.y, vec3(0.0));
    }
    if (pc.grade.z != 1.0) {
        c = 0.5 * pow(c / 0.5, vec3(pc.grade.z));
    }
    if (pc.grade.w < 0.5) {
        c = vec3(shoulder(c.r), shoulder(c.g), shoulder(c.b));
    } else {
        c = pow(min(aces(pow(c, vec3(2.2))) * pc.shape.z, vec3(1.0)), vec3(1.0 / 2.2));
    }
    if (pc.lut.x >= 2.0) {
        c = graded(c);
    }
    return clamp(c, 0.0, 1.0);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
#ifdef MULTISAMPLED
    int samples = int(pc.shape.w);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        sum += tone(texelFetch(u_colour, p, i).rgb);
    }
    o_colour = vec4(sum / float(samples), 1.0);
#else
    o_colour = vec4(tone(texelFetch(u_colour, p, 0).rgb), 1.0);
#endif
}
)";

// The push constants, as the fragment shader's block lays them out.
struct PassConstants {
    std::array<float, 4> grade{};
    std::array<float, 4> shape{};
    std::array<float, 4> lut{};
    std::array<float, 4> lut_min{};
    std::array<float, 4> lut_scale{};
};
constexpr std::size_t kPassConstantBytes = 80;
static_assert(sizeof(PassConstants) == kPassConstantBytes, "the shader's push-constant block");

struct PassPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct HdrState {
    HdrSettings settings;
    ToneParameters parameters;
    GradeLut lut;
    bool lut_changed = false; // (the table's buffer made again at the next pass)
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::vector<PassPipeline> pipelines;
    Image copy;     // the viewport's colour copied out for the pass to read
    Image resolved; // resolve_for_copy's
    DeviceBuffer lut_buffer;
    bool failed = false;
    uint64_t passes = 0;
};

HdrState& hdr()
{
    static HdrState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The listener rt_scene tells when a view's world is drawn (last of them: kToneMapping).
class ToneListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kToneMapping; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_tone_map_pass(state, view);
        }
    }
};

ToneListener& listener()
{
    static ToneListener instance;
    return instance;
}

float aces_curve(float x) { return std::clamp((x * ((kAcesA * x) + kAcesB)) / ((x * ((kAcesC * x) + kAcesD)) + kAcesE), 0.0F, 1.0F); }

// What is left below white at the white point for a shoulder with room `room` (1 - the knee).
double left_at_white(double room, double white)
{
    return room * std::exp(-1.0 - ((white - 1.0) / room));
}

float finite(float value)
{
    return value >= 0.0F ? std::min(value, kLargest) : 0.0F; // (not a number fails the comparison: 0)
}

float shoulder(const ToneParameters& parameters, float value)
{
    if (value <= parameters.knee) {
        return value;
    }
    if (parameters.room <= 0.0F) {
        return 1.0F;
    }
    return 1.0F - (parameters.room * std::exp(-(value - parameters.knee) / parameters.room));
}

using Rgb = std::array<float, 3>;
using Index3 = std::array<uint32_t, 3>;

// The table's entry at (red, green, blue).
Rgb lut_entry(const GradeLut& lut, const Index3& at)
{
    const std::size_t side = lut.size;
    return core::at(lut.entries, core::at(at, 0) + (side * (core::at(at, 1) + (side * core::at(at, 2)))));
}

// From one colour toward another by `part`.
Rgb mix(const Rgb& from, const Rgb& to, float part)
{
    Rgb mixed{};
    for (std::size_t channel = 0; channel < mixed.size(); ++channel) {
        core::at(mixed, channel) =
            core::at(from, channel) + ((core::at(to, channel) - core::at(from, channel)) * part);
    }
    return mixed;
}

Rgb graded(const GradeLut& lut, const Rgb& colour)
{
    Index3 low{};
    Rgb fraction{};
    for (std::size_t channel = 0; channel < colour.size(); ++channel) {
        const float span = core::at(lut.domain_max, channel) - core::at(lut.domain_min, channel);
        const float scale = span != 0.0F ? 1.0F / span : 0.0F;
        const float place =
            std::clamp((core::at(colour, channel) - core::at(lut.domain_min, channel)) * scale, 0.0F, 1.0F) *
            static_cast<float>(lut.size - 1);
        const uint32_t below = std::min(static_cast<uint32_t>(place), lut.size - 2);
        core::at(low, channel) = below;
        core::at(fraction, channel) = place - static_cast<float>(below);
    }
    const auto entry = [&](const Index3& step) {
        return lut_entry(lut, {core::at(low, 0) + core::at(step, 0), core::at(low, 1) + core::at(step, 1),
                               core::at(low, 2) + core::at(step, 2)});
    };
    const float along_red = core::at(fraction, 0);
    const Rgb near_near = mix(entry({0, 0, 0}), entry({1, 0, 0}), along_red);
    const Rgb far_near = mix(entry({0, 1, 0}), entry({1, 1, 0}), along_red);
    const Rgb near_far = mix(entry({0, 0, 1}), entry({1, 0, 1}), along_red);
    const Rgb far_far = mix(entry({0, 1, 1}), entry({1, 1, 1}), along_red);
    return mix(mix(near_near, far_near, core::at(fraction, 1)), mix(near_far, far_far, core::at(fraction, 1)),
               core::at(fraction, 2));
}

// A whole file's text (empty when it can't be read).
std::string read_text_file(const std::string& path)
{
    std::string text;
    fs::DiskStream* const in = fs::disk().open_stream(path.c_str(), "rb", fs::StreamSharing::kAll);
    if (in == nullptr) {
        return text;
    }
    if (fs::disk().stream_seek(in, 0, SEEK_END) == 0) {
        const long size = fs::disk().stream_tell(in);
        if (size > 0 && fs::disk().stream_seek(in, 0, SEEK_SET) == 0) {
            text.resize(static_cast<std::size_t>(size));
            text.resize(fs::disk().stream_read(in, text.data(), 1, text.size()));
        }
    }
    fs::disk().stream_close(in);
    return text;
}

// --- the pass's objects -----------------------------------------------------------------------------------------

VkShaderModule make_module(VkDevice device, ShaderStage stage, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_glsl(stage, source);
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

// The layouts, the sampler and the vertex shader (once).
bool ensure_layouts(Backend& state, std::string* error)
{
    HdrState& own = hdr();
    if (own.layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
         .pImmutableSamplers = nullptr},
    }};
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(PassConstants)};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
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
    own.vertex = make_module(device, ShaderStage::kVertex, kVertexSource, error);
    return own.vertex != VK_NULL_HANDLE;
}

// The pipeline for a colour target's format and samples (made on first use): no blending, colour written, alpha kept.
VkPipeline pass_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, std::string* error)
{
    HdrState& own = hdr();
    for (const PassPipeline& made : own.pipelines) {
        if (made.format == format && made.samples == samples) {
            return made.pipeline;
        }
    }
    if (!ensure_layouts(state, error)) {
        return VK_NULL_HANDLE;
    }
    const VkDevice device = state.context.device();
    std::string source = "#version 450\n";
    if (samples != VK_SAMPLE_COUNT_1_BIT) {
        source += "#define MULTISAMPLED\n";
    }
    source += kFragmentBody;
    const VkShaderModule fragment = make_module(device, ShaderStage::kFragment, source, error);
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
    const VkResult made = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    own.pipelines.push_back({.format = format, .samples = samples, .pipeline = pipeline});
    return pipeline;
}

// An image like `like` (format, size, samples) for `usage`, made again when any of them changed.
bool ensure_image_like(Backend& state, Image* image, const Image& like, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->format == like.format && image->samples == like.samples &&
        image->extent.width == like.extent.width && image->extent.height == like.extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = like.format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = like.extent,
                               .levels = 1,
                               .samples = like.samples,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    prepare_image(state, image);
    return true;
}

// The grade table's buffer (one entry when there is none: the descriptor needs a buffer), made again when it changed.
bool ensure_lut_buffer(Backend& state)
{
    HdrState& own = hdr();
    if (own.lut_buffer.buffer != VK_NULL_HANDLE && !own.lut_changed) {
        return true;
    }
    retire_buffer(state, &own.lut_buffer);
    own.lut_changed = false;
    const std::size_t entries = std::max<std::size_t>(own.lut.entries.size(), 1);
    constexpr std::size_t kEntryBytes = 4 * sizeof(float); // (std430 vec4)
    if (!state.memory.create_buffer(entries * kEntryBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload,
                                    &own.lut_buffer)) {
        return false;
    }
    std::vector<float> values(entries * 4, 0.0F);
    for (std::size_t entry = 0; entry < own.lut.entries.size(); ++entry) {
        const Rgb& colour = core::at(own.lut.entries, entry);
        for (std::size_t channel = 0; channel < colour.size(); ++channel) {
            core::at(values, (entry * 4) + channel) = core::at(colour, channel);
        }
    }
    const std::span<uint8_t> into(static_cast<uint8_t*>(own.lut_buffer.mapped), entries * kEntryBytes);
    const auto from = std::as_bytes(std::span(values));
    std::ranges::transform(from, into.begin(), [](std::byte b) { return static_cast<uint8_t>(b); });
    state.memory.flush(own.lut_buffer, 0, entries * kEntryBytes);
    return true;
}

// The image a surface draws into (a texture's level 0, a render target's).
Image* colour_image_of(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return surface->level == 0 ? &surface->texture->image : nullptr;
    case SurfaceKind::kRenderTarget:
        return &surface->image;
    case SurfaceKind::kDepth:
    case SurfaceKind::kSystemMemory:
        break;
    }
    return nullptr;
}

// One 16-bit float's value.
float half_value(uint16_t bits)
{
    constexpr uint32_t kSignShift = 15;
    constexpr uint32_t kExponentShift = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr uint32_t kMantissaUnit = 0x400;
    constexpr int kBias = 15;
    const uint32_t exponent = (bits >> kExponentShift) & kExponentMask;
    const uint32_t mantissa = bits & kMantissaMask;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), 1 - kBias - static_cast<int>(kExponentShift));
    } else if (exponent == kExponentMask) {
        value = mantissa == 0 ? kBeyondWhite : 0.0F; // (infinity read as beyond white; not a number as 0)
    } else {
        value = std::ldexp(static_cast<float>(mantissa + kMantissaUnit),
                           static_cast<int>(exponent) - kBias - static_cast<int>(kExponentShift));
    }
    return (bits >> kSignShift) != 0 ? -value : value;
}

uint8_t to_byte(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByteMax));
}

// A .cube line's words (split at spaces and tabs).
std::vector<std::string_view> cube_words(std::string_view line)
{
    std::vector<std::string_view> found;
    std::size_t at = 0;
    const auto blank = [](char letter) { return letter == ' ' || letter == '\t'; };
    while (at < line.size()) {
        while (at < line.size() && blank(line.at(at))) {
            ++at;
        }
        const std::size_t start = at;
        while (at < line.size() && !blank(line.at(at))) {
            ++at;
        }
        if (at > start) {
            found.push_back(line.substr(start, at - start));
        }
    }
    return found;
}

// A number word (all of it a number, finite).
bool cube_number(std::string_view word, float* value)
{
    const std::string copy(word);
    char* end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    *value = static_cast<float>(parsed);
    return true;
}

// Three number words from `first` into `rgb`.
bool cube_triple(const std::vector<std::string_view>& parts, std::size_t first, Rgb* rgb)
{
    if (parts.size() != first + rgb->size()) {
        return false;
    }
    for (std::size_t channel = 0; channel < rgb->size(); ++channel) {
        if (!cube_number(core::at(parts, first + channel), &core::at(*rgb, channel))) {
            return false;
        }
    }
    return true;
}

// One line of a .cube file (not empty) into the table; false (and why, unless it isn't three numbers) when it's wrong.
bool read_cube_line(const std::vector<std::string_view>& parts, GradeLut* made, std::string* error)
{
    const std::string_view key = parts.front();
    if (key.starts_with('#') || key == "TITLE") {
        return true;
    }
    if (key == "LUT_1D_SIZE") {
        *error = "a 1D table (only LUT_3D_SIZE is read)";
        return false;
    }
    if (key == "LUT_3D_SIZE") {
        float size = 0.0F;
        if (parts.size() != 2 || !cube_number(parts.back(), &size) || size < static_cast<float>(kLeastLut) ||
            size > static_cast<float>(kMostLut) || size != std::floor(size)) {
            *error = "LUT_3D_SIZE isn't 2 - 128";
            return false;
        }
        made->size = static_cast<uint32_t>(size);
        return true;
    }
    if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
        if (!cube_triple(parts, 1, key == "DOMAIN_MIN" ? &made->domain_min : &made->domain_max)) {
            *error = "a DOMAIN line without three numbers";
            return false;
        }
        return true;
    }
    Rgb entry{};
    if (!cube_triple(parts, 0, &entry)) {
        return false;
    }
    made->entries.push_back(entry);
    return true;
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

ToneParameters tone_parameters(const HdrSettings& settings)
{
    ToneParameters parameters;
    const float stops = std::isfinite(settings.exposure) ? std::clamp(settings.exposure, -kMostStops, kMostStops) : 0.0F;
    parameters.scale = static_cast<float>(std::exp2(static_cast<double>(stops) / static_cast<double>(kGamma)));
    parameters.saturation =
        std::isfinite(settings.saturation) ? std::clamp(settings.saturation, 0.0F, kMostSaturation) : 1.0F;
    parameters.contrast =
        std::isfinite(settings.contrast) ? std::clamp(settings.contrast, kLeastContrast, kMostContrast) : 1.0F;
    parameters.curve = settings.tonemap == 1 ? 1U : 0U;
    const double white = std::isfinite(settings.white_point)
                             ? std::clamp(static_cast<double>(settings.white_point), 1.0, static_cast<double>(kMostWhite))
                             : static_cast<double>(kHdrWhitePoint);
    // the shoulder's room: the largest that still shows the white point within the margin (what is left below white
    // there grows with the room), found by halving; 0 at a white point of 1
    double room = 0.0;
    if (white > static_cast<double>(kLeastWhite)) {
        double low = 0.0;
        double high = 1.0;
        if (left_at_white(high, white) <= static_cast<double>(kWhiteMargin)) {
            low = high;
        } else {
            for (int i = 0; i < kBisections; ++i) {
                const double middle = (low + high) * 0.5;
                (left_at_white(middle, white) <= static_cast<double>(kWhiteMargin) ? low : high) = middle;
            }
        }
        room = low;
    }
    parameters.room = static_cast<float>(room);
    parameters.knee = static_cast<float>(1.0 - room);
    parameters.aces_scale = 1.0F / aces_curve(static_cast<float>(std::pow(white, static_cast<double>(kGamma))));
    return parameters;
}

void set_hdr_settings(const HdrSettings& settings)
{
    GradeLut lut;
    // (a split comparison's swap between its sides with the same grading table: the table kept, not read again -
    // rt_scene.h settings_swap)
    if (rt::settings_swap()) {
        const std::scoped_lock lock(backend().mutex);
        HdrState& own = hdr();
        if (own.settings.grade_lut == settings.grade_lut) {
            own.settings = settings;
            own.parameters = tone_parameters(settings);
            if (settings.on) {
                rt::add_view_listener(&listener());
            } else {
                rt::remove_view_listener(&listener());
            }
            return;
        }
    }
    if (!settings.grade_lut.empty()) {
        const std::string text = read_text_file(settings.grade_lut);
        std::string error;
        if (text.empty()) {
            get_log("vulkan: rt_grade_lut: %s can't be read - no grading table", settings.grade_lut.c_str());
        } else if (!parse_cube(text, &lut, &error)) {
            get_log("vulkan: rt_grade_lut: %s: %s - no grading table", settings.grade_lut.c_str(), error.c_str());
            lut = GradeLut{};
        } else {
            get_log("vulkan: rt_grade_lut: %s (%u x %u x %u)", settings.grade_lut.c_str(), lut.size, lut.size,
                    lut.size);
        }
    }
    {
        const std::scoped_lock lock(backend().mutex);
        HdrState& own = hdr();
        own.settings = settings;
        own.parameters = tone_parameters(settings);
        own.lut = std::move(lut);
        own.lut_changed = true;
    }
    if (settings.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

void set_hdr_lut(GradeLut lut)
{
    const std::scoped_lock lock(backend().mutex);
    HdrState& own = hdr();
    own.lut = std::move(lut);
    own.lut_changed = true;
}

HdrSettings hdr_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return hdr().settings;
}

bool parse_cube(std::string_view text, GradeLut* lut, std::string* error)
{
    GradeLut made;
    std::size_t at = 0;
    int line_number = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        ++line_number;
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> parts = cube_words(line);
        if (!parts.empty() && !read_cube_line(parts, &made, error)) {
            if (error->empty()) {
                *error = "line " + std::to_string(line_number) + " isn't three numbers";
            }
            return false;
        }
    }
    if (made.size == 0) {
        *error = "no LUT_3D_SIZE";
        return false;
    }
    const std::size_t wanted = static_cast<std::size_t>(made.size) * made.size * made.size;
    if (made.entries.size() != wanted) {
        *error = std::to_string(made.entries.size()) + " entries, not " + std::to_string(wanted);
        return false;
    }
    for (std::size_t channel = 0; channel < made.domain_min.size(); ++channel) {
        if (!(core::at(made.domain_max, channel) > core::at(made.domain_min, channel))) {
            *error = "DOMAIN_MAX not above DOMAIN_MIN";
            return false;
        }
    }
    *lut = std::move(made);
    return true;
}

std::array<float, 3> tone_map_reference(const ToneParameters& parameters, const GradeLut* lut,
                                        std::array<float, 3> colour)
{
    for (float& value : colour) {
        value = finite(value) * parameters.scale;
    }
    if (parameters.saturation != 1.0F) {
        float luma = 0.0F;
        for (std::size_t channel = 0; channel < colour.size(); ++channel) {
            luma += core::at(colour, channel) * core::at(kLuma, channel);
        }
        for (float& value : colour) {
            value = std::max(luma + ((value - luma) * parameters.saturation), 0.0F);
        }
    }
    if (parameters.contrast != 1.0F) {
        for (float& value : colour) {
            value = kMidGrey * std::pow(value / kMidGrey, parameters.contrast);
        }
    }
    for (float& value : colour) {
        if (parameters.curve == 0) {
            value = shoulder(parameters, value);
        } else {
            const float mapped = std::min(aces_curve(std::pow(value, kGamma)) * parameters.aces_scale, 1.0F);
            value = std::pow(mapped, 1.0F / kGamma);
        }
    }
    if (lut != nullptr && lut->size >= kLeastLut) {
        colour = graded(*lut, colour);
    }
    for (float& value : colour) {
        value = std::clamp(value, 0.0F, 1.0F);
    }
    return colour;
}

// --- the device ----------------------------------------------------------------------------------------------------

bool hdr_wanted(const Backend& state)
{
    if (!hdr().settings.on) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), kHdrFormat, &properties);
    constexpr VkFormatFeatureFlags kNeeded =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((properties.optimalTilingFeatures & kNeeded) != kNeeded) {
        get_log("vulkan: rt_hdr=1 but this device can't draw into 16-bit float targets - the 8-bit ones kept");
        return false;
    }
    return true;
}

bool record_tone_map_pass(Backend& state, const rt::ViewDrawn& view)
{
    HdrState& own = hdr();
    if (!own.settings.on || own.failed) {
        return false;
    }
    Image* const colour = colour_image_of(core::at(state.targets, 0));
    if (colour == nullptr || colour->format != kHdrFormat || colour->attachment_view == VK_NULL_HANDLE) {
        if (first_report(state, kReportNotFloat)) {
            get_log("vulkan: rt_hdr: a view drawn into an 8-bit target - not tone mapped there (it is clipped as the "
                    "original's)");
        }
        return false;
    }
    std::string error;
    const VkPipeline pipeline = pass_pipeline(state, colour->format, colour->samples, &error);
    const bool copy_made = pipeline != VK_NULL_HANDLE &&
                           ensure_image_like(state, &own.copy, *colour,
                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
                           ensure_lut_buffer(state);
    if (!copy_made) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_hdr: the tone-mapping pass couldn't be made (%s) - the frame shown clipped",
                    error.empty() ? "its images or buffer" : error.c_str());
        }
        return false;
    }
    // the viewport, inside the target
    const uint32_t left = std::min(core::at(view.rect, 0), colour->extent.width);
    const uint32_t top = std::min(core::at(view.rect, 1), colour->extent.height);
    const uint32_t width = std::min(core::at(view.rect, 2), colour->extent.width - left);
    const uint32_t height = std::min(core::at(view.rect, 3), colour->extent.height - top);
    if (width == 0 || height == 0) {
        return false;
    }
    const ToneParameters& tone = own.parameters;
    PassConstants constants{};
    constants.grade = {tone.scale, tone.saturation, tone.contrast, static_cast<float>(tone.curve)};
    constants.shape = {tone.knee, tone.room, tone.aces_scale, static_cast<float>(colour->samples)};
    const GradeLut& lut = own.lut;
    if (lut.size >= kLeastLut) {
        constants.lut = {static_cast<float>(lut.size), 0.0F, 0.0F, 0.0F};
        for (std::size_t channel = 0; channel < lut.domain_min.size(); ++channel) {
            const float low = core::at(lut.domain_min, channel);
            core::at(constants.lut_min, channel) = low;
            core::at(constants.lut_scale, channel) = 1.0F / (core::at(lut.domain_max, channel) - low);
        }
    }

    VkCommandBuffer commands = outside_pass(state);
    // the viewport's colour copied out
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(left), .y = static_cast<int32_t>(top), .z = 0};
    const VkImageCopy region{.srcSubresource = layers,
                             .srcOffset = corner,
                             .dstSubresource = layers,
                             .dstOffset = corner,
                             .extent = {.width = width, .height = height, .depth = 1}};
    vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.copy.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    Memory::transition(commands, &own.copy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // the pass: one triangle over the viewport
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(left), .y = static_cast<int32_t>(top)},
                        .extent = {.width = width, .height = height}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(left),
                              .y = static_cast<float>(top),
                              .width = static_cast<float>(width),
                              .height = static_cast<float>(height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    const VkDescriptorImageInfo source{.sampler = own.sampler,
                                       .imageView = own.copy.view,
                                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo table{.buffer = own.lut_buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    writes.front().dstBinding = 0;
    writes.front().descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes.front().pImageInfo = &source;
    writes.back().dstBinding = 1;
    writes.back().descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes.back().pBufferInfo = &table;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PassConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    // the target back where the Gpu's passes expect it; its bound state forgotten (pipeline, descriptors, dynamics)
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    own.copy.used = serial;
    ++own.passes;
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_hdr: the first tone-mapping pass (%ux%u, %u sample%s; exposure %+.2f, curve %u, white "
                "point %.2f - knee %.3f, saturation %.2f, contrast %.2f%s)",
                width, height, static_cast<unsigned>(colour->samples), colour->samples == 1 ? "" : "s",
                static_cast<double>(own.settings.exposure), tone.curve, static_cast<double>(own.settings.white_point),
                static_cast<double>(tone.knee), static_cast<double>(tone.saturation),
                static_cast<double>(tone.contrast), lut.size >= kLeastLut ? ", a grading table" : "");
    }
    return true;
}

uint64_t tone_map_passes() { return hdr().passes; }

bool read_hdr_as_bgra8(Backend& state, Image* image, Extent extent, std::vector<uint8_t>* bgra)
{
    const std::size_t pixels = static_cast<std::size_t>(extent.width) * extent.height;
    std::vector<uint8_t> raw(pixels * kHdrPixelBytes);
    if (image->format != kHdrFormat || image->samples != VK_SAMPLE_COUNT_1_BIT ||
        !state.memory.read_image_level(image, 0, &raw)) {
        return false;
    }
    bgra->resize(pixels * kBgraPixelBytes);
    const std::span<const uint8_t> from(raw);
    const std::span<uint8_t> into(*bgra);
    constexpr uint32_t kHighByte = 8;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const std::span<const uint8_t> texel = from.subspan(pixel * kHdrPixelBytes, kHdrPixelBytes);
        std::array<float, 4> rgba{};
        for (std::size_t channel = 0; channel < rgba.size(); ++channel) {
            const auto bits = static_cast<uint16_t>(core::at(texel, channel * 2) |
                                                    (core::at(texel, (channel * 2) + 1) << kHighByte));
            core::at(rgba, channel) = half_value(bits);
        }
        // (B, G, R, A)
        const std::span<uint8_t> out = into.subspan(pixel * kBgraPixelBytes, kBgraPixelBytes);
        core::at(out, 0) = to_byte(core::at(rgba, 2));
        core::at(out, 1) = to_byte(core::at(rgba, 1));
        core::at(out, 2) = to_byte(core::at(rgba, 0));
        core::at(out, 3) = to_byte(core::at(rgba, 3));
    }
    return true;
}

Image* resolve_for_copy(Backend& state, VkCommandBuffer commands, Image* multisampled)
{
    HdrState& own = hdr();
    Image single = *multisampled;
    single.samples = VK_SAMPLE_COUNT_1_BIT;
    if (!ensure_image_like(state, &own.resolved, single,
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        if (first_report(state, kReportCopy)) {
            get_log("vulkan: rt_hdr: no image to resolve a multisampled float target into - its copy refused");
        }
        return nullptr;
    }
    Memory::record_resolve(commands, multisampled, &own.resolved);
    own.resolved.used = state.frames.serial();
    return &own.resolved;
}

void release_hdr(Backend& state)
{
    HdrState& own = hdr();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const PassPipeline& made : own.pipelines) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
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
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.copy);
        state.memory.destroy_image(&own.resolved);
        state.memory.destroy_buffer(&own.lut_buffer);
    }
    own.pipelines.clear();
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.lut_changed = true;
    own.failed = false;
}

} // namespace render::vulkan
