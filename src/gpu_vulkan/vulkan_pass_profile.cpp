// NOT ORIGINAL - (round PF) every pass's time on the device, from the passes' marks (vulkan_pass_profile.h).
#include "gpu_vulkan/vulkan_pass_profile.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
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

void PassProfile::read(const Finished& finished)
{
    const uint32_t slot = finished.slot;
    const uint64_t frame = finished.frame;
    if (pool_ == VK_NULL_HANDLE) {
        return;
    }
    SlotStamps& own = core::at(slots_, slot);
    if (!own.reset || own.stamps.empty()) {
        own.reset = false;
        return;
    }
    own.reset = false;
    if (frame != last_frame_) {
        frame_done();
    }
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
        std::size_t at = open.size();
        while (at > 0 && core::at(open, at - 1).key != key) {
            --at;
        }
        if (at == 0) {
            continue; // (its beginning in an earlier submission - a read-back finished the frame part-way)
        }
        const Open begun = core::at(open, at - 1);
        open.resize(at - 1); // (inner passes never ended: dropped)
        const double inclusive = tick >= begun.start ? static_cast<double>(tick - begun.start) * tick_ms_ : 0.0;
        Sum& sum = sums_[key];
        sum.inclusive_ms += inclusive;
        sum.own_ms += std::max(0.0, inclusive - begun.inner_ms);
        frame_own_[key] += std::max(0.0, inclusive - begun.inner_ms);
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

// (round PERF5) A frame's passes against their usual times: a frame whose passes took much longer than usual is logged
// with the passes that grew ("gpu slow frame: ...") - the averages every kReportFrames frames hide a burst of a few.
void PassProfile::frame_done()
{
    if (frame_own_.empty()) {
        return;
    }
    double total = 0.0;
    for (const auto& [key, ms] : frame_own_) {
        total += ms;
    }
    constexpr double kSlowShare = 1.3; // over the usual by this share and by kSlowMs
    constexpr double kSlowMs = 5.0;
    constexpr double kGrownMs = 0.25; // (a pass listed when it grew by this)
    constexpr double kFollow = 1.0 / 32.0;
    constexpr uint32_t kMostLogged = 200;
    constexpr std::size_t kMostListed = 6;
    constexpr uint32_t kSettleFrames = 60; // (the usual times followed this long first)
    if (followed_ >= kSettleFrames && total > usual_frame_ * kSlowShare && total > usual_frame_ + kSlowMs &&
        slow_logged_ < kMostLogged) {
        ++slow_logged_;
        std::vector<std::pair<double, uint32_t>> grown;
        for (const auto& [key, ms] : frame_own_) {
            const auto usual = usual_own_.find(key);
            const double was = usual != usual_own_.end() ? usual->second : 0.0;
            if (ms - was > kGrownMs) {
                grown.emplace_back(ms - was, key);
            }
        }
        std::ranges::sort(grown, [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string text;
        for (std::size_t i = 0; i < grown.size() && i < kMostListed; ++i) {
            const uint32_t key = core::at(grown, i).second;
            text += text::format("%s%s %.2f (usual %.2f)", i == 0 ? "" : ", ",
                                 gpu_markers::name(static_cast<gpu_markers::Phase>(key >> kPhaseShift),
                                                   static_cast<uint16_t>(key & 0xFFFFU))
                                     .c_str(),
                                 frame_own_[key], usual_own_[key]);
        }
        get_log("gpu slow frame %llu: %.2f ms of passes (usual %.2f) - grown: %s",
                static_cast<unsigned long long>(last_frame_), total, usual_frame_, text.empty() ? "-" : text.c_str());
    }
    ++followed_;
    usual_frame_ = usual_frame_ > 0.0 ? usual_frame_ + ((total - usual_frame_) * kFollow) : total;
    for (auto& [key, usual] : usual_own_) {
        const auto now = frame_own_.find(key);
        usual += ((now != frame_own_.end() ? now->second : 0.0) - usual) * kFollow;
    }
    for (const auto& [key, ms] : frame_own_) {
        if (!usual_own_.contains(key)) {
            usual_own_[key] = ms;
        }
    }
    frame_own_.clear();
}

// (round PERF3) The compiled shaders' statistics (VK_KHR_pipeline_executable_properties - the driver's own: registers,
// spills, instruction counts... as it names them), with frame_profile on.
namespace {
bool g_pipeline_statistics = false;
} // namespace

void set_pipeline_statistics(bool on) { g_pipeline_statistics = on; }

VkPipelineCreateFlags pipeline_statistics_flags()
{
    return g_pipeline_statistics ? VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : 0;
}

void log_pipeline_statistics(VkDevice device, VkPipeline pipeline, const std::string& source)
{
    if (!g_pipeline_statistics || pipeline == VK_NULL_HANDLE || vkGetPipelineExecutablePropertiesKHR == nullptr ||
        vkGetPipelineExecutableStatisticsKHR == nullptr) {
        return;
    }
    // (the pass: the source's first `#define PASS_...`, else its first line after the version)
    const std::size_t at = source.find("#define PASS_");
    const std::size_t from = at != std::string::npos ? at + 8 : std::min(source.find('\n') + 1, source.size());
    const std::string name = source.substr(from, std::min<std::size_t>(source.find('\n', from), source.size()) - from);
    const VkPipelineInfoKHR info{.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, .pNext = nullptr, .pipeline = pipeline};
    uint32_t executables = 0;
    if (vkGetPipelineExecutablePropertiesKHR(device, &info, &executables, nullptr) != VK_SUCCESS) {
        return;
    }
    for (uint32_t index = 0; index < executables; ++index) {
        const VkPipelineExecutableInfoKHR executable{.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
                                                     .pNext = nullptr,
                                                     .pipeline = pipeline,
                                                     .executableIndex = index};
        uint32_t count = 0;
        if (vkGetPipelineExecutableStatisticsKHR(device, &executable, &count, nullptr) != VK_SUCCESS) {
            continue;
        }
        std::vector<VkPipelineExecutableStatisticKHR> stats(count, VkPipelineExecutableStatisticKHR{
                                                                       .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
        if (vkGetPipelineExecutableStatisticsKHR(device, &executable, &count, stats.data()) != VK_SUCCESS) {
            continue;
        }
        std::string line;
        for (const VkPipelineExecutableStatisticKHR& stat : stats) {
            std::string value;
            switch (stat.format) {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: value = stat.value.b32 != 0 ? "yes" : "no"; break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: value = std::to_string(stat.value.i64); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: value = std::to_string(stat.value.u64); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: value = std::to_string(stat.value.f64); break;
            default: break;
            }
            line += std::string(line.empty() ? "" : ", ") + static_cast<const char*>(stat.name) + " " + value;
        }
        get_log("shader stats: %s: %s", name.c_str(), line.c_str());
    }
}
} // namespace render::vulkan
