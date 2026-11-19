// NOT ORIGINAL - (round PF) every pass's time on the device, from the passes' marks (vulkan_pass_profile.h).
#include "gpu_vulkan/vulkan_pass_profile.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu_markers.h"
#include "gpu_vulkan/vulkan_context.h"
#include "platform/frame_profile.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr double kNanosecondsPerMillisecond = 1.0e6;
constexpr uint32_t kPhaseShift = 16;
constexpr double kShownMs = 0.001; // (a listener's call that records nothing is left out of the report)

uint32_t key_of(gpu_markers::Phase phase, uint16_t id)
{
    return (static_cast<uint32_t>(phase) << kPhaseShift) | id;
}

} // namespace

void PassProfile::init(const Context& context, uint32_t slots)
{
    if (!frame_profile::on() || pool_ != VK_NULL_HANDLE) {
        return;
    }
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context.physical_device(), &families, nullptr);
    std::vector<VkQueueFamilyProperties> properties(families);
    vkGetPhysicalDeviceQueueFamilyProperties(context.physical_device(), &families, properties.data());
    VkPhysicalDeviceProperties device{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &device);
    if (context.queue_family() >= families || !(device.limits.timestampPeriod > 0.0F)) {
        return;
    }
    const uint32_t bits = core::at(properties, context.queue_family()).timestampValidBits;
    if (bits == 0) {
        return;
    }
    constexpr uint32_t kAllBits = 64;
    valid_mask_ = bits >= kAllBits ? ~0ULL : ((1ULL << bits) - 1);
    tick_ms_ = static_cast<double>(device.limits.timestampPeriod) / kNanosecondsPerMillisecond;
    const VkQueryPoolCreateInfo pool{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = slots * kStampsPerSlot,
                                     .pipelineStatistics = 0};
    if (vkCreateQueryPool(context.device(), &pool, nullptr, &pool_) != VK_SUCCESS) {
        pool_ = VK_NULL_HANDLE;
        return;
    }
    device_ = context.device();
    slots_.assign(slots, SlotStamps{});
    get_log("vulkan: frame_profile - every pass timed on the device (a line a pass every %u frames: \"gpu pass: ...\")",
            kReportFrames);
}

void PassProfile::shutdown(VkDevice device)
{
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    slots_.clear();
    sums_.clear();
    device_ = nullptr;
}

void PassProfile::stream_begun(VkCommandBuffer commands, uint32_t slot)
{
    if (pool_ == VK_NULL_HANDLE) {
        return;
    }
    SlotStamps& own = core::at(slots_, slot);
    own.stamps.clear();
    vkCmdResetQueryPool(commands, pool_, slot * kStampsPerSlot, kStampsPerSlot);
    own.reset = true;
}

void PassProfile::mark(VkCommandBuffer commands, uint32_t slot, const gpu_markers::Mark& mark)
{
    if (pool_ == VK_NULL_HANDLE) {
        return;
    }
    SlotStamps& own = core::at(slots_, slot);
    if (!own.reset) {
        return;
    }
    if (own.stamps.size() >= kStampsPerSlot) {
        ++dropped_;
        return;
    }
    const auto query = (slot * kStampsPerSlot) + static_cast<uint32_t>(own.stamps.size());
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, query);
    own.stamps.push_back({.phase = mark.phase, .id = mark.id, .end = mark.end});
}

void PassProfile::read(uint32_t slot, uint64_t frame)
{
    if (pool_ == VK_NULL_HANDLE) {
        return;
    }
    SlotStamps& own = core::at(slots_, slot);
    if (!own.reset || own.stamps.empty()) {
        own.reset = false;
        return;
    }
    own.reset = false;
    std::vector<uint64_t> ticks(own.stamps.size());
    if (vkGetQueryPoolResults(device_, pool_, slot * kStampsPerSlot, static_cast<uint32_t>(ticks.size()),
                              ticks.size() * sizeof(uint64_t), ticks.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        own.stamps.clear();
        return;
    }
    // The marks paired as a stack: a pass's own time is its time less its inner passes'.
    struct Open {
        uint32_t key = 0;
        uint64_t start = 0;
        double inner_ms = 0.0;
    };
    std::vector<Open> open;
    for (std::size_t i = 0; i < own.stamps.size(); ++i) {
        const Stamp& stamp = core::at(own.stamps, i);
        const uint64_t tick = core::at(ticks, i) & valid_mask_;
        const uint32_t key = key_of(stamp.phase, stamp.id);
        if (!stamp.end) {
            open.push_back({.key = key, .start = tick, .inner_ms = 0.0});
            continue;
        }
        const auto match = std::find_if(open.rbegin(), open.rend(), [key](const Open& each) { return each.key == key; });
        if (match == open.rend()) {
            continue; // (its beginning in an earlier submission - a read-back finished the frame part-way)
        }
        open.erase(match.base(), open.end()); // (inner passes never ended: dropped)
        const Open begun = open.back();
        open.pop_back();
        const double inclusive = tick >= begun.start ? static_cast<double>(tick - begun.start) * tick_ms_ : 0.0;
        Sum& sum = sums_[key];
        sum.inclusive_ms += inclusive;
        sum.own_ms += std::max(0.0, inclusive - begun.inner_ms);
        ++sum.calls;
        if (!open.empty()) {
            open.back().inner_ms += inclusive;
        }
    }
    own.stamps.clear();
    if (frame != last_frame_) {
        last_frame_ = frame;
        ++frames_;
    }
    if (frames_ >= kReportFrames) {
        report();
    }
}

void PassProfile::report()
{
    std::vector<std::pair<uint32_t, Sum>> passes(sums_.begin(), sums_.end());
    std::ranges::sort(passes, [](const auto& a, const auto& b) { return a.second.own_ms > b.second.own_ms; });
    const double frames = frames_;
    get_log("gpu passes: %u frames timed (own time, largest first; under %.3f ms left out; %u marks dropped)", frames_,
            kShownMs, dropped_);
    for (const auto& [key, sum] : passes) {
        if (sum.inclusive_ms / frames < kShownMs) {
            continue;
        }
        const auto phase = static_cast<gpu_markers::Phase>(key >> kPhaseShift);
        const auto id = static_cast<uint16_t>(key & 0xFFFFU);
        get_log("gpu pass: %s %.3f ms (own %.3f) a frame, %.2f calls a frame", gpu_markers::name(phase, id).c_str(),
                sum.inclusive_ms / frames, sum.own_ms / frames, static_cast<double>(sum.calls) / frames);
    }
    sums_.clear();
    frames_ = 0;
    dropped_ = 0;
}

} // namespace render::vulkan
