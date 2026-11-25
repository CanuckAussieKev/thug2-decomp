#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
// of its GLSL (thug2-decomp-vulkan-shaders.bin), and each pipeline's driver cache data beside it
// (thug2-decomp-vulkan-pipelines.bin), so a second run of a level makes its pipelines from both. Both files keep what
// the newest runs used, up to a size (a save drops the rest). The 64-bit build's files end in -x64 (an x86 driver's
// cache is never given to x64's).
namespace render::vulkan {

// (added by the upscaler) The motion attachment's format (vulkan_motion.h): the move in 0 - 1 of the viewport (x, y),
// how much of the pixel a blended draw left (z).
inline constexpr VkFormat kMotionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// (round RG) PipelineKey::motion of a blended draw that writes depth: the surface - its motion written, not its share
inline constexpr uint8_t kMotionSurface = 2;

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
    // (added by the upscaler) The pass has the motion attachment (kMotionFormat) as its second colour attachment: the
    // pipeline has two; with the key's motion on the fragment shader writes it - the motion where the draw doesn't
    // blend or is the surface (kMotionSurface: a blended draw that writes depth), else only its alpha into z (the
    // largest kept).
    uint8_t motion = 0;
    // (round RG) Drawn into Ray Reconstruction's transparency layer (vulkan_surface_light.h surface_light_layer): its
    // alpha blended as the layer's coverage - premultiplied "over" - not as the colour's
    uint8_t layer = 0;
    // (round DECAL - vulkan_surface_light.h, the game's layers laid) The pass's two attachments are the decals' inputs
    // (colour_format each - SurfaceInputs::decals / layer), no depth: each blended over the last with DBuffer's blend (src
    // x 1 + dst x src alpha; alpha dst x src alpha) - the key's own blend states unused.
    uint8_t dbuffer = 0;
    uint8_t spare = 0;

    bool operator==(const PipelineKey&) const = default;
};
static_assert(std::has_unique_object_representations_v<PipelineKey>, "PipelineKey is hashed as bytes");

struct PipelineKeyHash {
    std::size_t operator()(const PipelineKey& key) const;
};
// (round CPU) The pipelines' table's hash: the key's bytes eight at a time - every draw looks its pipeline up.
// (PipelineKeyHash, a byte at a time, stays the pipeline's identity in the pipeline cache's files.)
struct PipelineKeyTableHash {
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

// (round TRAIN) The draws' set 0 bindings (the scene's with ray queries) and set 1's (the view's light inputs): one
// definition for the pipelines' layout and the shader check's.
[[nodiscard]] std::vector<VkDescriptorSetLayoutBinding> draw_set_bindings(bool ray_queries);
[[nodiscard]] std::vector<VkDescriptorSetLayoutBinding> light_set_bindings();

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
    // (added by cel shading) `ray_queries`: the device has ray tracing - the layout gets the scene's binding
    // (kCelSceneBinding) and fragment shaders that trace (kSurfaceCelShadow) can be made.
    bool init(VkDevice device, VkPhysicalDevice physical, const std::string& folder, bool ray_queries = false);
    // The caches written and everything destroyed (the device idle).
    void shutdown();
    // The caches written to disk (when something new was made since the last time) - by a thread of their own, never
    // the drawing thread's time: one save at a time (one asked for meanwhile waits for the next call).
    void save();
    // The same on this thread, a save under way finished first (the end: shutdown).
    void save_now();

    [[nodiscard]] VkPipelineLayout layout() const { return layout_; }
    // (NOT ORIGINAL - round FL) set 1's layout: the view's light inputs (ff_uniforms.h kLightInputsSet), a descriptor set a
    // view - vulkan_surface_light.cpp allocates them
    [[nodiscard]] VkDescriptorSetLayout light_set_layout() const { return light_set_layout_; }
    // The key's pipeline (made the first time); VK_NULL_HANDLE when its shaders don't compile or the driver refuses it
    // (reported once through `error`).
    VkPipeline pipeline(const PipelineKey& key, std::string* error);
    [[nodiscard]] const PipelineStats& stats() const { return stats_; }
    // A full-screen pass's pipeline (made the first time): a triangle covering the target from three vertices without
    // a vertex buffer, `fragment` (GLSL using this layout's bindings; its input v_uv runs 0 - 1 over the target) for each
    // pixel, no blending and no depth, into one single-sampled target of `colour_format`; the viewport and scissor
    // dynamic. VK_NULL_HANDLE when it doesn't compile or the driver refuses it (reported through `error`).
    VkPipeline screen_pass(VkFormat colour_format, const std::string& fragment, std::string* error);
    // The UI's pipeline (made the first time; vulkan_ui_backend.cpp draws Dear ImGui's draw lists with it): triangles of
    // ImGui's vertices (a position and texture coordinates as two floats each, a colour as four bytes - red first; 20
    // bytes), each position mapped by the vertex uniform block's first vec4 (xy scale, zw offset) to the target, the
    // colour times t0 (binding kFirstTextureBinding), alpha-blended over the target as ImGui's Direct3D 9 backend does
    // (colour: source alpha / one minus it; alpha: one / one minus source alpha), no culling, no depth test or writes;
    // into the attachments of the open pass (`colour_format`, `depth_format` - VK_FORMAT_UNDEFINED: none - and
    // `samples`); the viewport and scissor dynamic. VK_NULL_HANDLE when it doesn't compile or the driver refuses it.
    VkPipeline ui_pass(VkFormat colour_format, VkFormat depth_format, uint32_t samples, std::string* error);
    // The UI's vertex (ImDrawVert's): its size, and where its texture coordinates and colour are.
    static constexpr uint32_t kUiVertexBytes = 20;
    static constexpr uint32_t kUiUvOffset = 8;
    static constexpr uint32_t kUiColourOffset = 16;

    // The caches kept between runs, each entry with the run (generation_) that last used it: a save keeps the newest
    // up to a size (kShaderCacheBytes / kPipelineCacheBytes), so they never grow without limit. The data is shared
    // (the saver's snapshot copies the pointers, not the megabytes).
    struct SpirvEntry {
        std::shared_ptr<const std::vector<uint32_t>> words; // (empty: it didn't compile)
        uint32_t used = 0;
    };
    // One pipeline's driver cache data (a cache of its own made from it, read back after the pipeline was made).
    struct PipelineBlob {
        std::shared_ptr<const std::vector<uint8_t>> data;
        uint32_t used = 0;
    };

private:
    VkShaderModule module(bool vertex, const ShaderKey& key, std::string* error);
    // GLSL's module: its SPIR-V from the disk cache or compiled; VK_NULL_HANDLE when it doesn't compile.
    VkShaderModule spirv_module(ShaderStage stage, const std::string& source, std::string* error, bool ray_query = false);
    // vkCreateGraphicsPipelines through the pipeline's own cache blob (`identity`: a hash of what the pipeline is made
    // of), timed; the caches marked to be saved unless the blob had it.
    bool make_graphics(const VkGraphicsPipelineCreateInfo& info, uint64_t identity, VkPipeline* made);
    void load(const std::string& folder);

    VkDevice device_ = nullptr;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout light_set_layout_ = VK_NULL_HANDLE; // (round FL)
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::string folder_;
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyTableHash> pipelines_;
    std::unordered_map<VertexKey, VkShaderModule, VertexKeyHash> vertex_modules_;
    std::unordered_map<FragmentKey, VkShaderModule, FragmentKeyHash> fragment_modules_;
    std::unordered_map<uint64_t, VkShaderModule> screen_modules_; // by the GLSL's hash
    std::unordered_map<uint64_t, VkPipeline> screen_pipelines_;   // by the fragment's hash and the format (and the UI's)
    std::unordered_map<uint64_t, SpirvEntry> spirv_;       // by the GLSL's hash
    std::unordered_map<uint64_t, PipelineBlob> blobs_;     // by the pipeline's identity
    uint32_t generation_ = 1;                              // this run's number (the files' last + 1)
    bool dirty_ = false;
    std::jthread saver_;             // (the last save's thread - joined when it goes)
    std::atomic<bool> saving_{false}; // (a save under way)
    bool ray_queries_ = false; // (added by cel shading)
    PipelineStats stats_;
};

} // namespace render::vulkan
