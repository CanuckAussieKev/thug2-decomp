// NOT ORIGINAL - (round GD) the Vulkan Gpu's lost device: its report and the game's end, and the passes' markers
// (vulkan_crash_markers.h, engine/gfx/gpu_markers.h).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "base/core/format.h"
#include "engine/config/settings_state.h"
#include "engine/file/disk.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu_markers.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_crash_markers.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

void log_lines(const char* heading, const std::vector<std::string>& lines)
{
    for (const std::string& line : lines) {
        get_log("vulkan: device lost: %s - %s", heading, line.c_str());
    }
}

// The vendor's binary dump (VK_EXT_device_fault) beside the log: its path, "" when it couldn't be written.
std::string save_vendor_binary(const std::vector<uint8_t>& bytes)
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    constexpr int kYearBase = 1900;
    const std::string name =
        text::format("thug2-decomp-gpu-fault-%04d-%02d-%02d_%02d-%02d-%02d.bin", local.tm_year + kYearBase,
                     local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    const std::string path = game_folder_path(name.c_str());
    fs::DiskStream* const out = fs::disk().open_stream(path.c_str(), "wb", fs::StreamSharing::kAll);
    if (out == nullptr) {
        return {};
    }
    const std::size_t written = fs::disk().stream_write(out, bytes.data(), 1, bytes.size());
    return fs::disk().stream_close(out) == 0 && written == bytes.size() ? path : std::string();
}

std::string address_name(const void* user, VkDeviceAddress address)
{
    return static_cast<const Memory*>(user)->describe_address(address);
}

// The marks of the passes recorded outside the lock (the view listeners' calls, the world's surfaces): into the stream.
void pass_marked(const gpu_markers::Mark& mark)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    if (state.device_made && state.draws_made) {
        state.frames.mark(mark);
    }
}

// Which markers the frames record, for the log.
const char* markers_text(const Backend& state)
{
    if (state.frames.markers().checkpoints()) {
        return "NVIDIA's diagnostic checkpoints";
    }
    if (state.frames.markers().on()) {
        return "AMD's buffer markers";
    }
    return state.options.crash_markers == CrashMarkerChoice::kOff ? "off ([debug] gpu_crash_markers=0)"
                                                                  : "none (not offered)";
}

} // namespace

void install_crash_markers(Backend& state)
{
    // (round PF: the marks also time every pass with frame_profile on - vulkan_pass_profile.h)
    gpu_markers::set_sink(state.frames.markers().on() || state.frames.passes_timed() ? &pass_marked : nullptr);
    const CrashDiagnostics& has = state.context.crash_diagnostics();
    get_log("vulkan: GPU crash diagnostics - markers: %s; the driver's fault report: %s%s; memory budget: %s",
            markers_text(state), has.device_fault ? "VK_EXT_device_fault" : "not offered",
            has.fault_vendor_binary ? " with the vendor's binary dump" : "", has.memory_budget ? "yes" : "no");
}

void remove_crash_markers() { gpu_markers::set_sink(nullptr); }

bool check_device_lost(Backend& state)
{
    const Frames::Lost& lost = state.frames.lost();
    if (!lost.lost) {
        return false;
    }
    if (state.lost_reported) {
        return true;
    }
    state.lost_reported = true;
    gpu_markers::set_sink(nullptr);
    const Context& context = state.context;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &properties);
    get_log("vulkan: THE DEVICE WAS LOST (VkResult %d, seen at %s) - submission %llu, %llu frames presented; %s, "
            "driver %s. The GPU crash report follows, then the game quits (a lost device never comes back)",
            static_cast<int>(lost.result), lost.where, static_cast<unsigned long long>(lost.serial),
            static_cast<unsigned long long>(state.frame), std::data(properties.deviceName),
            state.adapter.driver.data());
    // which pass: the markers each queue reached, and the lost submission's passes
    const uint64_t newest = state.frames.serial();
    const uint64_t completed = state.frames.completed();
    log_lines("markers", state.frames.markers().reached(context.all_queues(), newest, completed));
    // (round GF) the passes of every submission not known finished - the loss may be in an earlier one still running
    // than the submission it was seen at - the oldest first
    const uint64_t first = std::max(completed + 1, newest >= CrashMarkers::kKept ? newest - CrashMarkers::kKept + 1 : 1);
    for (uint64_t serial = first; serial <= newest; ++serial) {
        const std::vector<uint32_t> passes = state.frames.markers().passes_of(serial);
        const char* const role = serial == lost.serial ? " (the loss seen at it)" : "";
        if (passes.empty()) {
            get_log("vulkan: device lost: submission %llu%s's passes - none kept (no markers recorded in it)",
                    static_cast<unsigned long long>(serial), role);
        } else {
            log_lines(text::format("submission %llu%s's passes", static_cast<unsigned long long>(serial), role).c_str(),
                      describe_passes(passes));
        }
    }
    // which resource: the driver's report, its addresses named by the buffers holding them
    const CrashDiagnostics& has = context.crash_diagnostics();
    if (has.device_fault) {
        const FaultReport fault = read_fault(context.device(), has.fault_vendor_binary);
        log_lines("fault", describe_fault(fault, &address_name, &state.memory));
        if (!fault.vendor_binary.empty()) {
            const std::string saved = save_vendor_binary(fault.vendor_binary);
            get_log("vulkan: device lost: the vendor's binary dump %s%s",
                    saved.empty() ? "could not be saved" : "saved to ", saved.c_str());
        }
    } else {
        get_log("vulkan: device lost: fault - no driver report (VK_EXT_device_fault not offered)");
    }
    // the memory, and what the game ran with
    log_lines("memory", describe_memory(context.physical_device(), has.memory_budget));
    const VmaTotalStatistics totals = state.memory.statistics();
    get_log("vulkan: device lost: memory - the game's allocations: %u, %.0f MB (%.0f MB in %u blocks)",
            totals.total.statistics.allocationCount,
            static_cast<double>(totals.total.statistics.allocationBytes) / kBytesPerMegabyte,
            static_cast<double>(totals.total.statistics.blockBytes) / kBytesPerMegabyte,
            totals.total.statistics.blockCount);
    const std::string settings = config::describe(config::running());
    get_log("vulkan: device lost: settings in effect -\n%s", settings.empty() ? "(as the ini asks)" : settings.c_str());
    get_log("vulkan: device lost: the report ends - the game quits");
    log_flush_to_disk();
    engine_parts().set_quit_requested(true);
    return true;
}

} // namespace render::vulkan
