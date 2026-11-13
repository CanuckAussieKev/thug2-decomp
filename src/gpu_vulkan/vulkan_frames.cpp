// NOT ORIGINAL - the Vulkan Gpu's frames: their command buffers, submissions and fences (vulkan_frames.h).
#include "gpu_vulkan/vulkan_frames.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "platform/frame_profile.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kNoTimeout = std::numeric_limits<uint64_t>::max();

bool begin(VkCommandBuffer commands)
{
    const VkCommandBufferBeginInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .pNext = nullptr,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                        .pInheritanceInfo = nullptr};
    return vkBeginCommandBuffer(commands, &info) == VK_SUCCESS;
}

// One global barrier: what `from` stages did before, before what `to` stages do after.
void memory_barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags writes, VkPipelineStageFlags to,
                    VkAccessFlags reads)
{
    const VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = writes, .dstAccessMask = reads};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// What reads vertex, index and uniform data.
constexpr VkPipelineStageFlags kBufferReaders = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags kBufferReads =
    VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;

} // namespace

bool Frames::init(const Context& context, Memory* memory, const Rings& rings)
{
    device_ = context.device();
    queue_ = context.queue();
    memory_ = memory;
    draws_ = rings.draws;
    staging_ = rings.staging;
    for (Slot& each : slots_) {
        const VkCommandPoolCreateInfo pool{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                           .queueFamilyIndex = context.queue_family()};
        if (vkCreateCommandPool(device_, &pool, nullptr, &each.pool) != VK_SUCCESS) {
            return false;
        }
        std::array<VkCommandBuffer, 2> buffers{};
        const VkCommandBufferAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                   .pNext = nullptr,
                                                   .commandPool = each.pool,
                                                   .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                   .commandBufferCount = static_cast<uint32_t>(buffers.size())};
        if (vkAllocateCommandBuffers(device_, &allocate, buffers.data()) != VK_SUCCESS) {
            return false;
        }
        each.uploads = buffers.front();
        each.stream = buffers.back();
        const VkFenceCreateInfo fence{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        if (vkCreateFence(device_, &fence, nullptr, &each.done) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphore, nullptr, &each.acquired) != VK_SUCCESS) {
            return false;
        }
    }
    current_ = 0;
    serial_ = 1;
    completed_ = 0;
    init_timing(context);
    return true;
}

void Frames::init_timing(const Context& context)
{
    if (!frame_profile::on()) {
        return;
    }
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.physical_device(), &families, nullptr);
    std::vector<VkQueueFamilyProperties> properties(families);
    vkGetPhysicalDeviceQueueFamilyProperties(context.physical_device(), &families, properties.data());
    VkPhysicalDeviceProperties device{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &device);
    if (context.queue_family() >= families || core::at(properties, context.queue_family()).timestampValidBits == 0 ||
        !(device.limits.timestampPeriod > 0.0F)) {
        return;
    }
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    tick_ms_ = static_cast<double>(device.limits.timestampPeriod) / kNanosecondsPerMillisecond;
    const VkQueryPoolCreateInfo pool{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kSlots * 2,
                                     .pipelineStatistics = 0};
    if (vkCreateQueryPool(device_, &pool, nullptr, &timing_) != VK_SUCCESS) {
        timing_ = VK_NULL_HANDLE;
    }
}

void Frames::read_timing(Slot& slot)
{
    if (!slot.timed) {
        return;
    }
    slot.timed = false;
    const auto index = static_cast<uint32_t>(&slot - slots_.data());
    std::array<uint64_t, 2> stamps{};
    if (vkGetQueryPoolResults(device_, timing_, index * 2, 2, sizeof(stamps), stamps.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
        std::get<1>(stamps) >= std::get<0>(stamps)) {
        static const int kGpu = frame_profile::slot("gpu");
        frame_profile::add_ms(kGpu, static_cast<double>(std::get<1>(stamps) - std::get<0>(stamps)) * tick_ms_);
    }
}

void Frames::shutdown()
{
    if (device_ == nullptr) {
        return;
    }
    vkDeviceWaitIdle(device_);
    for (Slot& each : slots_) {
        destroy(each.retired);
        if (each.acquired != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, each.acquired, nullptr);
        }
        if (each.done != VK_NULL_HANDLE) {
            vkDestroyFence(device_, each.done, nullptr);
        }
        if (each.pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, each.pool, nullptr); // (frees its command buffers)
        }
        each = Slot{};
    }
    if (timing_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_, timing_, nullptr);
        timing_ = VK_NULL_HANDLE;
    }
    device_ = nullptr;
    queue_ = nullptr;
}

Frames::Slot& Frames::slot() { return core::at(slots_, current_); }

const Frames::Slot& Frames::slot() const { return core::at(slots_, current_); }

bool Frames::stream_begun() const { return slot().stream_begun; }

VkSemaphore Frames::acquire_semaphore() const { return slot().acquired; }

void Frames::add_wait(const Wait& wait)
{
    waits_.push_back(wait.semaphore);
    wait_stages_.push_back(wait.stage);
}

void Frames::add_signal(VkSemaphore semaphore) { signals_.push_back(semaphore); }

VkCommandBuffer Frames::uploads()
{
    Slot& now = slot();
    if (!now.uploads_begun) {
        now.uploads_begun = begin(now.uploads);
        // the earlier submissions' reads of what these transfers overwrite, and their copies into the same buffers
        // (images have their own barriers)
        memory_barrier(now.uploads, kBufferReaders | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    return now.uploads;
}

VkCommandBuffer Frames::stream()
{
    Slot& now = slot();
    if (!now.stream_begun) {
        now.stream_begun = begin(now.stream);
        restarted_ = true;
        if (timing_ != VK_NULL_HANDLE && now.stream_begun && !now.timed) {
            const uint32_t first = current_ * 2;
            vkCmdResetQueryPool(now.stream, timing_, first, 2);
            vkCmdWriteTimestamp(now.stream, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timing_, first);
        }
    }
    return now.stream;
}

bool Frames::take_stream_restarted()
{
    const bool was = restarted_;
    restarted_ = false;
    return was;
}

void Frames::retire(const Image& image) { slot().retired.images.push_back(image); }

void Frames::retire(const DeviceBuffer& buffer) { slot().retired.buffers.push_back(buffer); }

void Frames::destroy(Retired& retired)
{
    for (Image& image : retired.images) {
        memory_->destroy_image(&image);
    }
    for (DeviceBuffer& buffer : retired.buffers) {
        memory_->destroy_buffer(&buffer);
    }
    retired.images.clear();
    retired.buffers.clear();
}

bool Frames::submit(VkSemaphore acquired, VkSemaphore presented)
{
    Slot& now = slot();
    std::array<VkCommandBuffer, 2> buffers{};
    uint32_t count = 0;
    if (now.uploads_begun) {
        // the transfers' writes before the stream's reads of the buffers (the images' moves back to their resting
        // layouts are their own barriers)
        memory_barrier(now.uploads, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kBufferReaders,
                       kBufferReads);
        if (vkEndCommandBuffer(now.uploads) != VK_SUCCESS) {
            return false;
        }
        core::at(buffers, count++) = now.uploads;
    }
    if (now.stream_begun || presented != VK_NULL_HANDLE || !signals_.empty()) {
        stream(); // (a present with nothing drawn still waits and signals)
        if (timing_ != VK_NULL_HANDLE && !now.timed) {
            vkCmdWriteTimestamp(now.stream, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_, (current_ * 2) + 1);
            now.timed = true;
        }
        if (vkEndCommandBuffer(now.stream) != VK_SUCCESS) {
            return false;
        }
        core::at(buffers, count++) = now.stream;
    }
    now.uploads_begun = false;
    now.stream_begun = false;
    // the present's acquire (at the transfer stage: its copy), then the UI windows' (add_wait)
    std::vector<VkSemaphore> waits;
    std::vector<VkPipelineStageFlags> wait_stages;
    if (acquired != VK_NULL_HANDLE) {
        waits.push_back(acquired);
        wait_stages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    waits.insert(waits.end(), waits_.begin(), waits_.end());
    wait_stages.insert(wait_stages.end(), wait_stages_.begin(), wait_stages_.end());
    std::vector<VkSemaphore> signals;
    if (presented != VK_NULL_HANDLE) {
        signals.push_back(presented);
    }
    signals.insert(signals.end(), signals_.begin(), signals_.end());
    waits_.clear();
    wait_stages_.clear();
    signals_.clear();
    const VkSubmitInfo info{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                            .pNext = nullptr,
                            .waitSemaphoreCount = static_cast<uint32_t>(waits.size()),
                            .pWaitSemaphores = waits.data(),
                            .pWaitDstStageMask = wait_stages.data(),
                            .commandBufferCount = count,
                            .pCommandBuffers = buffers.data(),
                            .signalSemaphoreCount = static_cast<uint32_t>(signals.size()),
                            .pSignalSemaphores = signals.data()};
    if (vkResetFences(device_, 1, &now.done) != VK_SUCCESS || vkQueueSubmit(queue_, 1, &info, now.done) != VK_SUCCESS) {
        return false;
    }
    now.pending = true;
    now.submitted = serial_;
    ++serial_;
    return true;
}

void Frames::recycle(Slot& slot)
{
    read_timing(slot);
    vkResetCommandPool(device_, slot.pool, 0);
    slot.pending = false;
}

bool Frames::finish()
{
    const bool submitted = submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
    const bool idle = vkQueueWaitIdle(queue_) == VK_SUCCESS;
    for (Slot& each : slots_) {
        if (each.pending) {
            vkWaitForFences(device_, 1, &each.done, VK_TRUE, kNoTimeout);
            recycle(each);
        }
        destroy(each.retired);
    }
    completed_ = serial_ - 1;
    draws_->begin_frame(current_);
    staging_->begin_frame(current_);
    return submitted && idle;
}

bool Frames::end_frame(VkSemaphore acquired, VkSemaphore presented)
{
    const bool submitted = submit(acquired, presented);
    current_ = (current_ + 1) % kSlots;
    Slot& next = slot();
    if (next.pending) {
        vkWaitForFences(device_, 1, &next.done, VK_TRUE, kNoTimeout);
        completed_ = std::max(completed_, next.submitted);
        recycle(next);
    }
    destroy(next.retired);
    draws_->begin_frame(current_);
    staging_->begin_frame(current_);
    return submitted;
}

} // namespace render::vulkan
