#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <volk.h>

#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/glsl_compiler.h"

// NOT ORIGINAL - the Vulkan Gpu's pipelines (vulkan_pipelines.cpp; docs/design/vulkan.md, V7): one per key - the
// fixed-function emulation's shaders (ff_key.h), the vertex format they read, the primitive's class, the fill mode, the
// blend and the colour write mask, and the attachments' formats and samples - made the first time a draw needs it and
// kept. What changes per draw without a new pipeline is dynamic state: the viewport and scissor, culling, the depth test
// and writes and their comparison, the depth bias, the blend factor, the primitive within its class and the vertex stride.
//
// The shaders are generated (ff_shaders.h) and compiled by glslang at run time; the SPIR-V is kept on disk by the hash
// of its GLSL (thug2-decomp-vulkan-shaders.bin), and the driver's pipeline cache beside it
// (thug2-decomp-vulkan-pipelines.bin), so a second run of a level makes its pipelines from both.
namespace render::vulkan {

struct PipelineKey {
    ShaderKey shaders{};
    uint32_t colour_format = 0; // VkFormat
    uint32_t depth_format = 0;  // VkFormat; VK_FORMAT_UNDEFINED: no depth attachment
    uint8_t samples = 1;        // VkSampleCountFlagBits
    uint8_t topology_class = 2; // 0 points, 1 lines, 2 triangles
    uint8_t wireframe = 0;
    uint8_t blend = 0;          // D3DRS_ALPHABLENDENABLE
    uint8_t source_blend = 0;   // D3DBLEND
    uint8_t dest_blend = 0;
    uint8_t blend_operation = 0; // D3DBLENDOP
    uint8_t write_mask = 0;      // D3DRS_COLORWRITEENABLE (its bits are Vulkan's)

    bool operator==(const PipelineKey&) const = default;
};
static_assert(std::has_unique_object_representations_v<PipelineKey>, "PipelineKey is hashed as bytes");

struct PipelineKeyHash {
    std::size_t operator()(const PipelineKey& key) const;
};

// Counts and times, for the log.
struct PipelineStats {
    std::size_t pipelines = 0;
    std::size_t shaders_compiled = 0; // by glslang this run
    std::size_t shaders_from_disk = 0;
    std::size_t refused = 0;
    double compile_seconds = 0.0;  // glslang
    double pipeline_seconds = 0.0; // vkCreateGraphicsPipelines
};

class Pipelines {
public:
    Pipelines() = default;
    Pipelines(const Pipelines&) = delete;
    Pipelines& operator=(const Pipelines&) = delete;
    Pipelines(Pipelines&&) = delete;
    Pipelines& operator=(Pipelines&&) = delete;
    ~Pipelines() = default;

    // The set layout (push descriptors: the two uniform blocks and the stages' textures), the pipeline layout, and the
    // caches read from the files in `folder` (ending in a separator; empty: none kept).
    bool init(VkDevice device, VkPhysicalDevice physical, const std::string& folder);
    // The caches written and everything destroyed (the device idle).
    void shutdown();
    // The caches written to disk now (when something new was made since the last time).
    void save();

    [[nodiscard]] VkPipelineLayout layout() const { return layout_; }
    // The key's pipeline (made the first time); VK_NULL_HANDLE when its shaders don't compile or the driver refuses it
    // (reported once through `error`).
    VkPipeline pipeline(const PipelineKey& key, std::string* error);
    [[nodiscard]] const PipelineStats& stats() const { return stats_; }
    // A full-screen pass's pipeline (made the first time): a triangle covering the target from three vertices without
    // a vertex buffer, `fragment` (GLSL using this layout's bindings; its input v_uv runs 0 - 1 over the target) for each
    // pixel, no blending and no depth, into one single-sampled target of `colour_format`; the viewport and scissor
    // dynamic. VK_NULL_HANDLE when it doesn't compile or the driver refuses it (reported through `error`).
    VkPipeline screen_pass(VkFormat colour_format, const std::string& fragment, std::string* error);

private:
    VkShaderModule module(bool vertex, const ShaderKey& key, std::string* error);
    // GLSL's module: its SPIR-V from the disk cache or compiled; VK_NULL_HANDLE when it doesn't compile.
    VkShaderModule spirv_module(ShaderStage stage, const std::string& source, std::string* error);
    void load(const std::string& folder);

    VkDevice device_ = nullptr;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
    std::string folder_;
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines_;
    std::unordered_map<VertexKey, VkShaderModule, VertexKeyHash> vertex_modules_;
    std::unordered_map<FragmentKey, VkShaderModule, FragmentKeyHash> fragment_modules_;
    std::unordered_map<uint64_t, VkShaderModule> screen_modules_; // by the GLSL's hash
    std::unordered_map<uint64_t, VkPipeline> screen_pipelines_;   // by the fragment's hash and the format
    std::unordered_map<uint64_t, std::vector<uint32_t>> spirv_; // by the GLSL's hash
    bool dirty_ = false;
    PipelineStats stats_;
};

} // namespace render::vulkan
