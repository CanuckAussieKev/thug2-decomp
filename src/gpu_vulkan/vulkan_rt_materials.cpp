// NOT ORIGINAL - ray tracing's material table on Vulkan (vulkan_rt_materials.h).
#include "gpu_vulkan/vulkan_rt_materials.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint32_t kAddressMirror = 2;     // (gpu_value has wrap, clamp and border only)
constexpr uint32_t kAddressMirrorOnce = 5; // (read as mirror: Vulkan's mirror-clamp needs a feature not enabled)
constexpr VkDeviceSize kStagingAlignment = 16;
constexpr uint32_t kRowGap = 16; // changed rows this close together go up as one copy

// An address mode's place in the samplers' order: wrap, mirror, clamp, border.
uint32_t address_place(uint32_t d3d_address)
{
    switch (d3d_address) {
    case kAddressMirror:
    case kAddressMirrorOnce:
        return 1;
    case gpu_value::kAddressClamp:
        return 2;
    case gpu_value::kAddressBorder:
        return 3;
    case gpu_value::kAddressWrap:
    default:
        return 0;
    }
}

constexpr std::array<VkSamplerAddressMode, kMaterialAddressModes> kAddressModes{
    VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER};

// Whether a texture's image can sit in a slot: sampled, colour, never drawn into (its layout is always the resting one).
bool slot_worthy(const Texture& texture)
{
    const Image& image = texture.image;
    constexpr VkImageUsageFlags kDrawnInto =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return image.view != VK_NULL_HANDLE && image.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
           (image.usage & kDrawnInto) == 0 && image.resting == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// Barriers around the records' copies: the reads of earlier submissions before the copy, every later read after it.
void records_barrier(VkCommandBuffer commands, bool before)
{
    const VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = before ? VkAccessFlags{0} : VkAccessFlags{VK_ACCESS_TRANSFER_WRITE_BIT},
        .dstAccessMask = before ? VkAccessFlags{VK_ACCESS_TRANSFER_WRITE_BIT} : VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}};
    vkCmdPipelineBarrier(commands, before ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                         before ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
}

constexpr std::string_view kMaterialGlsl = R"(#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#ifndef THUG2_MATERIAL_SET
#define THUG2_MATERIAL_SET 1
#endif
struct Material {
    uint texture_slot;
    uint sampler_index;
    uint uv_set;
    uint blend;
    float alpha_ref;
    uint flags;
    vec4 colour;
    float reflectivity;
    uint environment_pass;
    uint reflection_class;
    uint reflection_mask;
    uint mask_texture[3];
    uint mask_sampler[3];
    uint mask_mode[3];
};
layout(set = THUG2_MATERIAL_SET, binding = 0, scalar) readonly buffer MaterialRecords { Material material_records[]; };
layout(set = THUG2_MATERIAL_SET, binding = 1) uniform sampler material_samplers[32];
layout(set = THUG2_MATERIAL_SET, binding = 2) uniform texture2D material_textures[];
const uint MATERIAL_ALPHA_TESTED = 2u;
const uint MATERIAL_TRANSPARENT = 64u;
Material material(uint id)
{
    if (id >= uint(material_records.length())) {
        return Material(0u, 0u, 0u, 0u, 0.0, 0u, vec4(1.0), 0.0, 0xFFFFFFFFu, 0u, 0u, uint[3](0u, 0u, 0u),
                        uint[3](0u, 0u, 0u), uint[3](0u, 0u, 0u));
    }
    return material_records[id];
}
vec4 material_texel_lod(uint id, vec2 uv, float lod)
{
    Material m = material(id);
    uint slot = m.texture_slot;
    uint chosen = min(m.sampler_index, 31u);
    return textureLod(sampler2D(material_textures[nonuniformEXT(slot)], material_samplers[nonuniformEXT(chosen)]),
                      uv, lod);
}
vec4 material_texel(uint id, vec2 uv)
{
    return material_texel_lod(id, uv, 0.0);
}
vec4 material_sample(uint id, vec2 uv)
{
    return material_texel_lod(id, uv, 0.0) * material(id).colour;
}
bool material_alpha_passes(uint id, float alpha)
{
    Material m = material(id);
    if ((m.flags & MATERIAL_ALPHA_TESTED) == 0u) {
        return true;
    }
    return uint(round(clamp(alpha, 0.0, 1.0) * 255.0)) >= uint(round(clamp(m.alpha_ref, 0.0, 1.0) * 255.0));
}
bool material_alpha_passes_at(uint id, vec2 uv)
{
    return material_alpha_passes(id, material_sample(id, uv).a);
}
float material_mask_alpha(uint id, uint k, vec2 uv, float lod)
{
    Material m = material(id);
    uint slot = m.mask_texture[k];
    uint chosen = min(m.mask_sampler[k], 31u);
    return textureLod(sampler2D(material_textures[nonuniformEXT(slot)], material_samplers[nonuniformEXT(chosen)]),
                      uv, lod).a;
}
)";

} // namespace

uint32_t material_sampler_index(const rt::Material& material)
{
    const uint32_t linear = material.filter >= gpu_value::kFilterLinear ? 1 : 0;
    return (linear * kMaterialAddressModes * kMaterialAddressModes) +
           (address_place(material.address_u) * kMaterialAddressModes) + address_place(material.address_v);
}

MaterialRecord material_record(const rt::Material& material, uint32_t slot,
                               const std::array<uint32_t, rt::Material::kMaskPasses>& mask_slots)
{
    MaterialRecord record{.texture = slot,
                          .sampler = material_sampler_index(material),
                          .uv_set = material.uv_set,
                          .blend = material.blend,
                          .alpha_ref = material.alpha_ref,
                          .flags = material.flags,
                          .colour = material.colour,
                          .reflectivity = material.reflectivity,
                          .environment_pass = material.environment_pass,
                          .reflection_class = material.reflection_class,
                          .reflection_mask = material.reflection_mask};
    for (std::size_t k = 0; k < rt::Material::kMaskPasses; ++k) {
        const rt::Material::MaskPass& mask = core::at(material.mask_passes, k);
        if (mask.kind == rt::kMaskNone) {
            continue;
        }
        // (filtered linearly, as the pass's own texture is)
        core::at(record.mask_texture, k) = core::at(mask_slots, k);
        core::at(record.mask_sampler, k) = (kMaterialAddressModes * kMaterialAddressModes) +
                                           (address_place(mask.address_u) * kMaterialAddressModes) +
                                           address_place(mask.address_v);
        core::at(record.mask_mode, k) = (mask.kind & 0xFU) | ((mask.uv_set & 0xFU) << kMaskUvSetShift) |
                                        (mask.texture_alpha ? kMaskTextureAlpha : 0U);
    }
    return record;
}

std::string_view material_glsl() { return kMaterialGlsl; }

uint32_t MaterialTable::device_slot_limit(VkPhysicalDevice device)
{
    VkPhysicalDeviceDescriptorIndexingProperties indexing{};
    indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &indexing;
    vkGetPhysicalDeviceProperties2(device, &properties);
    // (the records' buffer counts as one resource of the stage)
    const uint32_t resources = indexing.maxPerStageUpdateAfterBindResources > 1
                                   ? indexing.maxPerStageUpdateAfterBindResources - 1
                                   : 0;
    const uint32_t limit = std::min({indexing.maxDescriptorSetUpdateAfterBindSampledImages,
                                     indexing.maxPerStageDescriptorUpdateAfterBindSampledImages, resources});
    return std::min(limit > kReservedSampledImages ? limit - kReservedSampledImages : 0, kMostTextureSlots);
}

bool MaterialTable::init(Backend& state, const Options& options)
{
    const std::scoped_lock lock(mutex_);
    if (layout_ != VK_NULL_HANDLE || !state.context.ray_tracing_enabled() || !state.draws_made) {
        return false;
    }
    device_ = state.context.device();
    const uint32_t limit = device_slot_limit(state.context.physical_device());
    slot_count_ = std::min(options.texture_slots != 0 ? options.texture_slots : kDefaultTextureSlots, limit);
    first_slot_ = std::clamp<uint32_t>(options.first_slot, 1, slot_count_ > 1 ? slot_count_ - 1 : 1);
    next_slot_ = first_slot_;
    bool made = slot_count_ > 1;
    for (uint32_t index = 0; made && index < kMaterialSamplerCount; ++index) {
        const uint32_t per_filter = kMaterialAddressModes * kMaterialAddressModes;
        const bool linear = index >= per_filter;
        const VkFilter filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        const VkSamplerCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = filter,
            .minFilter = filter,
            .mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = core::at(kAddressModes, (index % per_filter) / kMaterialAddressModes),
            .addressModeV = core::at(kAddressModes, index % kMaterialAddressModes),
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias = 0.0F,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0F,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0F,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            .unnormalizedCoordinates = VK_FALSE};
        made = vkCreateSampler(device_, &info, nullptr, &core::at(samplers_, index)) == VK_SUCCESS;
    }
    if (made) {
        const std::array<VkDescriptorSetLayoutBinding, 3> bindings{
            VkDescriptorSetLayoutBinding{.binding = kMaterialRecordsBinding,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_ALL,
                                         .pImmutableSamplers = nullptr},
            VkDescriptorSetLayoutBinding{.binding = kMaterialSamplersBinding,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                         .descriptorCount = kMaterialSamplerCount,
                                         .stageFlags = VK_SHADER_STAGE_ALL,
                                         .pImmutableSamplers = nullptr},
            VkDescriptorSetLayoutBinding{.binding = kMaterialTexturesBinding,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                         .descriptorCount = slot_count_,
                                         .stageFlags = VK_SHADER_STAGE_ALL,
                                         .pImmutableSamplers = nullptr}};
        const std::array<VkDescriptorBindingFlags, 3> flags{
            0, 0,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT};
        const VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext = nullptr,
            .bindingCount = static_cast<uint32_t>(flags.size()),
            .pBindingFlags = flags.data()};
        const VkDescriptorSetLayoutCreateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = &flag_info,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
        made = vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout_) == VK_SUCCESS;
    }
    slot_textures_.assign(slot_count_, nullptr);
    slot_users_.assign(slot_count_, {});
    made = made && make_generation(state, std::max<uint32_t>(options.first_rows, 1), &current_);
    if (made) {
        // (slot 0: the white texture, in the first set for good)
        write_slots(state, current_.set, {0});
    }
    if (!made) {
        get_log("vulkan: the ray-tracing material table could not be made");
        destroy_generation(state, &current_);
        if (layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
        for (VkSampler& sampler : samplers_) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device_, sampler, nullptr);
                sampler = VK_NULL_HANDLE;
            }
        }
        return false;
    }
    get_log("vulkan: ray-tracing material table: %u texture slots (the device allows %u), %u rows to begin with",
            slot_count_, limit, current_.rows);
    return true;
}

void MaterialTable::shutdown(Backend& state)
{
    const std::scoped_lock lock(mutex_);
    if (layout_ == VK_NULL_HANDLE) {
        return;
    }
    for (RetiredGeneration& retired : retired_generations_) {
        destroy_generation(state, &retired.generation);
    }
    retired_generations_.clear();
    destroy_generation(state, &current_);
    vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
    for (VkSampler& sampler : samplers_) {
        vkDestroySampler(device_, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    records_.clear();
    record_textures_.clear();
    record_mask_textures_.clear();
    dirty_.clear();
    dirty_mark_.clear();
    slots_.clear();
    slot_textures_.clear();
    slot_users_.clear();
    free_slots_.clear();
    retired_slots_.clear();
    slots_to_write_.clear();
    slot_count_ = 0;
    device_ = nullptr;
}

bool MaterialTable::make_generation(Backend& state, uint32_t rows, Generation* made)
{
    const std::array<VkDescriptorPoolSize, 3> sizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = kMaterialSamplerCount},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = slot_count_}};
    const VkDescriptorPoolCreateInfo pool_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                               .maxSets = 1,
                                               .poolSizeCount = static_cast<uint32_t>(sizes.size()),
                                               .pPoolSizes = sizes.data()};
    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &made->pool) != VK_SUCCESS) {
        made->pool = VK_NULL_HANDLE;
        return false;
    }
    const VkDescriptorSetVariableDescriptorCountAllocateInfo count_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &slot_count_};
    const VkDescriptorSetAllocateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                               .pNext = &count_info,
                                               .descriptorPool = made->pool,
                                               .descriptorSetCount = 1,
                                               .pSetLayouts = &layout_};
    if (vkAllocateDescriptorSets(device_, &set_info, &made->set) != VK_SUCCESS) {
        made->set = VK_NULL_HANDLE;
        return false;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(rows) * sizeof(MaterialRecord);
    if (!state.memory.create_buffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    BufferPlace::kDevice, &made->buffer)) {
        return false;
    }
    made->rows = rows;
    const VkDescriptorBufferInfo buffer{.buffer = made->buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    std::array<VkDescriptorImageInfo, kMaterialSamplerCount> samplers{};
    for (uint32_t index = 0; index < kMaterialSamplerCount; ++index) {
        core::at(samplers, index) = {.sampler = core::at(samplers_, index),
                                     .imageView = VK_NULL_HANDLE,
                                     .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    }
    const std::array<VkWriteDescriptorSet, 2> writes{
        VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .pNext = nullptr,
                             .dstSet = made->set,
                             .dstBinding = kMaterialRecordsBinding,
                             .dstArrayElement = 0,
                             .descriptorCount = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .pImageInfo = nullptr,
                             .pBufferInfo = &buffer,
                             .pTexelBufferView = nullptr},
        VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .pNext = nullptr,
                             .dstSet = made->set,
                             .dstBinding = kMaterialSamplersBinding,
                             .dstArrayElement = 0,
                             .descriptorCount = kMaterialSamplerCount,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .pImageInfo = samplers.data(),
                             .pBufferInfo = nullptr,
                             .pTexelBufferView = nullptr}};
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    // (every row the buffer holds goes up before anything reads it: the rows not set read as white)
    std::vector<MaterialRecord> all(rows);
    std::copy_n(records_.begin(), std::min<std::size_t>(records_.size(), rows), all.begin());
    std::vector<uint8_t> raw(all.size() * sizeof(MaterialRecord));
    std::memcpy(raw.data(), all.data(), raw.size());
    const std::optional<RingSpan> staged = state.staging.copy(raw, kStagingAlignment);
    if (!staged) {
        return false;
    }
    VkCommandBuffer commands = state.frames.uploads();
    const VkBufferCopy region{.srcOffset = staged->offset, .dstOffset = 0, .size = raw.size()};
    vkCmdCopyBuffer(commands, staged->buffer, made->buffer.buffer, 1, &region);
    records_barrier(commands, false);
    return true;
}

void MaterialTable::destroy_generation(Backend& state, Generation* generation)
{
    if (generation->pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, generation->pool, nullptr); // (its set with it)
    }
    state.memory.destroy_buffer(&generation->buffer);
    *generation = Generation{};
}

void MaterialTable::write_slots(Backend& state, VkDescriptorSet set, const std::vector<uint32_t>& slots)
{
    if (slots.empty()) {
        return;
    }
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkWriteDescriptorSet> writes;
    images.reserve(slots.size());
    writes.reserve(slots.size());
    for (const uint32_t slot : slots) {
        const Texture* const texture = core::at(slot_textures_, slot);
        const bool shown = texture != nullptr && state.textures.contains(texture) && slot_worthy(*texture);
        const Image& image = shown ? texture->image : state.blank;
        images.push_back({.sampler = VK_NULL_HANDLE,
                          .imageView = image.view,
                          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        writes.push_back({.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                          .pNext = nullptr,
                          .dstSet = set,
                          .dstBinding = kMaterialTexturesBinding,
                          .dstArrayElement = slot,
                          .descriptorCount = 1,
                          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                          .pImageInfo = &images.back(),
                          .pBufferInfo = nullptr,
                          .pTexelBufferView = nullptr});
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

uint32_t MaterialTable::slot_for(Texture* texture)
{
    if (const auto found = slots_.find(texture); found != slots_.end()) {
        return found->second;
    }
    uint32_t slot = 0;
    if (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
    } else if (next_slot_ < slot_count_) {
        slot = next_slot_++;
    } else {
        if (!full_reported_) {
            full_reported_ = true;
            get_log("vulkan: the ray-tracing material table's %u texture slots are all used - more shown as white",
                    slot_count_);
        }
        return 0;
    }
    slots_.emplace(texture, slot);
    core::at(slot_textures_, slot) = texture;
    slots_to_write_.push_back(slot);
    return slot;
}

void MaterialTable::mark_dirty(uint32_t row)
{
    if (dirty_mark_.size() <= row) {
        dirty_mark_.resize(static_cast<std::size_t>(row) + 1, 0);
    }
    if (core::at(dirty_mark_, row) == 0) {
        core::at(dirty_mark_, row) = 1;
        dirty_.push_back(row);
    }
}

void MaterialTable::set(rt::MaterialId id, const rt::Material& material)
{
    const std::scoped_lock lock(mutex_);
    if (layout_ == VK_NULL_HANDLE || id == 0) {
        return;
    }
    if (records_.size() <= id) {
        records_.resize(static_cast<std::size_t>(id) + 1);
        record_textures_.resize(static_cast<std::size_t>(id) + 1, nullptr);
        record_mask_textures_.resize(static_cast<std::size_t>(id) + 1);
    }
    Texture* const texture = material.texture != nullptr ? object(material.texture) : nullptr;
    const uint32_t slot = texture != nullptr ? slot_for(texture) : 0;
    MaterialRecord& record = core::at(records_, id);
    const bool user_known = core::at(record_textures_, id) == texture && record.texture == slot;
    // (the mask passes' textures: slots of their own, their users noted as the first texture's)
    std::array<Texture*, rt::Material::kMaskPasses> masks{};
    std::array<uint32_t, rt::Material::kMaskPasses> mask_slots{};
    std::array<bool, rt::Material::kMaskPasses> mask_known{};
    for (std::size_t k = 0; k < rt::Material::kMaskPasses; ++k) {
        const rt::Material::MaskPass& mask = core::at(material.mask_passes, k);
        Texture* const mask_texture =
            mask.kind != rt::kMaskNone && mask.texture != nullptr ? object(mask.texture) : nullptr;
        core::at(masks, k) = mask_texture;
        core::at(mask_slots, k) = mask_texture != nullptr ? slot_for(mask_texture) : 0;
        core::at(mask_known, k) = core::at(core::at(record_mask_textures_, id), k) == mask_texture &&
                                  core::at(record.mask_texture, k) == core::at(mask_slots, k);
    }
    record = material_record(material, slot, mask_slots);
    core::at(record_textures_, id) = texture;
    core::at(record_mask_textures_, id) = masks;
    if (slot != 0 && !user_known) {
        core::at(slot_users_, slot).push_back(id);
    }
    for (std::size_t k = 0; k < rt::Material::kMaskPasses; ++k) {
        if (core::at(mask_slots, k) != 0 && !core::at(mask_known, k)) {
            core::at(slot_users_, core::at(mask_slots, k)).push_back(id);
        }
    }
    mark_dirty(id);
}

void MaterialTable::remove(rt::MaterialId id)
{
    const std::scoped_lock lock(mutex_);
    if (layout_ == VK_NULL_HANDLE || id == 0 || id >= records_.size()) {
        return;
    }
    core::at(records_, id) = MaterialRecord{};
    core::at(record_textures_, id) = nullptr;
    core::at(record_mask_textures_, id) = {};
    mark_dirty(id);
}

void MaterialTable::texture_released(Backend& state, Texture* texture)
{
    const std::scoped_lock lock(mutex_);
    const auto found = slots_.find(texture);
    if (found == slots_.end()) {
        return;
    }
    const uint32_t slot = found->second;
    slots_.erase(found);
    core::at(slot_textures_, slot) = nullptr;
    // (white at once: the image goes when the submissions that may read it have finished, the slot no sooner)
    write_slots(state, current_.set, {slot});
    std::erase(slots_to_write_, slot);
    for (const rt::MaterialId id : core::at(slot_users_, slot)) {
        if (core::at(record_textures_, id) == texture) {
            core::at(records_, id).texture = 0;
            core::at(record_textures_, id) = nullptr;
            mark_dirty(id);
        }
        for (std::size_t k = 0; k < rt::Material::kMaskPasses; ++k) {
            if (core::at(core::at(record_mask_textures_, id), k) == texture) {
                core::at(core::at(records_, id).mask_texture, k) = 0;
                core::at(core::at(record_mask_textures_, id), k) = nullptr;
                mark_dirty(id);
            }
        }
    }
    core::at(slot_users_, slot).clear();
    retired_slots_.push_back({.slot = slot, .serial = state.frames.serial()});
}

bool MaterialTable::upload_dirty_rows(Backend& state)
{
    // (one staging copy of every run, one copy command)
    std::ranges::sort(dirty_);
    std::vector<uint8_t> bytes;
    std::vector<VkBufferCopy> regions;
    std::size_t at = 0;
    while (at < dirty_.size()) {
        const uint32_t run_first = core::at(dirty_, at);
        uint32_t run_last = run_first;
        while (at + 1 < dirty_.size() && core::at(dirty_, at + 1) - run_last <= kRowGap) {
            run_last = core::at(dirty_, ++at);
        }
        ++at;
        regions.push_back({.srcOffset = bytes.size(),
                           .dstOffset = static_cast<VkDeviceSize>(run_first) * sizeof(MaterialRecord),
                           .size = static_cast<VkDeviceSize>(run_last - run_first + 1) * sizeof(MaterialRecord)});
        const std::span<const MaterialRecord> run(&core::at(records_, run_first), run_last - run_first + 1);
        const std::size_t end = bytes.size();
        bytes.resize(end + run.size_bytes());
        std::memcpy(&core::at(bytes, end), run.data(), run.size_bytes());
    }
    const std::optional<RingSpan> staged = state.staging.copy(bytes, kStagingAlignment);
    if (!staged) {
        return false;
    }
    for (VkBufferCopy& region : regions) {
        region.srcOffset += staged->offset;
    }
    VkCommandBuffer commands = state.frames.uploads();
    records_barrier(commands, true);
    vkCmdCopyBuffer(commands, staged->buffer, current_.buffer.buffer, static_cast<uint32_t>(regions.size()),
                    regions.data());
    records_barrier(commands, false);
    return true;
}

bool MaterialTable::make_visible(Backend& state)
{
    const std::scoped_lock lock(mutex_);
    if (layout_ == VK_NULL_HANDLE) {
        return true;
    }
    const uint64_t completed = state.frames.completed();
    std::erase_if(retired_generations_, [&](RetiredGeneration& retired) {
        if (retired.serial > completed) {
            return false;
        }
        destroy_generation(state, &retired.generation);
        return true;
    });
    std::erase_if(retired_slots_, [&](const RetiredSlot& retired) {
        if (retired.serial > completed) {
            return false;
        }
        free_slots_.push_back(retired.slot);
        return true;
    });
    bool ok = true;
    if (records_.size() > current_.rows) {
        // a bigger buffer and a set naming it: every slot written again, every row copied up
        Generation grown;
        const auto rows = static_cast<uint32_t>(std::max<std::size_t>(records_.size(), std::size_t{current_.rows} * 2));
        if (!make_generation(state, rows, &grown)) {
            destroy_generation(state, &grown);
            return false;
        }
        std::vector<uint32_t> slots{0};
        for (uint32_t slot = first_slot_; slot < next_slot_; ++slot) {
            slots.push_back(slot);
        }
        write_slots(state, grown.set, slots);
        if (current_.buffer.buffer != VK_NULL_HANDLE) {
            DeviceBuffer old_buffer = current_.buffer;
            current_.buffer = DeviceBuffer{};
            retire_buffer(state, &old_buffer);
        }
        retired_generations_.push_back({.generation = current_, .serial = state.frames.serial()});
        current_ = grown;
        slots_to_write_.clear();
        for (const uint32_t row : dirty_) {
            core::at(dirty_mark_, row) = 0;
        }
        dirty_.clear();
        return true;
    }
    std::ranges::sort(slots_to_write_);
    const auto repeated = std::ranges::unique(slots_to_write_);
    slots_to_write_.erase(repeated.begin(), repeated.end());
    write_slots(state, current_.set, slots_to_write_);
    slots_to_write_.clear();
    if (!dirty_.empty()) {
        ok = upload_dirty_rows(state);
        for (const uint32_t row : dirty_) {
            core::at(dirty_mark_, row) = 0;
        }
        dirty_.clear();
    }
    return ok;
}

uint32_t MaterialTable::slots_in_use() const
{
    const std::scoped_lock lock(mutex_);
    return static_cast<uint32_t>(slots_.size());
}

uint32_t MaterialTable::slot_of(GpuTexture* texture) const
{
    const std::scoped_lock lock(mutex_);
    const auto found = slots_.find(object(texture));
    return found != slots_.end() ? found->second : 0;
}

MaterialRecord MaterialTable::record(rt::MaterialId id) const
{
    const std::scoped_lock lock(mutex_);
    return id < records_.size() ? core::at(records_, id) : MaterialRecord{};
}

} // namespace render::vulkan
