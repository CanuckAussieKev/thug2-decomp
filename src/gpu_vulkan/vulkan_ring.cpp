// NOT ORIGINAL - the draws' upload ring (vulkan_ring.h).
#include "gpu_vulkan/vulkan_ring.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_memory.h"

namespace render::vulkan {

bool UploadRing::init(Memory* memory, VkBufferUsageFlags usage)
{
    memory_ = memory;
    usage_ = usage;
    for (FrameBuffers& frame : frames_) {
        if (!memory_->create_buffer(kFirstBytes, usage_, BufferPlace::kUpload, &frame.buffer)) {
            return false;
        }
    }
    frame_ = 0;
    used_ = 0;
    return true;
}

void UploadRing::shutdown()
{
    if (memory_ == nullptr) {
        return;
    }
    for (FrameBuffers& frame : frames_) {
        memory_->destroy_buffer(&frame.buffer);
        for (DeviceBuffer& old : frame.retired) {
            memory_->destroy_buffer(&old);
        }
        frame.retired.clear();
    }
    memory_ = nullptr;
}

void UploadRing::begin_frame(uint32_t frame)
{
    frame_ = frame % static_cast<uint32_t>(frames_.size());
    FrameBuffers& buffers = core::at(frames_, frame_);
    for (DeviceBuffer& old : buffers.retired) {
        memory_->destroy_buffer(&old);
    }
    buffers.retired.clear();
    used_ = 0;
}

VkDeviceSize UploadRing::capacity() const { return core::at(frames_, frame_).buffer.bytes; }

std::optional<RingSpan> UploadRing::copy(std::span<const uint8_t> bytes, VkDeviceSize alignment)
{
    FrameBuffers& buffers = core::at(frames_, frame_);
    const VkDeviceSize step = std::max<VkDeviceSize>(alignment, 1);
    VkDeviceSize offset = ((used_ + step - 1) / step) * step; // (the next multiple of the alignment)
    if (offset + bytes.size() > buffers.buffer.bytes) {
        // outgrown: a buffer twice as big (or big enough) from its start; the old one kept until this frame comes round
        VkDeviceSize size = buffers.buffer.bytes * 2;
        while (size < bytes.size()) {
            size *= 2;
        }
        DeviceBuffer bigger;
        if (!memory_->create_buffer(size, usage_, BufferPlace::kUpload, &bigger)) {
            return std::nullopt;
        }
        buffers.retired.push_back(buffers.buffer);
        buffers.buffer = bigger;
        offset = 0;
    }
    const std::span<uint8_t> whole(static_cast<uint8_t*>(buffers.buffer.mapped),
                                   static_cast<std::size_t>(buffers.buffer.bytes));
    const std::span<uint8_t> into = whole.subspan(static_cast<std::size_t>(offset), bytes.size());
    std::ranges::copy(bytes, into.begin());
    memory_->flush(buffers.buffer, offset, bytes.size());
    used_ = offset + bytes.size();
    return RingSpan{.buffer = buffers.buffer.buffer, .offset = offset, .bytes = into};
}

} // namespace render::vulkan
