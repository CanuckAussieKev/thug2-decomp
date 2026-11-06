#pragma once

#include <cstdint>
#include <map>
#include <volk.h>

#include "engine/gfx/gpu.h"

// NOT ORIGINAL - the Vulkan Gpu's samplers (vulkan_samplers.cpp; docs/design/vulkan.md, V6): Direct3D 9 sets a stage's
// filters, addressing, border colour, level of detail bias, most detailed level and anisotropy one state at a time; a
// Vulkan sampler is all of them at once, made once per combination and kept.
namespace render::vulkan {

// A sampler's states, as Direct3D 9's values (the state block's for one stage).
struct SamplerKey {
    uint32_t address_u = gpu_value::kAddressWrap;
    uint32_t address_v = gpu_value::kAddressWrap;
    uint32_t address_w = gpu_value::kAddressWrap;
    uint32_t border_colour = 0;
    uint32_t mag_filter = gpu_value::kFilterPoint;
    uint32_t min_filter = gpu_value::kFilterPoint;
    uint32_t mip_filter = gpu_value::kFilterNone;
    uint32_t mip_lod_bias = 0;  // a float's bits
    uint32_t max_mip_level = 0; // the most detailed level used
    uint32_t max_anisotropy = 1;

    auto operator<=>(const SamplerKey&) const = default;
};

// Stage `stage`'s sampler states in the block.
SamplerKey sampler_key(const GpuStateBlock& block, uint32_t stage);

// What the device allows (its features and limits).
struct SamplerLimits {
    bool anisotropy = false; // samplerAnisotropy is on
    float max_anisotropy = 1.0F;
};

// The samplers made so far, by key.
class SamplerCache {
public:
    SamplerCache() = default;
    SamplerCache(const SamplerCache&) = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;
    SamplerCache(SamplerCache&&) = delete;
    SamplerCache& operator=(SamplerCache&&) = delete;
    ~SamplerCache() = default;

    void init(VkDevice device, const SamplerLimits& limits)
    {
        device_ = device;
        limits_ = limits;
    }
    // The sampler for `key` (made the first time); VK_NULL_HANDLE when the device refused it.
    VkSampler sampler(const SamplerKey& key);
    [[nodiscard]] std::size_t size() const { return samplers_.size(); }
    // Every sampler destroyed.
    void clear();

private:
    VkDevice device_ = nullptr;
    SamplerLimits limits_;
    std::map<SamplerKey, VkSampler> samplers_;
};

// The sampler's create info for a key (what SamplerCache makes; for the checks).
VkSamplerCreateInfo sampler_info(const SamplerKey& key, const SamplerLimits& limits);

} // namespace render::vulkan
