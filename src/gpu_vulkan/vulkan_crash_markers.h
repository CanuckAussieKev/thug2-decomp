#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu_markers.h"
#include "gpu_vulkan/vulkan_memory.h"

// NOT ORIGINAL - GPU crash diagnostics (round GD), as shipping Vulkan engines keep them: what to log when the device is
// lost (a driver reset after a hang - Windows' TDR -, a page fault), so the log says which pass and which resource.
//   - Markers: each pass's beginning and end (engine/gfx/gpu_markers.h - the Gpu's sections and every view listener's
//     call) recorded in the command buffer - NVIDIA's diagnostic checkpoints (VK_NV_device_diagnostic_checkpoints),
//     else AMD's buffer markers (VK_AMD_buffer_marker: the marker written into a buffer at the top and the bottom of the
//     pipe). After a loss each queue's last markers reached - at the top of the pipe (begun) and the bottom (finished) -
//     name the pass the device was in. A marker carries its submission's number (its low 18 bits) and the pass.
//   - The passes begun in the last few submissions, kept on the processor's side: the lost frame's list.
//   - The driver's own report (VK_EXT_device_fault): its description, the faulting addresses (each named by the buffer
//     holding it - Memory::describe_address), the vendor's codes, and its binary dump when the driver gives one.
//   - Each memory heap's budget and use (VK_EXT_memory_budget).
// The report itself (vulkan_device_lost.cpp) is written once, then the game quits: a lost device never comes back.
// Checked by src/tests/vulkan_crash_check.cpp.
namespace render::vulkan {

class Context;

// A marker's value: bit 0 the end of the pass, bits 1-10 its id, 11-13 its phase, 14-31 the submission's low bits.
[[nodiscard]] uint32_t encode_marker(const gpu_markers::Mark& mark, uint64_t serial);
[[nodiscard]] gpu_markers::Mark decode_marker(uint32_t value);
// The submission a marker was recorded in: the one nearest `near` (at most it) with the marker's low bits.
[[nodiscard]] uint64_t marker_serial(uint32_t value, uint64_t near);
// "submission 5361: the end of opaque_drawn: lamps (350)".
[[nodiscard]] std::string marker_text(uint32_t value, uint64_t near);

// A queue's markers reached, by pipeline stage (the top of the pipe: begun; the bottom: finished).
struct ReachedMarker {
    VkPipelineStageFlags stage = 0;
    uint32_t marker = 0;
};
// The lines for one queue's markers: each stage's, then which pass the device was in - begun, not finished.
[[nodiscard]] std::vector<std::string> describe_reached(uint32_t queue, std::span<const ReachedMarker> reached,
                                                        uint64_t near);

// The passes begun in one submission (their keys, in order) as lines: a line per run of one phase, repeats counted.
[[nodiscard]] std::vector<std::string> describe_passes(std::span<const uint32_t> begun);

// The driver's report of a lost device (VK_EXT_device_fault).
struct FaultReport {
    VkResult result = VK_SUCCESS; // vkGetDeviceFaultInfoEXT's
    std::string description;
    std::vector<VkDeviceFaultAddressInfoEXT> addresses;
    std::vector<VkDeviceFaultVendorInfoEXT> vendor;
    std::vector<uint8_t> vendor_binary;
};
[[nodiscard]] FaultReport read_fault(VkDevice device, bool vendor_binary);
// Its lines; `name_address` names what holds an address ("" when nothing known does - Memory::describe_address).
using AddressNamer = std::string (*)(const void* user, VkDeviceAddress address);
[[nodiscard]] std::vector<std::string> describe_fault(const FaultReport& report, AddressNamer name_address,
                                                      const void* user);

// Each memory heap's size and - with VK_EXT_memory_budget - its budget and use, as lines.
[[nodiscard]] std::vector<std::string> describe_memory(VkPhysicalDevice physical, bool budget);

// The markers recorded for a device (none when it has neither extension - Context::crash_diagnostics).
class CrashMarkers {
public:
    // The submissions whose passes are kept (the frames in flight and the one before).
    static constexpr std::size_t kKept = 4;

    bool init(const Context& context, Memory* memory);
    void shutdown();
    [[nodiscard]] bool on() const { return checkpoints_ || buffer_.buffer != VK_NULL_HANDLE; }
    [[nodiscard]] bool checkpoints() const { return checkpoints_; }

    // A mark recorded into `commands`, in submission `serial`.
    void mark(VkCommandBuffer commands, const gpu_markers::Mark& mark, uint64_t serial);
    // The passes begun in submission `serial` (empty when it isn't among the last kKept).
    [[nodiscard]] std::vector<uint32_t> passes_of(uint64_t serial) const;
    // The markers each queue reached (checkpoints), or the buffer's (buffer markers), as lines; `near`: the latest
    // submission with markers (their numbers are found near it); `completed`: the last one known finished (round GF:
    // when the driver gives no checkpoints, the lost work is said to be after it). Only once the device is lost: the
    // driver gives a queue's checkpoints then alone (vkGetQueueCheckpointDataNV's rule).
    [[nodiscard]] std::vector<std::string> reached(std::span<const VkQueue> queues, uint64_t near,
                                                   uint64_t completed) const;
    // The buffer markers' two values (top of the pipe, bottom) - for the check.
    [[nodiscard]] std::array<uint32_t, 2> buffer_values() const;

private:
    struct Kept {
        uint64_t serial = 0;
        std::vector<uint32_t> begun; // the passes' keys (encode_marker with no submission, no end bit)
    };
    bool checkpoints_ = false;
    Memory* memory_ = nullptr;
    DeviceBuffer buffer_; // (buffer markers) two 32-bit values
    std::array<Kept, kKept> kept_{};
};

} // namespace render::vulkan
