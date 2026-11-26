// NOT ORIGINAL - the fixed-function shader check's GPU part (ff_shader_check_gpu.h).
#include "tests/ff_shader_check_gpu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_pipelines.h"

namespace ff_check {

namespace {

using render::vulkan::FragmentKey;
using render::vulkan::VertexKey;

constexpr int kWindowSize = 64;
constexpr std::size_t kReportedFailures = 5;
constexpr uint32_t kFloatBytes = 4;
constexpr uint32_t kColourBytes = 4;
constexpr uint32_t kPositionFloats = 3;
constexpr uint32_t kRhwFloats = 4;
constexpr std::array<VkFormat, 4> kFloatFormats = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                                   VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};

// (round TRAIN) The validation layer's errors while the modules and pipelines are made - a layout that doesn't match a
// shader's resources (VUID-VkGraphicsPipelineCreateInfo-layout-07988) passed silently before: each one fails the sweep.
std::size_t& validation_errors()
{
    static std::size_t count = 0;
    return count;
}

void print_message(const std::string& text)
{
    text::print("  vulkan: %s\n", text.c_str());
    if (text.starts_with("validation error")) {
        ++validation_errors();
    }
}

// The vertex format's attributes, in Direct3D 9's order (position, normal, diffuse, specular, the sets), one binding.
struct VertexLayout {
    std::vector<VkVertexInputAttributeDescription> attributes;
    uint32_t stride = 0;
};

VertexLayout layout_of(uint32_t fvf)
{
    VertexLayout layout;
    const auto add = [&layout](uint32_t location, VkFormat format, uint32_t bytes) {
        layout.attributes.push_back({.location = location, .binding = 0, .format = format, .offset = layout.stride});
        layout.stride += bytes;
    };
    const bool rhw = render::vulkan::fvf_pre_transformed(fvf);
    const uint32_t position = rhw ? kRhwFloats : kPositionFloats;
    add(render::vulkan::kPositionLocation, core::at(kFloatFormats, position - 1), position * kFloatBytes);
    if ((fvf & render::vulkan::kFvfNormal) != 0U) {
        add(render::vulkan::kNormalLocation, VK_FORMAT_R32G32B32_SFLOAT, kPositionFloats * kFloatBytes);
    }
    if ((fvf & render::vulkan::kFvfDiffuse) != 0U) {
        add(render::vulkan::kDiffuseLocation, VK_FORMAT_B8G8R8A8_UNORM, kColourBytes);
    }
    if ((fvf & render::vulkan::kFvfSpecular) != 0U) {
        add(render::vulkan::kSpecularLocation, VK_FORMAT_B8G8R8A8_UNORM, kColourBytes);
    }
    for (uint32_t set = 0; set < render::vulkan::fvf_tex_count(fvf); ++set) {
        const uint32_t size = render::vulkan::fvf_tex_size(fvf, set);
        add(render::vulkan::kFirstTexSetLocation + set, core::at(kFloatFormats, size - 1), size * kFloatBytes);
    }
    return layout;
}

// The objects the pipelines share.
struct Shared {
    VkDevice device = nullptr;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout light_set_layout = VK_NULL_HANDLE; // (round FL: set 1, the view's light inputs)
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkShaderModule> vertex_modules;
    std::vector<VkShaderModule> fragment_modules;

    Shared() = default;
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;
    ~Shared()
    {
        if (!device) {
            return;
        }
        for (const VkShaderModule module : vertex_modules) {
            vkDestroyShaderModule(device, module, nullptr);
        }
        for (const VkShaderModule module : fragment_modules) {
            vkDestroyShaderModule(device, module, nullptr);
        }
        vkDestroyRenderPass(device, render_pass, nullptr);
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, light_set_layout, nullptr);
    }
};

bool make_layouts(Shared& shared)
{
    // (round TRAIN) the game's own bindings (vulkan_pipelines.h draw_set_bindings / light_set_bindings) - before, a copy
    // here without the water's, the snow's, the paint's, the wet ground's and the emission map's: every --water
    // pipeline's shaders read bindings its layout didn't declare (VUID-VkGraphicsPipelineCreateInfo-layout-07988). The
    // scene's binding is only in a ray-tracing device's layout.
    const std::vector<VkDescriptorSetLayoutBinding> bindings = render::vulkan::draw_set_bindings(false);
    const VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                      .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                      .pBindings = bindings.data()};
    if (vkCreateDescriptorSetLayout(shared.device, &set_info, nullptr, &shared.set_layout) != VK_SUCCESS) {
        return false;
    }
    // (round V; round FL: a set of their own, kLightInputsSet) the view's light inputs, the front layer's with them
    const std::vector<VkDescriptorSetLayoutBinding> light_bindings = render::vulkan::light_set_bindings();
    const VkDescriptorSetLayoutCreateInfo light_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                        .bindingCount = static_cast<uint32_t>(light_bindings.size()),
                                                        .pBindings = light_bindings.data()};
    if (vkCreateDescriptorSetLayout(shared.device, &light_info, nullptr, &shared.light_set_layout) != VK_SUCCESS) {
        return false;
    }
    const std::array<VkDescriptorSetLayout, 2> set_layouts{shared.set_layout, shared.light_set_layout};
    const VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                    .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
                                                    .pSetLayouts = set_layouts.data()};
    return vkCreatePipelineLayout(shared.device, &layout_info, nullptr, &shared.pipeline_layout) == VK_SUCCESS;
}

bool make_render_pass(Shared& shared, VkFormat colour, VkFormat depth)
{
    const std::array<VkAttachmentDescription, 2> attachments = {{
        {.format = colour,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {.format = depth,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
    }};
    const VkAttachmentReference colour_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth_ref = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          .colorAttachmentCount = 1,
                                          .pColorAttachments = &colour_ref,
                                          .pDepthStencilAttachment = &depth_ref};
    const VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                         .attachmentCount = static_cast<uint32_t>(attachments.size()),
                                         .pAttachments = attachments.data(),
                                         .subpassCount = 1,
                                         .pSubpasses = &subpass};
    return vkCreateRenderPass(shared.device, &info, nullptr, &shared.render_pass) == VK_SUCCESS;
}

// The modules, in the keys' order; the refused ones counted (and left VK_NULL_HANDLE).
std::size_t make_modules(VkDevice device, const std::vector<const std::vector<uint32_t>*>& spirv,
                         std::vector<VkShaderModule>& modules)
{
    std::size_t refused = 0;
    for (const std::vector<uint32_t>* words : spirv) {
        if (words == nullptr) {
            modules.push_back(VK_NULL_HANDLE);
            continue;
        }
        const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .codeSize = words->size() * sizeof(uint32_t),
                                               .pCode = words->data()};
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
            ++refused;
        }
        modules.push_back(module);
    }
    return refused;
}

VkResult make_pipeline(const Shared& shared, const render::vulkan::ShaderKey& key, VkShaderModule vertex,
                       VkShaderModule fragment)
{
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment,
         .pName = "main"},
    }};
    VertexLayout layout = layout_of(key.vertex.fvf);
    // (the upscaler's motion, as vulkan_pipelines.cpp: binding 1 - the frame before's vertices, the same layout - its
    // position)
    const std::array<VkVertexInputBindingDescription, 2> bindings{{
        {.binding = 0, .stride = layout.stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        {.binding = 1, .stride = layout.stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
    }};
    if (key.vertex.motion != 0U) {
        layout.attributes.push_back(
            {.location = render::vulkan::kPreviousPositionLocation, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0});
    }
    const VkPipelineVertexInputStateCreateInfo input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = key.vertex.motion != 0U ? 2U : 1U,
        .pVertexBindingDescriptions = bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(layout.attributes.size()),
        .pVertexAttributeDescriptions = layout.attributes.data()};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = key.vertex.points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                        .viewportCount = 1,
                                                        .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0F};
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                       .attachmentCount = 1,
                                                       .pAttachments = &blend_attachment};
    const std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
                                                      .pDynamicStates = dynamic_states.data()};
    const VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                               .stageCount = static_cast<uint32_t>(stages.size()),
                                               .pStages = stages.data(),
                                               .pVertexInputState = &input,
                                               .pInputAssemblyState = &assembly,
                                               .pViewportState = &viewport,
                                               .pRasterizationState = &raster,
                                               .pMultisampleState = &multisample,
                                               .pDepthStencilState = &depth,
                                               .pColorBlendState = &blend,
                                               .pDynamicState = &dynamic,
                                               .layout = shared.pipeline_layout,
                                               .renderPass = shared.render_pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(shared.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    if (result == VK_SUCCESS) {
        vkDestroyPipeline(shared.device, pipeline, nullptr);
    }
    return result;
}

template <typename Key, typename Hash>
std::unordered_map<Key, std::size_t, Hash> index_of(const std::vector<Key>& keys)
{
    std::unordered_map<Key, std::size_t, Hash> indices;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        indices.emplace(core::at(keys, index), index);
    }
    return indices;
}

void destroy_modules(VkDevice device, std::vector<VkShaderModule>& modules)
{
    for (const VkShaderModule module : modules) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    modules.clear();
}

} // namespace

struct GpuSession::Impl {
    SDL_Window* window = nullptr;
    render::vulkan::Context context;
    std::unique_ptr<Shared> shared;
    std::unordered_map<VertexKey, std::size_t, render::vulkan::VertexKeyHash> vertex_index;
    int pipeline_limit = -1;
    std::size_t modules = 0;
    std::size_t refused_modules = 0;
    std::size_t made = 0;
    std::size_t refused_pipelines = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    ~Impl()
    {
        shared.reset(); // (its modules and layouts go before the device does)
        context.destroy();
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
};

GpuSession::GpuSession() : impl_(std::make_unique<Impl>()) {}

GpuSession::~GpuSession() = default;

std::unique_ptr<GpuSession> GpuSession::open(int device_index, int pipeline_limit, bool& ok)
{
    ok = true;
    if (!render::vulkan::open_loader()) {
        text::print("GPU part skipped: no Vulkan loader for this process\n");
        return nullptr;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("GPU part skipped: SDL_Init: %s\n", SDL_GetError());
        return nullptr;
    }
    std::unique_ptr<GpuSession> session(new GpuSession());
    Impl& impl = *session->impl_;
    impl.pipeline_limit = pipeline_limit;
    impl.window = SDL_CreateWindow("thug2 fixed-function shader check", kWindowSize, kWindowSize,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (impl.window == nullptr) {
        text::print("GPU part skipped: SDL_CreateWindow: %s\n", SDL_GetError());
        return nullptr;
    }
    const render::vulkan::ContextOptions options = {
        .validation = true,
        .device_index = device_index,
        .on_message = print_message,
        // (the game's device has them where it's offered: the light inputs' guides are stored from a fragment shader -
        // fragmentStoresAndAtomics comes with ray tracing)
        .ray_tracing = true};
    if (!impl.context.create(impl.window, options)) {
        text::print("GPU part skipped: no Vulkan context\n");
        return nullptr;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(impl.context.physical_device(), &properties);
    text::print("GPU: %s\n", static_cast<const char*>(properties.deviceName));
    impl.shared = std::make_unique<Shared>();
    Shared& shared = *impl.shared;
    shared.device = impl.context.device();
    static constexpr std::array<VkFormat, 2> kDepthFormats = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    const VkFormat depth = render::vulkan::first_depth_format(impl.context.physical_device(), kDepthFormats);
    if (!make_layouts(shared) || !make_render_pass(shared, impl.context.surface_format().format, depth)) {
        text::print("the pipeline layout or render pass couldn't be made\n");
        ok = false;
        return nullptr;
    }
    return session;
}

void GpuSession::add_vertex_modules(const std::vector<VertexKey>& keys,
                                    const std::vector<const std::vector<uint32_t>*>& spirv)
{
    impl_->vertex_index = index_of<VertexKey, render::vulkan::VertexKeyHash>(keys);
    impl_->refused_modules += make_modules(impl_->shared->device, spirv, impl_->shared->vertex_modules);
    impl_->modules += spirv.size();
}

void GpuSession::run_batch(const std::vector<const std::vector<uint32_t>*>& fragment_spirv,
                           const std::vector<BatchPair>& pairs)
{
    Impl& impl = *impl_;
    Shared& shared = *impl.shared;
    impl.refused_modules += make_modules(shared.device, fragment_spirv, shared.fragment_modules);
    impl.modules += fragment_spirv.size();
    for (const BatchPair& pair : pairs) {
        if (impl.pipeline_limit >= 0 && std::cmp_greater_equal(impl.made, impl.pipeline_limit)) {
            break;
        }
        const render::vulkan::ShaderKey& key = *pair.key;
        const VkShaderModule vertex = core::at(shared.vertex_modules, impl.vertex_index.at(key.vertex));
        const VkShaderModule fragment = core::at(shared.fragment_modules, pair.fragment);
        if (vertex == VK_NULL_HANDLE || fragment == VK_NULL_HANDLE) {
            continue;
        }
        ++impl.made;
        const VkResult result = make_pipeline(shared, key, vertex, fragment);
        if (result != VK_SUCCESS && impl.refused_pipelines++ < kReportedFailures) {
            text::print("pipeline refused (%d): %s | %s\n", static_cast<int>(result),
                        render::vulkan::describe(key.vertex).c_str(), render::vulkan::describe(key.fragment).c_str());
        }
    }
    destroy_modules(shared.device, shared.fragment_modules);
}

bool GpuSession::finish()
{
    const Impl& impl = *impl_;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - impl.start).count();
    text::print("GPU: %u modules (%u refused), %u pipelines (%u refused) in %.1f s\n",
                static_cast<unsigned>(impl.modules), static_cast<unsigned>(impl.refused_modules),
                static_cast<unsigned>(impl.made), static_cast<unsigned>(impl.refused_pipelines), seconds);
    if (validation_errors() != 0) {
        text::print("GPU: %u validation errors - FAILED\n", static_cast<unsigned>(validation_errors()));
    }
    return impl.refused_modules == 0 && impl.refused_pipelines == 0 && validation_errors() == 0;
}

} // namespace ff_check
