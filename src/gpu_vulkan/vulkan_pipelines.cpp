// NOT ORIGINAL - the Vulkan Gpu's pipelines and their caches (vulkan_pipelines.h).
#include "gpu_vulkan/vulkan_pipelines.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/file/disk.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_shaders.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_draw_state.h"

namespace render::vulkan {

namespace {

constexpr const char* kShaderFile = "thug2-decomp-vulkan-shaders.bin";
constexpr const char* kPipelineFile = "thug2-decomp-vulkan-pipelines.bin";
constexpr uint32_t kShaderFileMagic = 0x53324854U; // "TH2S"
constexpr uint32_t kShaderFileVersion = 1;
constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr std::size_t kMostSpirvWords = 1U << 20U; // (a sanity limit for the file's entries)

uint64_t fnv1a(std::span<const uint8_t> bytes)
{
    uint64_t hash = kFnvOffset;
    for (const uint8_t byte : bytes) {
        hash = (hash ^ byte) * kFnvPrime;
    }
    return hash;
}

uint64_t text_hash(const std::string& text)
{
    const std::span<const char> chars(text);
    uint64_t hash = kFnvOffset;
    for (const char ch : chars) {
        hash = (hash ^ static_cast<uint8_t>(ch)) * kFnvPrime;
    }
    return hash;
}

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// A whole file's bytes (empty when it isn't there).
std::vector<uint8_t> read_file(const std::string& path)
{
    std::vector<uint8_t> bytes;
    fs::DiskStream* const in = fs::disk().open_stream(path.c_str(), "rb", fs::StreamSharing::kAll);
    if (in == nullptr) {
        return bytes;
    }
    if (fs::disk().stream_seek(in, 0, SEEK_END) == 0) {
        const long size = fs::disk().stream_tell(in);
        if (size > 0 && fs::disk().stream_seek(in, 0, SEEK_SET) == 0) {
            bytes.resize(static_cast<std::size_t>(size));
            const std::size_t got = fs::disk().stream_read(in, bytes.data(), 1, bytes.size());
            bytes.resize(got);
        }
    }
    fs::disk().stream_close(in);
    return bytes;
}

bool write_file(const std::string& path, std::span<const uint8_t> bytes)
{
    fs::DiskStream* const out = fs::disk().open_stream(path.c_str(), "wb", fs::StreamSharing::kAll);
    if (out == nullptr) {
        return false;
    }
    const std::size_t written = fs::disk().stream_write(out, bytes.data(), 1, bytes.size());
    return fs::disk().stream_close(out) == 0 && written == bytes.size();
}

// A little-endian reader over a file's bytes.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    template <typename T>
    bool read(T* value)
    {
        if (at_ + sizeof(T) > bytes_.size()) {
            return false;
        }
        std::memcpy(value, bytes_.subspan(at_, sizeof(T)).data(), sizeof(T));
        at_ += sizeof(T);
        return true;
    }
    bool read_words(std::size_t count, std::vector<uint32_t>* words)
    {
        const std::size_t size = count * sizeof(uint32_t);
        if (at_ + size > bytes_.size()) {
            return false;
        }
        words->resize(count);
        std::memcpy(words->data(), bytes_.subspan(at_, size).data(), size);
        at_ += size;
        return true;
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t at_ = 0;
};

template <typename T>
void append(std::vector<uint8_t>& out, const T& value)
{
    std::array<uint8_t, sizeof(T)> raw{};
    std::memcpy(raw.data(), &value, sizeof(T));
    out.insert(out.end(), raw.begin(), raw.end());
}

} // namespace

std::size_t PipelineKeyHash::operator()(const PipelineKey& key) const
{
    std::array<uint8_t, sizeof(PipelineKey)> raw{};
    std::memcpy(raw.data(), &key, sizeof(PipelineKey));
    return static_cast<std::size_t>(fnv1a(raw));
}

bool Pipelines::init(VkDevice device, VkPhysicalDevice /*physical*/, const std::string& folder)
{
    device_ = device;
    folder_ = folder;
    std::array<VkDescriptorSetLayoutBinding, 2 + kStageCount> bindings{};
    core::at(bindings, 0) = {.binding = kVertexUniformsBinding,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                             .pImmutableSamplers = nullptr};
    core::at(bindings, 1) = {.binding = kFragmentUniformsBinding,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr};
    for (uint32_t stage = 0; stage < kStageCount; ++stage) {
        core::at(bindings, 2 + stage) = {.binding = kFirstTextureBinding + stage,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    if (vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &set_layout_) != VK_SUCCESS) {
        return false;
    }
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &set_layout_,
                                                 .pushConstantRangeCount = 0,
                                                 .pPushConstantRanges = nullptr};
    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        return false;
    }
    load(folder);
    return true;
}

void Pipelines::load(const std::string& folder)
{
    std::vector<uint8_t> cache_bytes;
    if (!folder.empty()) {
        cache_bytes = read_file(folder + kPipelineFile);
        const std::vector<uint8_t> shader_bytes = read_file(folder + kShaderFile);
        Reader reader(shader_bytes);
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t count = 0;
        if (reader.read(&magic) && reader.read(&version) && reader.read(&count) && magic == kShaderFileMagic &&
            version == kShaderFileVersion) {
            for (uint32_t entry = 0; entry < count; ++entry) {
                uint64_t hash = 0;
                uint32_t words = 0;
                std::vector<uint32_t> spirv;
                if (!reader.read(&hash) || !reader.read(&words) || words > kMostSpirvWords ||
                    !reader.read_words(words, &spirv)) {
                    break;
                }
                spirv_.emplace(hash, std::move(spirv));
            }
        }
    }
    VkPipelineCacheCreateInfo info{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                                   .pNext = nullptr,
                                   .flags = 0,
                                   .initialDataSize = cache_bytes.size(),
                                   .pInitialData = cache_bytes.empty() ? nullptr : cache_bytes.data()};
    if (vkCreatePipelineCache(device_, &info, nullptr, &cache_) != VK_SUCCESS) {
        // (another driver's or a damaged file: an empty cache)
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        if (vkCreatePipelineCache(device_, &info, nullptr, &cache_) != VK_SUCCESS) {
            cache_ = VK_NULL_HANDLE;
        }
    }
}

void Pipelines::save()
{
    if (!dirty_ || folder_.empty() || device_ == nullptr) {
        return;
    }
    dirty_ = false;
    std::vector<uint8_t> shaders;
    append(shaders, kShaderFileMagic);
    append(shaders, kShaderFileVersion);
    append(shaders, static_cast<uint32_t>(spirv_.size()));
    for (const auto& [hash, words] : spirv_) {
        append(shaders, hash);
        append(shaders, static_cast<uint32_t>(words.size()));
        const std::size_t at = shaders.size();
        shaders.resize(at + (words.size() * sizeof(uint32_t)));
        std::memcpy(std::span(shaders).subspan(at).data(), words.data(), words.size() * sizeof(uint32_t));
    }
    write_file(folder_ + kShaderFile, shaders);
    if (cache_ != VK_NULL_HANDLE) {
        std::size_t size = 0;
        if (vkGetPipelineCacheData(device_, cache_, &size, nullptr) == VK_SUCCESS && size > 0) {
            std::vector<uint8_t> data(size);
            if (vkGetPipelineCacheData(device_, cache_, &size, data.data()) == VK_SUCCESS) {
                data.resize(size);
                write_file(folder_ + kPipelineFile, data);
            }
        }
    }
}

void Pipelines::shutdown()
{
    if (device_ == nullptr) {
        return;
    }
    save();
    for (const auto& [key, pipeline] : pipelines_) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
    }
    for (const auto& [key, module] : vertex_modules_) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    for (const auto& [key, module] : fragment_modules_) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    for (const auto& [key, pipeline] : screen_pipelines_) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
    }
    for (const auto& [key, module] : screen_modules_) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    screen_pipelines_.clear();
    screen_modules_.clear();
    pipelines_.clear();
    vertex_modules_.clear();
    fragment_modules_.clear();
    spirv_.clear();
    if (cache_ != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device_, cache_, nullptr);
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    }
    cache_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    device_ = nullptr;
    stats_ = {};
}

VkShaderModule Pipelines::spirv_module(ShaderStage stage, const std::string& source, std::string* error)
{
    const uint64_t hash = text_hash(source);
    auto kept = spirv_.find(hash);
    if (kept == spirv_.end()) {
        const auto start = std::chrono::steady_clock::now();
        CompiledShader compiled = compile_glsl(stage, source);
        stats_.compile_seconds += seconds_since(start);
        ++stats_.shaders_compiled;
        if (!compiled.ok) {
            if (error != nullptr) {
                *error = compiled.log + "\n" + source;
            }
            kept = spirv_.emplace(hash, std::vector<uint32_t>{}).first;
        } else {
            kept = spirv_.emplace(hash, std::move(compiled.spirv)).first;
            dirty_ = true;
        }
    } else {
        ++stats_.shaders_from_disk;
    }
    VkShaderModule made = VK_NULL_HANDLE;
    if (!kept->second.empty()) {
        const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                            .pNext = nullptr,
                                            .flags = 0,
                                            .codeSize = kept->second.size() * sizeof(uint32_t),
                                            .pCode = kept->second.data()};
        if (vkCreateShaderModule(device_, &info, nullptr, &made) != VK_SUCCESS) {
            made = VK_NULL_HANDLE;
        }
    }
    return made;
}

VkShaderModule Pipelines::module(bool vertex, const ShaderKey& key, std::string* error)
{
    if (vertex) {
        const auto found = vertex_modules_.find(key.vertex);
        if (found != vertex_modules_.end()) {
            return found->second;
        }
    } else {
        const auto found = fragment_modules_.find(key.fragment);
        if (found != fragment_modules_.end()) {
            return found->second;
        }
    }
    const std::string source = vertex ? vertex_shader_glsl(key.vertex) : fragment_shader_glsl(key.fragment);
    const VkShaderModule made = spirv_module(vertex ? ShaderStage::kVertex : ShaderStage::kFragment, source, error);
    if (vertex) {
        vertex_modules_.emplace(key.vertex, made);
    } else {
        fragment_modules_.emplace(key.fragment, made);
    }
    return made;
}

VkPipeline Pipelines::pipeline(const PipelineKey& key, std::string* error)
{
    const auto found = pipelines_.find(key);
    if (found != pipelines_.end()) {
        return found->second;
    }
    const VkShaderModule vertex = module(true, key.shaders, error);
    const VkShaderModule fragment = module(false, key.shaders, error);
    VkPipeline made = VK_NULL_HANDLE;
    if (vertex != VK_NULL_HANDLE && fragment != VK_NULL_HANDLE) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vertex,
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
        const VertexLayout layout = vertex_layout(key.shaders.vertex.fvf);
        const VkVertexInputBindingDescription binding{
            .binding = 0, .stride = layout.bytes, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        const VkPipelineVertexInputStateCreateInfo input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = layout.count,
            .pVertexAttributeDescriptions = layout.attributes.data()};
        constexpr std::array<VkPrimitiveTopology, 3> kClassTopology{
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = core::at(kClassTopology, std::min<std::size_t>(key.topology_class, 2)),
            .primitiveRestartEnable = VK_FALSE};
        const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .viewportCount = 1,
                                                         .pViewports = nullptr,
                                                         .scissorCount = 1,
                                                         .pScissors = nullptr};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = key.wireframe != 0 ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_TRUE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = static_cast<VkSampleCountFlagBits>(key.samples),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE};
        const VkPipelineDepthStencilStateCreateInfo depth{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0F,
            .maxDepthBounds = 1.0F};
        const VkBlendFactor source = blend_factor(key.source_blend);
        const VkBlendFactor dest = blend_factor(key.dest_blend);
        const VkBlendOp operation = blend_op(key.blend_operation);
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = key.blend != 0 ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = source,
            .dstColorBlendFactor = dest,
            .colorBlendOp = operation,
            .srcAlphaBlendFactor = source,
            .dstAlphaBlendFactor = dest,
            .alphaBlendOp = operation,
            .colorWriteMask = key.write_mask};
        const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .logicOpEnable = VK_FALSE,
                                                        .logicOp = VK_LOGIC_OP_COPY,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &blend_attachment,
                                                        .blendConstants = {}};
        constexpr std::array<VkDynamicState, 11> kDynamic{
            VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_CULL_MODE,          VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_DEPTH_BIAS,         VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
        const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .dynamicStateCount = static_cast<uint32_t>(kDynamic.size()),
                                                       .pDynamicStates = kDynamic.data()};
        const auto colour_format = static_cast<VkFormat>(key.colour_format);
        const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                      .pNext = nullptr,
                                                      .viewMask = 0,
                                                      .colorAttachmentCount = 1,
                                                      .pColorAttachmentFormats = &colour_format,
                                                      .depthAttachmentFormat = static_cast<VkFormat>(key.depth_format),
                                                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
        const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                .pNext = &rendering,
                                                .flags = 0,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &input,
                                                .pInputAssemblyState = &assembly,
                                                .pTessellationState = nullptr,
                                                .pViewportState = &viewport,
                                                .pRasterizationState = &raster,
                                                .pMultisampleState = &multisample,
                                                .pDepthStencilState = &depth,
                                                .pColorBlendState = &blend,
                                                .pDynamicState = &dynamic,
                                                .layout = layout_,
                                                .renderPass = VK_NULL_HANDLE,
                                                .subpass = 0,
                                                .basePipelineHandle = VK_NULL_HANDLE,
                                                .basePipelineIndex = -1};
        const auto start = std::chrono::steady_clock::now();
        if (vkCreateGraphicsPipelines(device_, cache_, 1, &info, nullptr, &made) != VK_SUCCESS) {
            made = VK_NULL_HANDLE;
            if (error != nullptr && error->empty()) {
                *error = "vkCreateGraphicsPipelines refused it";
            }
        }
        stats_.pipeline_seconds += seconds_since(start);
    }
    if (made == VK_NULL_HANDLE) {
        ++stats_.refused;
    } else {
        ++stats_.pipelines;
        dirty_ = true;
    }
    pipelines_.emplace(key, made);
    return made;
}

namespace {

// The full-screen passes' vertex shader: the triangle (-1,-1) (3,-1) (-1,3), its texture coordinates 0 - 2 (0 - 1 over
// the target).
constexpr const char* kScreenPassVertex = R"(#version 450
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

} // namespace

VkPipeline Pipelines::screen_pass(VkFormat colour_format, const std::string& fragment, std::string* error)
{
    const uint64_t key = text_hash(fragment) ^ (static_cast<uint64_t>(colour_format) * kFnvPrime);
    const auto found = screen_pipelines_.find(key);
    if (found != screen_pipelines_.end()) {
        return found->second;
    }
    const auto module_of = [this, error](ShaderStage stage, const std::string& source) {
        const uint64_t hash = text_hash(source);
        const auto kept = screen_modules_.find(hash);
        if (kept != screen_modules_.end()) {
            return kept->second;
        }
        const VkShaderModule made = spirv_module(stage, source, error);
        screen_modules_.emplace(hash, made);
        return made;
    };
    const VkShaderModule vertex = module_of(ShaderStage::kVertex, kScreenPassVertex);
    const VkShaderModule pixel = module_of(ShaderStage::kFragment, fragment);
    VkPipeline made = VK_NULL_HANDLE;
    if (vertex != VK_NULL_HANDLE && pixel != VK_NULL_HANDLE) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vertex,
             .pName = "main",
             .pSpecializationInfo = nullptr},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = pixel,
             .pName = "main",
             .pSpecializationInfo = nullptr},
        }};
        const VkPipelineVertexInputStateCreateInfo input{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .vertexBindingDescriptionCount = 0,
                                                         .pVertexBindingDescriptions = nullptr,
                                                         .vertexAttributeDescriptionCount = 0,
                                                         .pVertexAttributeDescriptions = nullptr};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE};
        const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .viewportCount = 1,
                                                         .pViewports = nullptr,
                                                         .scissorCount = 1,
                                                         .pScissors = nullptr};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE};
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .logicOpEnable = VK_FALSE,
                                                        .logicOp = VK_LOGIC_OP_COPY,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &blend_attachment,
                                                        .blendConstants = {}};
        constexpr std::array<VkDynamicState, 2> kDynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .dynamicStateCount = static_cast<uint32_t>(kDynamic.size()),
                                                       .pDynamicStates = kDynamic.data()};
        const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                      .pNext = nullptr,
                                                      .viewMask = 0,
                                                      .colorAttachmentCount = 1,
                                                      .pColorAttachmentFormats = &colour_format,
                                                      .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                                                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
        const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                .pNext = &rendering,
                                                .flags = 0,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &input,
                                                .pInputAssemblyState = &assembly,
                                                .pTessellationState = nullptr,
                                                .pViewportState = &viewport,
                                                .pRasterizationState = &raster,
                                                .pMultisampleState = &multisample,
                                                .pDepthStencilState = nullptr,
                                                .pColorBlendState = &blend,
                                                .pDynamicState = &dynamic,
                                                .layout = layout_,
                                                .renderPass = VK_NULL_HANDLE,
                                                .subpass = 0,
                                                .basePipelineHandle = VK_NULL_HANDLE,
                                                .basePipelineIndex = -1};
        const auto start = std::chrono::steady_clock::now();
        if (vkCreateGraphicsPipelines(device_, cache_, 1, &info, nullptr, &made) != VK_SUCCESS) {
            made = VK_NULL_HANDLE;
            if (error != nullptr && error->empty()) {
                *error = "vkCreateGraphicsPipelines refused it";
            }
        }
        stats_.pipeline_seconds += seconds_since(start);
    }
    if (made == VK_NULL_HANDLE) {
        ++stats_.refused;
    } else {
        ++stats_.pipelines;
        dirty_ = true;
    }
    screen_pipelines_.emplace(key, made);
    return made;
}

namespace {

// The UI's shaders (Pipelines::ui_pass): ImGui's vertices to the target through the vertex block's scale and offset;
// each pixel the vertex colour times the texture, as ImGui's own backends draw.
static_assert(kVertexUniformsBinding == 0 && kFirstTextureBinding == 2, "the UI's shaders name the bindings");
constexpr const char* kUiVertex = R"(#version 450
layout(set = 0, binding = 0, std140) uniform Ui { vec4 scale_offset; } ui;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_colour;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;
void main()
{
    v_uv = a_uv;
    v_colour = a_colour;
    gl_Position = vec4(a_position * ui.scale_offset.xy + ui.scale_offset.zw, 0.0, 1.0);
}
)";
constexpr const char* kUiFragment = R"(#version 450
layout(set = 0, binding = 2) uniform sampler2D t0;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 o_colour;
void main()
{
    o_colour = v_colour * texture(t0, v_uv);
}
)";

} // namespace

VkPipeline Pipelines::ui_pass(VkFormat colour_format, VkFormat depth_format, uint32_t samples, std::string* error)
{
    const std::string vertex_source = kUiVertex;
    const std::string fragment_source = kUiFragment;
    const uint64_t key = (text_hash(fragment_source) ^ (static_cast<uint64_t>(colour_format) * kFnvPrime) ^
                          (static_cast<uint64_t>(depth_format) * kFnvPrime * kFnvPrime)) +
                         samples;
    const auto found = screen_pipelines_.find(key);
    if (found != screen_pipelines_.end()) {
        return found->second;
    }
    const auto module_of = [this, error](ShaderStage stage, const std::string& source) {
        const uint64_t hash = text_hash(source);
        const auto kept = screen_modules_.find(hash);
        if (kept != screen_modules_.end()) {
            return kept->second;
        }
        const VkShaderModule made = spirv_module(stage, source, error);
        screen_modules_.emplace(hash, made);
        return made;
    };
    const VkShaderModule vertex = module_of(ShaderStage::kVertex, vertex_source);
    const VkShaderModule pixel = module_of(ShaderStage::kFragment, fragment_source);
    VkPipeline made = VK_NULL_HANDLE;
    if (vertex != VK_NULL_HANDLE && pixel != VK_NULL_HANDLE) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vertex,
             .pName = "main",
             .pSpecializationInfo = nullptr},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = pixel,
             .pName = "main",
             .pSpecializationInfo = nullptr},
        }};
        const VkVertexInputBindingDescription binding{
            .binding = 0, .stride = kUiVertexBytes, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 3> attributes{{
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = kUiUvOffset},
            {.location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = kUiColourOffset},
        }};
        const VkPipelineVertexInputStateCreateInfo input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE};
        const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .viewportCount = 1,
                                                         .pViewports = nullptr,
                                                         .scissorCount = 1,
                                                         .pScissors = nullptr};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE};
        const VkPipelineDepthStencilStateCreateInfo depth{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_ALWAYS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0F,
            .maxDepthBounds = 1.0F};
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .logicOpEnable = VK_FALSE,
                                                        .logicOp = VK_LOGIC_OP_COPY,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &blend_attachment,
                                                        .blendConstants = {}};
        constexpr std::array<VkDynamicState, 2> kDynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .dynamicStateCount = static_cast<uint32_t>(kDynamic.size()),
                                                       .pDynamicStates = kDynamic.data()};
        const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                      .pNext = nullptr,
                                                      .viewMask = 0,
                                                      .colorAttachmentCount = 1,
                                                      .pColorAttachmentFormats = &colour_format,
                                                      .depthAttachmentFormat = depth_format,
                                                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
        const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                .pNext = &rendering,
                                                .flags = 0,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &input,
                                                .pInputAssemblyState = &assembly,
                                                .pTessellationState = nullptr,
                                                .pViewportState = &viewport,
                                                .pRasterizationState = &raster,
                                                .pMultisampleState = &multisample,
                                                .pDepthStencilState = &depth,
                                                .pColorBlendState = &blend,
                                                .pDynamicState = &dynamic,
                                                .layout = layout_,
                                                .renderPass = VK_NULL_HANDLE,
                                                .subpass = 0,
                                                .basePipelineHandle = VK_NULL_HANDLE,
                                                .basePipelineIndex = -1};
        const auto start = std::chrono::steady_clock::now();
        if (vkCreateGraphicsPipelines(device_, cache_, 1, &info, nullptr, &made) != VK_SUCCESS) {
            made = VK_NULL_HANDLE;
            if (error != nullptr && error->empty()) {
                *error = "vkCreateGraphicsPipelines refused it";
            }
        }
        stats_.pipeline_seconds += seconds_since(start);
    }
    if (made == VK_NULL_HANDLE) {
        ++stats_.refused;
    } else {
        ++stats_.pipelines;
        dirty_ = true;
    }
    screen_pipelines_.emplace(key, made);
    return made;
}

} // namespace render::vulkan
