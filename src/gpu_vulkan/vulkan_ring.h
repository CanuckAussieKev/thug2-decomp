#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <volk.h>

#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_memory.h"

// NOT ORIGINAL - the ring the draws from memory copy their vertices and indices into (Gpu::draw_from_memory /
// draw_indexed_from_memory, Direct3D 9's DrawPrimitiveUP / DrawIndexedPrimitiveUP; docs/design/vulkan.md, V6): per frame
// in flight a buffer the processor writes and the device reads, used from the start each frame. When a frame needs more
// than its buffer holds, a buffer twice the size takes its place and the old one is kept until that frame comes round
// again (the device may still read it).
namespace render::vulkan {

// Where a copy went: the buffer, the offset, and the bytes (the processor's view of them).
struct RingSpan {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    std::span<uint8_t> bytes;
};

class UploadRing {
public:
    static constexpr VkDeviceSize kFirstBytes = 1U << 20U; // each frame's buffer to begin with
    static constexpr VkBufferUsageFlags kDrawUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    UploadRing() = default;
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;
    UploadRing(UploadRing&&) = delete;
    UploadRing& operator=(UploadRing&&) = delete;
    ~UploadRing() = default;

    // The draws' ring (vertices, indices, uniform blocks), or with `usage` another kind (the uploads' staging).
    bool init(Memory* memory, VkBufferUsageFlags usage = kDrawUsage);
    void shutdown();
    // The next frame in flight's buffer, from its start (its earlier frame has been waited for).
    void begin_frame(uint32_t frame);
    // Room for `bytes` at a multiple of `alignment`, the bytes copied in; none when the memory ran out.
    std::optional<RingSpan> copy(std::span<const uint8_t> bytes, VkDeviceSize alignment);
    // The current frame's buffer and how much of it is used (for the checks).
    [[nodiscard]] VkDeviceSize used() const { return used_; }
    [[nodiscard]] VkDeviceSize capacity() const;

private:
    struct FrameBuffers {
        DeviceBuffer buffer;
        std::vector<DeviceBuffer> retired; // outgrown this frame: released when the frame comes round again
    };
    Memory* memory_ = nullptr;
    VkBufferUsageFlags usage_ = kDrawUsage;
    std::array<FrameBuffers, Context::kFramesInFlight> frames_{};
    uint32_t frame_ = 0;
    VkDeviceSize used_ = 0;
};

} // namespace render::vulkan
