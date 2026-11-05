#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - the Vulkan Gpu's memory (vulkan_memory.cpp; docs/design/vulkan.md, V6): images and buffers allocated
// through the Vulkan Memory Allocator, and the transfers that fill them and read them back. A transfer is recorded into
// one command buffer kept for the purpose, submitted and waited for at once (an upload is finished when the call
// returns, as a Direct3D 9 unlock is); the frame's own command buffers (V7) are not used for it.
namespace render::vulkan {

class Context;

// An image in the device's memory: a texture's, a render target's or a depth surface's, with its views.
struct Image {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE; // the sampled view (the X formats' alpha read as 1), or the depth view
    VkImageView attachment_view = VK_NULL_HANDLE; // level 0, no swizzle: the view drawn into (attachments only)
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    Extent extent;
    uint32_t levels = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;  // every level's (they move together)
    VkImageLayout resting = VK_IMAGE_LAYOUT_UNDEFINED; // where it is left between operations
};

// What an image is made from.
struct ImageRequest {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    Extent extent;
    uint32_t levels = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0;
    bool alpha_one = false; // the sampled view reads alpha as 1
};

// A buffer in the device's memory; `mapped` when it is in memory the processor writes (kept mapped).
struct DeviceBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
    VkDeviceSize bytes = 0;
};

// Where a buffer lives.
enum class BufferPlace : uint8_t {
    kDevice,   // device memory, filled by transfers (static vertex and index buffers)
    kUpload,   // memory the processor writes and the device reads (dynamic buffers, the ring, staging)
    kReadBack, // memory the device writes and the processor reads (read-backs)
};

// A region of an image level for a copy or a blit: its corners (x, y; z is 0 to 1).
struct ImageArea {
    uint32_t level = 0;
    std::array<VkOffset3D, 2> corners{};
};

class Memory {
public:
    Memory() = default;
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
    Memory(Memory&&) = delete;
    Memory& operator=(Memory&&) = delete;
    ~Memory() = default;

    // The allocator, the transfer command buffer and its fence for the context's device; false when one failed.
    bool init(const Context& context);
    // Everything released (every image and buffer must be destroyed first).
    void shutdown();
    [[nodiscard]] bool ready() const { return allocator_ != nullptr; }

    bool create_image(const ImageRequest& request, Image* image);
    void destroy_image(Image* image);
    bool create_buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, BufferPlace place, DeviceBuffer* buffer);
    void destroy_buffer(DeviceBuffer* buffer);
    // The processor's writes into a mapped buffer made visible to the device (a no-op in coherent memory).
    void flush(const DeviceBuffer& buffer, VkDeviceSize offset, VkDeviceSize bytes);

    // A level's texels (tightly packed, the image's format) copied in / out (read: as many bytes as `texels` holds).
    bool upload_image_level(Image* image, uint32_t level, std::span<const uint8_t> texels);
    bool read_image_level(Image* image, uint32_t level, std::vector<uint8_t>* texels);
    // Bytes copied into a device buffer at `offset`, and read back from it.
    bool upload_buffer(const DeviceBuffer& buffer, VkDeviceSize offset, std::span<const uint8_t> bytes);
    bool read_buffer(const DeviceBuffer& buffer, VkDeviceSize offset, VkDeviceSize bytes, std::vector<uint8_t>* out);
    // An area of one image copied, scaled, into an area of another (vkCmdBlitImage), or a multisampled image resolved
    // into a single-sampled one of its size (vkCmdResolveImage).
    bool blit(Image* source, const ImageArea& from, Image* target, const ImageArea& to, VkFilter filter);
    bool resolve(Image* source, Image* target);

    // Commands recorded and run at once on the queue (waited for).
    bool submit_now(const std::function<void(VkCommandBuffer)>& record);
    // An image moved to `layout` inside recorded commands (every level).
    static void transition(VkCommandBuffer commands, Image* image, VkImageLayout layout);

    [[nodiscard]] VkDevice device() const { return device_; }
    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
    // Bytes and allocations live now (VMA's statistics), for the checks.
    [[nodiscard]] VmaTotalStatistics statistics() const;

private:
    VkDevice device_ = nullptr;
    VkQueue queue_ = nullptr;
    VmaAllocator allocator_ = nullptr;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commands_ = nullptr;
    VkFence done_ = VK_NULL_HANDLE;
};

} // namespace render::vulkan
