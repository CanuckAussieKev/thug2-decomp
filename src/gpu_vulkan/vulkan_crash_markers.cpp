// NOT ORIGINAL - GPU crash diagnostics (vulkan_crash_markers.h): the markers, the driver's fault report, the heaps.
#include "gpu_vulkan/vulkan_crash_markers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/gpu_markers.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_memory.h"

namespace render::vulkan {

namespace {

constexpr uint32_t kEndBit = 1U;
constexpr uint32_t kIdShift = 1;
constexpr uint32_t kIdMask = 0x3FFU; // 10 bits: a ViewOrder up to 1023
constexpr uint32_t kPhaseShift = 11;
constexpr uint32_t kPhaseMask = 0x7U;
constexpr uint32_t kSerialShift = 14;
constexpr uint64_t kSerialSpan = 1ULL << (32U - kSerialShift); // the submissions a marker's number tells apart
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr std::size_t kPassesPerLine = 8; // (the lost frame's passes: a line of at most this many)

// A pass's key: its marker with no submission and no end.
uint32_t pass_key(uint32_t marker) { return marker & ((1U << kSerialShift) - 1U) & ~kEndBit; }

const char* stage_name(VkPipelineStageFlags stage)
{
    switch (stage) {
    case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
        return "the top of the pipe (begun)";
    case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
        return "the bottom of the pipe (finished)";
    default:
        return nullptr;
    }
}

const char* address_type_name(VkDeviceFaultAddressTypeEXT type)
{
    switch (type) {
    case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:
        return "no address";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
        return "an invalid read";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
        return "an invalid write";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
        return "an invalid execution";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
        return "an instruction (the fault's own unknown)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
        return "an instruction that faulted on an invalid access";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
        return "an instruction that caused a fault";
    default:
        return "an unknown kind of address";
    }
}

// A fixed-size text field of Vulkan's (up to its first 0).
std::string field_text(std::span<const char> chars) { return {chars.begin(), std::ranges::find(chars, '\0')}; }

} // namespace

uint32_t encode_marker(const gpu_markers::Mark& mark, uint64_t serial)
{
    return (static_cast<uint32_t>(serial % kSerialSpan) << kSerialShift) |
           ((static_cast<uint32_t>(mark.phase) & kPhaseMask) << kPhaseShift) |
           ((static_cast<uint32_t>(mark.id) & kIdMask) << kIdShift) | (mark.end ? kEndBit : 0U);
}

gpu_markers::Mark decode_marker(uint32_t value)
{
    return {.phase = static_cast<gpu_markers::Phase>((value >> kPhaseShift) & kPhaseMask),
            .id = static_cast<uint16_t>((value >> kIdShift) & kIdMask),
            .end = (value & kEndBit) != 0};
}

uint64_t marker_serial(uint32_t value, uint64_t near)
{
    const uint64_t low = value >> kSerialShift;
    uint64_t serial = near - (near % kSerialSpan) + low;
    if (serial > near && serial >= kSerialSpan) {
        serial -= kSerialSpan;
    }
    return serial;
}

std::string marker_text(uint32_t value, uint64_t near)
{
    const gpu_markers::Mark mark = decode_marker(value);
    return text::format("submission %llu: the %s of %s", static_cast<unsigned long long>(marker_serial(value, near)),
                        mark.end ? "end" : "beginning", gpu_markers::name(mark.phase, mark.id).c_str());
}

std::vector<std::string> describe_reached(uint32_t queue, std::span<const ReachedMarker> reached, uint64_t near)
{
    std::vector<std::string> lines;
    const ReachedMarker* top = nullptr;
    const ReachedMarker* bottom = nullptr;
    for (const ReachedMarker& each : reached) {
        const char* const stage = stage_name(each.stage);
        if (each.marker == 0) {
            lines.push_back(
                text::format("queue %u: %s - no marker reached", queue, stage != nullptr ? stage : "a stage"));
            continue;
        }
        lines.push_back(
            stage != nullptr
                ? text::format("queue %u: %s reached %s", queue, stage, marker_text(each.marker, near).c_str())
                : text::format("queue %u: stage 0x%x reached %s", queue, static_cast<unsigned>(each.stage),
                               marker_text(each.marker, near).c_str()));
        if (each.stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) {
            top = &each;
        } else if (each.stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) {
            bottom = &each;
        }
    }
    if (top == nullptr || top->marker == 0) {
        return lines;
    }
    const gpu_markers::Mark began = decode_marker(top->marker);
    const std::string where = gpu_markers::name(began.phase, began.id);
    const auto serial = static_cast<unsigned long long>(marker_serial(top->marker, near));
    if (!began.end) {
        lines.push_back(text::format("queue %u: the device was in %s (submission %llu): begun, not finished", queue,
                                     where.c_str(), serial));
    } else if (bottom != nullptr && bottom->marker == top->marker) {
        lines.push_back(text::format("queue %u: everything marked had finished - the device was in the unmarked work "
                                     "after %s (submission %llu)",
                                     queue, where.c_str(), serial));
    } else {
        lines.push_back(text::format("queue %u: the device had reached %s's end (submission %llu) with earlier work "
                                     "still running - from the last finished marker above to there",
                                     queue, where.c_str(), serial));
    }
    return lines;
}

std::vector<std::string> describe_passes(std::span<const uint32_t> begun)
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t on_line = 0;
    gpu_markers::Phase phase = gpu_markers::Phase::kGpu;
    for (std::size_t at = 0; at < begun.size();) {
        const uint32_t key = core::at(begun, at);
        std::size_t repeats = 1;
        while (at + repeats < begun.size() && core::at(begun, at + repeats) == key) {
            ++repeats;
        }
        at += repeats;
        const gpu_markers::Mark mark = decode_marker(key);
        if (line.empty() || mark.phase != phase || on_line == kPassesPerLine) {
            if (!line.empty()) {
                lines.push_back(line);
            }
            line = text::format("%s: ", gpu_markers::phase_name(mark.phase));
            phase = mark.phase;
            on_line = 0;
        } else {
            line += ", ";
        }
        line += gpu_markers::pass_name(mark.phase, mark.id);
        if (repeats > 1) {
            line += text::format(" x%u", static_cast<unsigned>(repeats));
        }
        ++on_line;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

FaultReport read_fault(VkDevice device, bool vendor_binary)
{
    FaultReport report;
    if (vkGetDeviceFaultInfoEXT == nullptr) {
        report.result = VK_ERROR_EXTENSION_NOT_PRESENT;
        return report;
    }
    VkDeviceFaultCountsEXT counts{};
    counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    report.result = vkGetDeviceFaultInfoEXT(device, &counts, nullptr);
    if (report.result != VK_SUCCESS && report.result != VK_INCOMPLETE) {
        return report;
    }
    if (!vendor_binary) {
        counts.vendorBinarySize = 0;
    }
    report.addresses.resize(counts.addressInfoCount);
    report.vendor.resize(counts.vendorInfoCount);
    report.vendor_binary.resize(static_cast<std::size_t>(counts.vendorBinarySize));
    VkDeviceFaultInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
    info.pAddressInfos = report.addresses.empty() ? nullptr : report.addresses.data();
    info.pVendorInfos = report.vendor.empty() ? nullptr : report.vendor.data();
    info.pVendorBinaryData = report.vendor_binary.empty() ? nullptr : report.vendor_binary.data();
    report.result = vkGetDeviceFaultInfoEXT(device, &counts, &info);
    report.description = field_text(info.description);
    report.addresses.resize(std::min<std::size_t>(report.addresses.size(), counts.addressInfoCount));
    report.vendor.resize(std::min<std::size_t>(report.vendor.size(), counts.vendorInfoCount));
    report.vendor_binary.resize(
        std::min<std::size_t>(report.vendor_binary.size(), static_cast<std::size_t>(counts.vendorBinarySize)));
    return report;
}

std::vector<std::string> describe_fault(const FaultReport& report, AddressNamer name_address, const void* user)
{
    std::vector<std::string> lines;
    if (report.result != VK_SUCCESS && report.result != VK_INCOMPLETE) {
        lines.push_back(
            text::format("the driver's fault report could not be read (VkResult %d)", static_cast<int>(report.result)));
        return lines;
    }
    lines.push_back(text::format("the driver says: \"%s\"%s", report.description.c_str(),
                                 report.result == VK_INCOMPLETE ? " (its report cut short)" : ""));
    if (report.addresses.empty()) {
        lines.emplace_back("no faulting address reported");
    }
    for (const VkDeviceFaultAddressInfoEXT& address : report.addresses) {
        // (the address is known to its precision, a power of two: the range it lies in)
        const VkDeviceAddress precision = std::max<VkDeviceAddress>(address.addressPrecision, 1);
        const VkDeviceAddress low = address.reportedAddress & ~(precision - 1);
        std::string line = text::format("address 0x%llx (%s; within 0x%llx - 0x%llx)",
                                        static_cast<unsigned long long>(address.reportedAddress),
                                        address_type_name(address.addressType), static_cast<unsigned long long>(low),
                                        static_cast<unsigned long long>(low + precision - 1));
        const std::string held = name_address != nullptr ? name_address(user, address.reportedAddress) : std::string();
        line += held.empty() ? std::string(" - in no live buffer with a device address (an image, or memory freed)")
                             : " - " + held;
        lines.push_back(line);
    }
    for (const VkDeviceFaultVendorInfoEXT& vendor : report.vendor) {
        lines.push_back(text::format("vendor: \"%s\" (code 0x%llx, data 0x%llx)",
                                     field_text(vendor.description).c_str(),
                                     static_cast<unsigned long long>(vendor.vendorFaultCode),
                                     static_cast<unsigned long long>(vendor.vendorFaultData)));
    }
    if (!report.vendor_binary.empty()) {
        lines.push_back(
            text::format("the vendor's binary dump: %u bytes", static_cast<unsigned>(report.vendor_binary.size())));
    }
    return lines;
}

std::vector<std::string> describe_memory(VkPhysicalDevice physical, bool budget)
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgets{};
    budgets.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    properties.pNext = budget ? &budgets : nullptr;
    vkGetPhysicalDeviceMemoryProperties2(physical, &properties);
    std::vector<std::string> lines;
    const VkPhysicalDeviceMemoryProperties& memory = properties.memoryProperties;
    const std::span<const VkMemoryHeap> heaps(std::data(memory.memoryHeaps),
                                              std::min<std::size_t>(memory.memoryHeapCount, VK_MAX_MEMORY_HEAPS));
    for (std::size_t heap = 0; heap < heaps.size(); ++heap) {
        const VkMemoryHeap& each = core::at(heaps, heap);
        const bool local = (each.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        const double size = static_cast<double>(each.size) / kBytesPerMegabyte;
        if (budget) {
            const double used = static_cast<double>(core::at(std::span(budgets.heapUsage), heap)) / kBytesPerMegabyte;
            const double allowed =
                static_cast<double>(core::at(std::span(budgets.heapBudget), heap)) / kBytesPerMegabyte;
            lines.push_back(text::format("heap %u (%s, %.0f MB): %.0f MB used of a %.0f MB budget",
                                         static_cast<unsigned>(heap), local ? "the device's" : "the system's", size,
                                         used, allowed));
        } else {
            lines.push_back(text::format("heap %u (%s): %.0f MB (no budget: VK_EXT_memory_budget not offered)",
                                         static_cast<unsigned>(heap), local ? "the device's" : "the system's", size));
        }
    }
    return lines;
}

bool CrashMarkers::init(const Context& context, Memory* memory)
{
    const CrashDiagnostics& has = context.crash_diagnostics();
    checkpoints_ = has.checkpoints;
    memory_ = memory;
    for (Kept& kept : kept_) {
        kept = Kept{};
    }
    if (has.buffer_markers && !checkpoints_) {
        constexpr VkDeviceSize kBytes = sizeof(uint32_t) * 2;
        if (!memory->create_buffer(kBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack, &buffer_) ||
            buffer_.mapped == nullptr) {
            memory->destroy_buffer(&buffer_);
            return false;
        }
        std::memset(buffer_.mapped, 0, kBytes);
        memory->flush(buffer_, 0, kBytes);
    }
    return true;
}

void CrashMarkers::shutdown()
{
    if (memory_ != nullptr && buffer_.buffer != VK_NULL_HANDLE) {
        memory_->destroy_buffer(&buffer_);
    }
    checkpoints_ = false;
    memory_ = nullptr;
}

void CrashMarkers::mark(VkCommandBuffer commands, const gpu_markers::Mark& mark, uint64_t serial)
{
    if (!on() || commands == nullptr) {
        return;
    }
    const uint32_t value = encode_marker(mark, serial);
    if (checkpoints_) {
        vkCmdSetCheckpointNV(commands, std::bit_cast<void*>(static_cast<uintptr_t>(value)));
    } else {
        vkCmdWriteBufferMarkerAMD(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, buffer_.buffer, 0, value);
        vkCmdWriteBufferMarkerAMD(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, buffer_.buffer, sizeof(uint32_t),
                                  value);
    }
    if (mark.end) {
        return;
    }
    Kept& kept = core::at(kept_, static_cast<std::size_t>(serial % kKept));
    if (kept.serial != serial) {
        kept.serial = serial;
        kept.begun.clear(); // (its room kept: no allocation once the lists have grown)
    }
    kept.begun.push_back(pass_key(value));
}

std::vector<uint32_t> CrashMarkers::passes_of(uint64_t serial) const
{
    const Kept& kept = core::at(kept_, static_cast<std::size_t>(serial % kKept));
    return kept.serial == serial ? kept.begun : std::vector<uint32_t>{};
}

std::array<uint32_t, 2> CrashMarkers::buffer_values() const
{
    std::array<uint32_t, 2> values{};
    if (buffer_.buffer == VK_NULL_HANDLE || buffer_.mapped == nullptr) {
        return values;
    }
    memory_->invalidate(buffer_);
    std::memcpy(values.data(), buffer_.mapped, sizeof(values));
    return values;
}

std::vector<std::string> CrashMarkers::reached(std::span<const VkQueue> queues, uint64_t near) const
{
    std::vector<std::string> lines;
    if (checkpoints_) {
        for (std::size_t index = 0; index < queues.size(); ++index) {
            uint32_t count = 0;
            vkGetQueueCheckpointDataNV(core::at(queues, index), &count, nullptr);
            VkCheckpointDataNV blank{};
            blank.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
            std::vector<VkCheckpointDataNV> data(count, blank);
            vkGetQueueCheckpointDataNV(core::at(queues, index), &count, data.data());
            data.resize(std::min<std::size_t>(data.size(), count));
            std::vector<ReachedMarker> reached;
            reached.reserve(data.size());
            for (const VkCheckpointDataNV& each : data) {
                reached.push_back({.stage = static_cast<VkPipelineStageFlags>(each.stage),
                                   .marker = static_cast<uint32_t>(std::bit_cast<uintptr_t>(each.pCheckpointMarker))});
            }
            if (reached.empty()) {
                if (index == 0) {
                    lines.emplace_back("queue 0: no checkpoint reached (none recorded yet)");
                }
                continue; // (a library's queue that recorded no marker)
            }
            const std::vector<std::string> described = describe_reached(static_cast<uint32_t>(index), reached, near);
            lines.insert(lines.end(), described.begin(), described.end());
        }
    } else if (buffer_.buffer != VK_NULL_HANDLE) {
        const std::array<uint32_t, 2> values = buffer_values();
        const std::array<ReachedMarker, 2> reached{
            ReachedMarker{.stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .marker = std::get<0>(values)},
            ReachedMarker{.stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, .marker = std::get<1>(values)}};
        lines = describe_reached(0, reached, near);
    } else {
        lines.emplace_back("no markers recorded (the device has neither NVIDIA's checkpoints nor AMD's buffer markers, "
                           "or [debug] gpu_crash_markers=0)");
    }
    return lines;
}

} // namespace render::vulkan
