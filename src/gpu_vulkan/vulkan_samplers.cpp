// NOT ORIGINAL - the Vulkan Gpu's samplers (vulkan_samplers.h).
#include "gpu_vulkan/vulkan_samplers.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "gpu_vulkan/vulkan_formats.h"

namespace render::vulkan {

namespace {

// Direct3D 9's "no limit" for a level's detail: every level of the chain is used.
constexpr float kAllLevels = 1000.0F;

} // namespace

SamplerKey sampler_key(const GpuStateBlock& block, uint32_t stage)
{
    return {.address_u = block.sampler_state(stage, GpuSamplerState::kAddressU),
            .address_v = block.sampler_state(stage, GpuSamplerState::kAddressV),
            .address_w = block.sampler_state(stage, GpuSamplerState::kAddressW),
            .border_colour = block.sampler_state(stage, GpuSamplerState::kBorderColour),
            .mag_filter = block.sampler_state(stage, GpuSamplerState::kMagFilter),
            .min_filter = block.sampler_state(stage, GpuSamplerState::kMinFilter),
            .mip_filter = block.sampler_state(stage, GpuSamplerState::kMipFilter),
            .mip_lod_bias = block.sampler_state(stage, GpuSamplerState::kMipLodBias),
            .max_mip_level = block.sampler_state(stage, GpuSamplerState::kMaxMipLevel),
            .max_anisotropy = block.sampler_state(stage, GpuSamplerState::kMaxAnisotropy)};
}

VkSamplerCreateInfo sampler_info(const SamplerKey& key, const SamplerLimits& limits)
{
    const bool anisotropic = limits.anisotropy && (key.min_filter == gpu_value::kFilterAnisotropic ||
                                                   key.mag_filter == gpu_value::kFilterAnisotropic);
    const auto most_detailed = static_cast<float>(key.max_mip_level);
    // MIPFILTER NONE: the most detailed level alone (Direct3D 9 doesn't use the chain)
    const float least_detailed = key.mip_filter == gpu_value::kFilterNone ? most_detailed : kAllLevels;
    const bool border = key.address_u == gpu_value::kAddressBorder || key.address_v == gpu_value::kAddressBorder ||
                        key.address_w == gpu_value::kAddressBorder;
    return VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = filter_of(key.mag_filter),
        .minFilter = filter_of(key.min_filter),
        .mipmapMode = mip_mode_of(key.mip_filter),
        .addressModeU = address_of(key.address_u),
        .addressModeV = address_of(key.address_v),
        .addressModeW = address_of(key.address_w),
        .mipLodBias = std::bit_cast<float>(key.mip_lod_bias),
        .anisotropyEnable = anisotropic ? VK_TRUE : VK_FALSE,
        .maxAnisotropy =
            anisotropic ? std::clamp(static_cast<float>(key.max_anisotropy), 1.0F, limits.max_anisotropy) : 1.0F,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = most_detailed,
        .maxLod = least_detailed,
        .borderColor = border ? border_of(key.border_colour) : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
}

VkSampler SamplerCache::sampler(const SamplerKey& key)
{
    const auto found = samplers_.find(key);
    if (found != samplers_.end()) {
        return found->second;
    }
    const VkSamplerCreateInfo info = sampler_info(key, limits_);
    VkSampler made = VK_NULL_HANDLE;
    if (vkCreateSampler(device_, &info, nullptr, &made) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    samplers_.emplace(key, made);
    return made;
}

void SamplerCache::clear()
{
    for (const auto& [key, sampler] : samplers_) {
        vkDestroySampler(device_, sampler, nullptr);
    }
    samplers_.clear();
}

} // namespace render::vulkan
