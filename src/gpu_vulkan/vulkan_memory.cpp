// NOT ORIGINAL - the Vulkan Gpu's memory: images and buffers through VMA, and their transfers (vulkan_memory.h).
#include "gpu_vulkan/vulkan_memory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kNoTimeout = std::numeric_limits<uint64_t>::max();

// The layout an image is kept in between operations: sampled ones ready to be read by shaders, attachments ready to be
// drawn into, anything else ready to be written by a transfer.
VkImageLayout resting_layout(const ImageRequest& request)
{
    const bool depth = (request.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    if ((request.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) {
        return depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if ((request.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if ((request.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

// What a layout's image is used for: the stages, their accesses, and the writes among them (what a later use waits for).
struct LayoutUse {
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags access = 0;
    VkAccessFlags writes = 0;
};
LayoutUse use_of(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return {};
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return {.stages = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .writes = VK_ACCESS_TRANSFER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return {.stages = VK_PIPELINE_STAGE_TRANSFER_BIT, .access = VK_ACCESS_TRANSFER_READ_BIT, .writes = 0};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return {.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT, .writes = 0};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return {.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                .writes = 0};
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return {.stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return {.stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .writes = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return {.stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, .access = 0, .writes = 0};
    default:
        return {.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .writes = VK_ACCESS_MEMORY_WRITE_BIT};
    }
}

VkImageSubresourceLayers level_layers(const Image& image, uint32_t level)
{
    return {.aspectMask = image.aspect, .mipLevel = level, .baseArrayLayer = 0, .layerCount = 1};
}

// A level's whole size as a copy's extent.
VkExtent3D level_size(const Image& image, uint32_t level)
{
    const Extent extent = level_extent(image.extent, level);
    return {.width = extent.width, .height = extent.height, .depth = 1};
}

// A mapped allocation's bytes.
std::span<uint8_t> mapped_bytes(const DeviceBuffer& buffer)
{
    return {static_cast<uint8_t*>(buffer.mapped), static_cast<std::size_t>(buffer.bytes)};
}

VkImageView make_view(VkDevice device, const Image& image, const VkImageSubresourceRange& range,
                      const VkComponentMapping& components)
{
    const VkImageViewCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image.format,
        .components = components,
        .subresourceRange = range,
    };
    VkImageView view = VK_NULL_HANDLE;
    return vkCreateImageView(device, &info, nullptr, &view) == VK_SUCCESS ? view : VK_NULL_HANDLE;
}

} // namespace

bool Memory::init(const Context& context)
{
    device_ = context.device();
    queue_ = context.queue();
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = context.physical_device();
    info.device = context.device();
    info.instance = context.instance();
    info.vulkanApiVersion = context.api_version();
    info.pVulkanFunctions = &functions;
    if (context.ray_tracing_enabled()) { // NOT ORIGINAL - acceleration structures and their scratch need addresses
        info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }
    if (vmaCreateAllocator(&info, &allocator_) != VK_SUCCESS) {
        allocator_ = nullptr;
        return false;
    }
    const VkCommandPoolCreateInfo pool{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context.queue_family(),
    };
    if (vkCreateCommandPool(device_, &pool, nullptr, &pool_) != VK_SUCCESS) {
        return false;
    }
    const VkCommandBufferAllocateInfo allocate{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device_, &allocate, &commands_) != VK_SUCCESS) {
        return false;
    }
    const VkFenceCreateInfo fence{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    return vkCreateFence(device_, &fence, nullptr, &done_) == VK_SUCCESS;
}

void Memory::shutdown()
{
    if (device_ != nullptr) {
        vkDeviceWaitIdle(device_);
        if (done_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, done_, nullptr);
        }
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, pool_, nullptr); // (frees its command buffer)
        }
    }
    if (allocator_ != nullptr) {
        vmaDestroyAllocator(allocator_);
    }
    done_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    commands_ = nullptr;
    allocator_ = nullptr;
    queue_ = nullptr;
    device_ = nullptr;
}

VmaTotalStatistics Memory::statistics() const
{
    VmaTotalStatistics stats{};
    if (allocator_ != nullptr) {
        vmaCalculateStatistics(allocator_, &stats);
    }
    return stats;
}

bool Memory::submit_now(const std::function<void(VkCommandBuffer)>& record)
{
    if (vkResetCommandBuffer(commands_, 0) != VK_SUCCESS) {
        return false;
    }
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (vkBeginCommandBuffer(commands_, &begin) != VK_SUCCESS) {
        return false;
    }
    record(commands_);
    if (vkEndCommandBuffer(commands_) != VK_SUCCESS) {
        return false;
    }
    const VkSubmitInfo submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commands_,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    if (vkResetFences(device_, 1, &done_) != VK_SUCCESS || vkQueueSubmit(queue_, 1, &submit, done_) != VK_SUCCESS) {
        return false;
    }
    return vkWaitForFences(device_, 1, &done_, VK_TRUE, kNoTimeout) == VK_SUCCESS;
}

void Memory::transition(VkCommandBuffer commands, Image* image, VkImageLayout layout, bool always)
{
    if (image->layout == layout && !always) {
        return;
    }
    const LayoutUse before = use_of(image->layout);
    const LayoutUse after = use_of(layout);
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = before.writes,
        .dstAccessMask = after.access,
        .oldLayout = image->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->image,
        .subresourceRange = {.aspectMask = image->aspect,
                             .baseMipLevel = 0,
                             .levelCount = image->levels,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, before.stages, after.stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    image->layout = layout;
}

bool Memory::create_image(const ImageRequest& request, Image* image)
{
    *image = Image{};
    const VkImageCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = request.format,
        .extent = {.width = request.extent.width, .height = request.extent.height, .depth = 1},
        .mipLevels = request.levels,
        .arrayLayers = 1,
        .samples = request.samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = request.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &info, &allocation, &image->image, &image->allocation, nullptr) != VK_SUCCESS) {
        *image = Image{};
        return false;
    }
    image->format = request.format;
    image->aspect = request.aspect;
    image->extent = request.extent;
    image->levels = request.levels;
    image->samples = request.samples;
    image->usage = request.usage;
    image->resting = resting_layout(request);
    const bool depth = (request.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    // (a depth / stencil image is sampled through its depth alone)
    const VkImageSubresourceRange all_levels{.aspectMask =
                                                 depth ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT} : request.aspect,
                                             .baseMipLevel = 0,
                                             .levelCount = request.levels,
                                             .baseArrayLayer = 0,
                                             .layerCount = 1};
    const VkComponentMapping sampled{.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                     .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                     .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                     .a = request.alpha_one ? VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_IDENTITY};
    const VkComponentMapping identity{};
    if ((request.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) {
        image->view = make_view(device_, *image, all_levels, sampled);
    }
    if ((request.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0) {
        const VkImageSubresourceRange top{
            .aspectMask = request.aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
        image->attachment_view = make_view(device_, *image, top, identity);
    }
    const bool views_made =
        ((request.usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || image->view != VK_NULL_HANDLE) &&
        ((request.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) == 0 ||
         image->attachment_view != VK_NULL_HANDLE);
    if (!views_made) {
        destroy_image(image);
        return false;
    }
    return true; // (undefined until its first use: the caller records the move to its resting layout)
}

void Memory::destroy_image(Image* image)
{
    if (image->view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image->view, nullptr);
    }
    if (image->attachment_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image->attachment_view, nullptr);
    }
    if (image->image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image->image, image->allocation);
    }
    *image = Image{};
}

bool Memory::create_buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, BufferPlace place, DeviceBuffer* buffer)
{
    *buffer = DeviceBuffer{};
    const VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = std::max<VkDeviceSize>(bytes, 1),
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    VmaAllocationCreateInfo allocation{};
    switch (place) {
    case BufferPlace::kDevice:
        allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case BufferPlace::kUpload:
        allocation.usage = VMA_MEMORY_USAGE_AUTO;
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case BufferPlace::kReadBack:
        allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
    VmaAllocationInfo made{};
    if (vmaCreateBuffer(allocator_, &info, &allocation, &buffer->buffer, &buffer->allocation, &made) != VK_SUCCESS) {
        *buffer = DeviceBuffer{};
        return false;
    }
    buffer->mapped = made.pMappedData;
    buffer->bytes = bytes;
    return true;
}

void Memory::destroy_buffer(DeviceBuffer* buffer)
{
    if (buffer->buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer->buffer, buffer->allocation);
    }
    *buffer = DeviceBuffer{};
}

void Memory::flush(const DeviceBuffer& buffer, VkDeviceSize offset, VkDeviceSize bytes)
{
    vmaFlushAllocation(allocator_, buffer.allocation, offset, bytes);
}

void Memory::record_upload_level(VkCommandBuffer commands, Image* image, uint32_t level, const Staged& from)
{
    transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkBufferImageCopy region{.bufferOffset = from.offset,
                                   .bufferRowLength = 0,
                                   .bufferImageHeight = 0,
                                   .imageSubresource = level_layers(*image, level),
                                   .imageOffset = {.x = 0, .y = 0, .z = 0},
                                   .imageExtent = level_size(*image, level)};
    vkCmdCopyBufferToImage(commands, from.buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    transition(commands, image, image->resting);
}

bool Memory::read_image_level(Image* image, uint32_t level, std::vector<uint8_t>* texels)
{
    DeviceBuffer read_back;
    if (!create_buffer(texels->size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack, &read_back)) {
        return false;
    }
    const bool done = submit_now([&](VkCommandBuffer commands) {
        transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy region{.bufferOffset = 0,
                                       .bufferRowLength = 0,
                                       .bufferImageHeight = 0,
                                       .imageSubresource = level_layers(*image, level),
                                       .imageOffset = {.x = 0, .y = 0, .z = 0},
                                       .imageExtent = level_size(*image, level)};
        vkCmdCopyImageToBuffer(commands, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read_back.buffer, 1,
                               &region);
        transition(commands, image, image->resting);
    });
    if (done) {
        vmaInvalidateAllocation(allocator_, read_back.allocation, 0, read_back.bytes);
        const std::span<const uint8_t> from = mapped_bytes(read_back);
        texels->assign(from.begin(), from.end());
    }
    destroy_buffer(&read_back);
    return done;
}

bool Memory::read_buffer(const DeviceBuffer& buffer, VkDeviceSize offset, VkDeviceSize bytes, std::vector<uint8_t>* out)
{
    DeviceBuffer read_back;
    if (!create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack, &read_back)) {
        return false;
    }
    const bool done = submit_now([&](VkCommandBuffer commands) {
        const VkBufferCopy region{.srcOffset = offset, .dstOffset = 0, .size = bytes};
        vkCmdCopyBuffer(commands, buffer.buffer, read_back.buffer, 1, &region);
    });
    if (done) {
        vmaInvalidateAllocation(allocator_, read_back.allocation, 0, read_back.bytes);
        const std::span<const uint8_t> from = mapped_bytes(read_back);
        out->assign(from.begin(), from.end());
    }
    destroy_buffer(&read_back);
    return done;
}

void Memory::record_blit(VkCommandBuffer commands, Image* source, const ImageArea& from, Image* target,
                         const ImageArea& to, VkFilter filter)
{
    transition(commands, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(commands, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageBlit region{.srcSubresource = level_layers(*source, from.level),
                             .srcOffsets = {from.corners.front(), from.corners.back()},
                             .dstSubresource = level_layers(*target, to.level),
                             .dstOffsets = {to.corners.front(), to.corners.back()}};
    vkCmdBlitImage(commands, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);
    transition(commands, source, source->resting);
    transition(commands, target, target->resting);
}

void Memory::record_resolve(VkCommandBuffer commands, Image* source, Image* target)
{
    transition(commands, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(commands, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageResolve region{.srcSubresource = level_layers(*source, 0),
                                .srcOffset = {.x = 0, .y = 0, .z = 0},
                                .dstSubresource = level_layers(*target, 0),
                                .dstOffset = {.x = 0, .y = 0, .z = 0},
                                .extent = level_size(*source, 0)};
    vkCmdResolveImage(commands, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    transition(commands, source, source->resting);
    transition(commands, target, target->resting);
}

} // namespace render::vulkan
