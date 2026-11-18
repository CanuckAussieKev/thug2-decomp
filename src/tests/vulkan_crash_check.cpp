// NOT ORIGINAL - thug2_vulkan_crash_check.exe: the GPU crash diagnostics (round GD - gpu_vulkan/vulkan_crash_markers.h,
// engine/gfx/gpu_markers.h) checked on this machine, without hanging the GPU:
//   1. every view listener's order has a name in the one table, and every name's order is a listener's;
//   2. a marker's value carries its pass and its submission (encode / decode, the submission found again past a wrap);
//   3. every view listener's call - view_begins, sky_drawn, opaque_drawn, world_drawn - is marked begun before it and
//      ended after it, at its order, and the world's surfaces as a section;
//   4. a lost device's report formatted from a simulated one: the driver's fault (addresses named by the buffers
//      holding them, the vendor's codes, its dump), the markers reached (the pass the device was in) and the frame's
//      passes - printed as the log would have them;
//   5. on the device: NVIDIA's checkpoints (or AMD's buffer markers) recorded by the frames and read back - the last
//      marker that finished is the last submission's end; a fault address found in a live buffer; the heaps' budget;
//      then a loss simulated (Frames::note_lost): nothing more is submitted or waited for, and it returns at once;
//   6. AMD's buffer markers on a device that has them (forced where checkpoints are offered too).
//   thug2_vulkan_crash_check [--display N] [--device N]
// Exit code: 0 when every case passed, 1 when one failed, 2 when this machine has no Vulkan for this process (the
// processor-side cases 1-4 still run and count).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu_markers.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_crash_markers.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "tests/check_window.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace gm = render::gpu_markers;
namespace vk = render::vulkan;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 180;

int& failures()
{
    static int count = 0;
    return count;
}

// The simulated report's numbers: the submission lost, the next, a marker's ids' limit, a vendor dump's size.
constexpr uint64_t kLost = 5361;
constexpr uint64_t kNext = kLost + 1;
constexpr int kMostIds = 1024;
constexpr std::size_t kDumpBytes = 4096;

void expect(bool ok, const std::string& what)
{
    text::print("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) {
        ++failures();
    }
}

bool contains(const std::vector<std::string>& lines, std::string_view part)
{
    return std::ranges::any_of(lines, [part](const std::string& line) { return line.find(part) != std::string::npos; });
}

void print_lines(const char* heading, const std::vector<std::string>& lines)
{
    for (const std::string& line : lines) {
        text::print("    vulkan: device lost: %s - %s\n", heading, line.c_str());
    }
}

// Every view listener's order in the game (rt_scene.h ViewOrder, and the ones placed beside them).
constexpr std::array kOrders{
    render::rt::ViewOrder::kSky,
    render::rt::ViewOrder::kShadows / 2,
    render::rt::ViewOrder::kSurfaceInputs,
    render::rt::ViewOrder::kShadows,
    render::rt::ViewOrder::kOcclusion,
    render::rt::ViewOrder::kIndirect,
    render::rt::ViewOrder::kLights - 1,
    render::rt::ViewOrder::kLights,
    render::rt::ViewOrder::kLights + 1,
    render::rt::ViewOrder::kDecals,
    render::rt::ViewOrder::kSurfaceShading,
    render::rt::ViewOrder::kRain,
    render::rt::ViewOrder::kReflections,
    render::rt::ViewOrder::kSnow,
    render::rt::ViewOrder::kClutter,
    render::rt::ViewOrder::kGrass,
    render::rt::ViewOrder::kFog,
    render::rt::ViewOrder::kSun,
    render::rt::ViewOrder::kUpscale,
    render::rt::ViewOrder::kCel,
    render::rt::ViewOrder::kCameraEffects,
    render::rt::ViewOrder::kLens,
    render::rt::ViewOrder::kToneMapping,
    render::rt::ViewOrder::kStyle,
    render::rt::ViewOrder::kGrade,
    render::rt::ViewOrder::kFrameGeneration,
};

void check_names()
{
    text::print("1. the passes' names\n");
    const std::span<const gm::ListenerName> table = gm::listener_names();
    bool every_order = true;
    for (const int order : kOrders) {
        const bool named =
            std::ranges::any_of(table, [order](const gm::ListenerName& one) { return one.order == order; });
        if (!named) {
            text::print("    no name for order %d\n", order);
        }
        every_order = every_order && named;
    }
    expect(every_order, "every view listener's order is named");
    const bool every_name = std::ranges::all_of(table, [](const gm::ListenerName& one) {
        return std::ranges::find(kOrders, one.order) != kOrders.end() && one.order >= 0 && one.order < kMostIds;
    });
    expect(every_name, "every name is a listener's order (0 - 1023: a marker's 10 bits)");
    expect(gm::name(gm::Phase::kOpaque, render::rt::ViewOrder::kLights) == "opaque_drawn: lamps (350)",
           "a listener's pass named with its phase: " + gm::name(gm::Phase::kOpaque, render::rt::ViewOrder::kLights));
    expect(gm::name(gm::Phase::kGpu, gm::section::kUploads) == "the Gpu: uploads", "a Gpu section named");
}

void check_encoding()
{
    text::print("2. a marker's value\n");
    bool round_trip = true;
    for (uint16_t phase = 0; phase < gm::kPhaseCount; ++phase) {
        for (const int order : kOrders) {
            for (const bool end : {false, true}) {
                const gm::Mark mark{
                    .phase = static_cast<gm::Phase>(phase), .id = static_cast<uint16_t>(order), .end = end};
                const uint32_t value = vk::encode_marker(mark, kLost);
                const gm::Mark back = vk::decode_marker(value);
                round_trip = round_trip && value != 0 && back.phase == mark.phase && back.id == mark.id &&
                             back.end == mark.end && vk::marker_serial(value, kNext) == kLost;
            }
        }
    }
    expect(round_trip, "every phase, order and end round-trips with its submission");
    constexpr uint64_t kWrap = 1ULL << 18U; // (a marker keeps 18 bits of its submission)
    const uint32_t before_wrap =
        vk::encode_marker({.phase = gm::Phase::kWorld, .id = render::rt::ViewOrder::kToneMapping}, kWrap - 1);
    expect(vk::marker_serial(before_wrap, kWrap + 1) == kWrap - 1, "a submission found again across its bits' wrap");
    expect(
        vk::marker_text(vk::encode_marker({.phase = gm::Phase::kOpaque, .id = render::rt::ViewOrder::kShadows}, kLost),
                        kNext) == "submission 5361: the beginning of opaque_drawn: ray-traced sun shadows (100)",
        "a marker's text");
}

// A listener that notes when it is called (between its marks).
std::vector<std::string>& recorded()
{
    static std::vector<std::string> lines;
    return lines;
}

void record_mark(const gm::Mark& mark)
{
    recorded().push_back(text::format("%s %u %u", mark.end ? "end" : "begin", static_cast<unsigned>(mark.phase),
                                      static_cast<unsigned>(mark.id)));
}

class Listener final : public render::rt::ViewListener {
public:
    explicit Listener(int order) : order_(order) {}
    [[nodiscard]] int order() const override { return order_; }
    void opaque_drawn(const render::rt::ViewDrawn& /*view*/) override { called(gm::Phase::kOpaque); }
    void world_drawn(const render::rt::ViewDrawn& /*view*/) override { called(gm::Phase::kWorld); }
    void sky_drawn(const render::rt::ViewDrawn& /*view*/) override { called(gm::Phase::kSky); }
    void view_begins(const render::rt::ViewDrawn& /*view*/) override { called(gm::Phase::kViewBegins); }

private:
    void called(gm::Phase phase) const
    {
        recorded().push_back(text::format("call %u %d", static_cast<unsigned>(phase), order_));
    }
    int order_;
};

void check_listener_marks()
{
    text::print("3. every listener's call marked\n");
    std::vector<std::unique_ptr<Listener>> listeners;
    for (const int order : kOrders) {
        listeners.push_back(std::make_unique<Listener>(order));
        render::rt::add_view_listener(listeners.back().get());
    }
    gm::set_sink(&record_mark);
    recorded().clear();
    const render::rt::ViewDrawn view{};
    render::rt::view_begins(view);
    render::rt::sky_view_drawn(view);
    render::rt::set_world_surfaces_drawing(true);
    render::rt::set_world_surfaces_drawing(false);
    render::rt::opaque_view_drawn(view);
    render::rt::world_view_drawn(view);
    gm::set_sink(nullptr);
    // what was expected: per phase, each listener (in its order) begun, called, ended
    std::vector<int> sorted(kOrders.begin(), kOrders.end());
    std::ranges::stable_sort(sorted);
    std::vector<std::string> expected;
    const auto phase_calls = [&](gm::Phase phase) {
        for (const int order : sorted) {
            const auto phase_number = static_cast<unsigned>(phase);
            expected.push_back(text::format("begin %u %u", phase_number, static_cast<unsigned>(order)));
            expected.push_back(text::format("call %u %d", phase_number, order));
            expected.push_back(text::format("end %u %u", phase_number, static_cast<unsigned>(order)));
        }
    };
    phase_calls(gm::Phase::kViewBegins);
    phase_calls(gm::Phase::kSky);
    expected.push_back(text::format("begin 0 %u", static_cast<unsigned>(gm::section::kWorldSurfaces)));
    expected.push_back(text::format("end 0 %u", static_cast<unsigned>(gm::section::kWorldSurfaces)));
    phase_calls(gm::Phase::kOpaque);
    phase_calls(gm::Phase::kWorld);
    expect(recorded() == expected,
           text::format("%u listener calls in 4 phases each begun and ended around the call "
                        "(%u marks and calls), the world's surfaces a section",
                        static_cast<unsigned>(kOrders.size()), static_cast<unsigned>(recorded().size())));
    recorded().clear();
    render::rt::opaque_view_drawn(view);
    expect(recorded().size() == kOrders.size(), "no sink: the calls alone, nothing marked");
    for (const auto& listener : listeners) {
        render::rt::remove_view_listener(listener.get());
    }
    recorded().clear();
}

std::string fake_buffer(const void* /*user*/, VkDeviceAddress address)
{
    constexpr VkDeviceAddress kStart = 0x7F0000000ULL;
    constexpr VkDeviceAddress kBytes = 0x100000ULL;
    if (address >= kStart && address < kStart + kBytes) {
        return text::format("in a 1.00 MB buffer at 0x%llx (storage) - at 0x%llx bytes from its start",
                            static_cast<unsigned long long>(kStart), static_cast<unsigned long long>(address - kStart));
    }
    return {};
}

void check_report_format()
{
    text::print("4. a lost device's report (simulated)\n");
    vk::FaultReport fault;
    fault.result = VK_SUCCESS;
    fault.description = "page fault";
    VkDeviceFaultAddressInfoEXT read{};
    read.addressType = VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT;
    read.reportedAddress = 0x7F0001234ULL;
    read.addressPrecision = 0x1000;
    VkDeviceFaultAddressInfoEXT pointer{};
    pointer.addressType = VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT;
    pointer.reportedAddress = 0x123450ULL;
    pointer.addressPrecision = 1;
    fault.addresses = {read, pointer};
    VkDeviceFaultVendorInfoEXT vendor{};
    constexpr std::string_view kVendorText = "MMU fault: GPC 2";
    std::ranges::copy(kVendorText, std::begin(vendor.description));
    vendor.vendorFaultCode = 0x1F;
    vendor.vendorFaultData = 0xABC;
    fault.vendor = {vendor};
    fault.vendor_binary.assign(kDumpBytes, 0);
    const std::vector<std::string> fault_lines = vk::describe_fault(fault, &fake_buffer, nullptr);
    expect(contains(fault_lines, "the driver says: \"page fault\""), "the driver's description");
    expect(contains(fault_lines,
                    "address 0x7f0001234 (an invalid read; within 0x7f0001000 - 0x7f0001fff) - in a 1.00 MB "
                    "buffer at 0x7f0000000 (storage) - at 0x1234 bytes from its start"),
           "a read fault's address, its precision's range and the buffer holding it");
    expect(contains(fault_lines, "an instruction that caused a fault; within 0x123450 - 0x123450) - a shader's code"),
           "an instruction address: a shader's code, not looked up among the buffers (round GF)");
    expect(contains(fault_lines, "vendor: \"MMU fault: GPC 2\" (code 0x1f, data 0xabc)"), "the vendor's codes");
    expect(contains(fault_lines, "the vendor's binary dump: 4096 bytes"), "the vendor's dump");
    vk::FaultReport unread;
    unread.result = VK_ERROR_DEVICE_LOST;
    expect(contains(vk::describe_fault(unread, nullptr, nullptr), "could not be read (VkResult -4)"),
           "a report that can't be read says so");

    // the markers: the device begun the lamps (opaque_drawn) in submission 5361, finished only the shadows before
    const uint32_t shadows_end =
        vk::encode_marker({.phase = gm::Phase::kOpaque, .id = render::rt::ViewOrder::kShadows, .end = true}, kLost);
    const uint32_t lamps_begun =
        vk::encode_marker({.phase = gm::Phase::kOpaque, .id = render::rt::ViewOrder::kLights}, kLost);
    const std::array reached{vk::ReachedMarker{.stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .marker = lamps_begun},
                             vk::ReachedMarker{.stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, .marker = shadows_end}};
    const std::vector<std::string> marker_lines = vk::describe_reached(0, reached, kNext);
    expect(contains(marker_lines, "queue 0: the device was in opaque_drawn: lamps (350) (submission 5361): begun, not "
                                  "finished"),
           "the pass the device was in");
    const std::array settled{vk::ReachedMarker{.stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .marker = shadows_end},
                             vk::ReachedMarker{.stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, .marker = shadows_end}};
    expect(contains(vk::describe_reached(0, settled, kNext), "everything marked had finished"),
           "everything finished: the unmarked work after the last end");

    // the frame's passes
    std::vector<uint32_t> passes;
    const auto add = [&passes](gm::Phase phase, uint16_t id) {
        passes.push_back(vk::encode_marker({.phase = phase, .id = id}, 0));
    };
    add(gm::Phase::kGpu, gm::section::kUploads);
    add(gm::Phase::kGpu, gm::section::kStream);
    add(gm::Phase::kGpu, gm::section::kRtScene);
    add(gm::Phase::kViewBegins, render::rt::ViewOrder::kUpscale);
    add(gm::Phase::kGpu, gm::section::kWorldSurfaces);
    add(gm::Phase::kOpaque, render::rt::ViewOrder::kShadows);
    add(gm::Phase::kOpaque, render::rt::ViewOrder::kLights);
    add(gm::Phase::kOpaque, render::rt::ViewOrder::kLights);
    const std::vector<std::string> pass_lines = vk::describe_passes(passes);
    expect(contains(pass_lines, "the Gpu: uploads, stream, ray tracing scene builds") &&
               contains(pass_lines, "view_begins: upscaler (950)") &&
               contains(pass_lines, "opaque_drawn: ray-traced sun shadows (100), lamps (350) x2"),
           "the frame's passes, a line per phase, repeats counted");
    text::print("  the report as the log has it:\n");
    text::print(
        "    vulkan: THE DEVICE WAS LOST (VkResult -4, seen at waiting for a frame's fence) - submission 5361, ...\n");
    print_lines("markers", marker_lines);
    print_lines("submission 5361's passes", pass_lines);
    print_lines("fault", fault_lines);
}

// The context's lines (the validation layer's messages among them), counted.
int& validation_messages()
{
    static int count = 0;
    return count;
}

void on_message(const std::string& line)
{
    text::print("    vulkan: %s\n", line.c_str());
    if (!line.starts_with("the validation layer is on")) { // (the context's own note that it is on)
        ++validation_messages();
    }
}

// The device's part: a context, its memory, the frames.
int check_device(SDL_Window* window, int device, vk::CrashMarkerChoice choice)
{
    const bool forced_buffer = choice == vk::CrashMarkerChoice::kBufferMarkers;
    text::print("%s\n", forced_buffer ? "6. AMD's buffer markers (forced)" : "5. on the device");
    vk::Context context;
    validation_messages() = 0;
    const vk::ContextOptions options{.validation = true,
                                     .device_index = device,
                                     .on_message = &on_message,
                                     .ray_tracing = true,
                                     .additions = nullptr,
                                     .crash_markers = choice};
    if (!context.create(window, options)) {
        context.destroy();
        text::print("  no context: no Vulkan here\n");
        return 2;
    }
    const vk::CrashDiagnostics& has = context.crash_diagnostics();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &properties);
    text::print(
        "  %s: fault report %s (vendor dump %s), checkpoints %s, buffer markers %s, memory budget %s; %u queues\n",
        std::data(properties.deviceName), has.device_fault ? "yes" : "no", has.fault_vendor_binary ? "yes" : "no",
        has.checkpoints ? "yes" : "no", has.buffer_markers ? "yes" : "no", has.memory_budget ? "yes" : "no",
        static_cast<unsigned>(context.all_queues().size()));
    if (forced_buffer && !has.buffer_markers) {
        text::print("  the device has no VK_AMD_buffer_marker - skipped\n");
        context.destroy();
        return 0;
    }
    vk::Memory memory;
    vk::UploadRing ring;
    vk::UploadRing staging;
    vk::Frames frames;
    if (!memory.init(context) || !ring.init(&memory) || !staging.init(&memory, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !frames.init(context, &memory, {.draws = &ring, .staging = &staging})) {
        expect(false, "the memory and the frames made");
        context.destroy();
        return 1;
    }
    const bool markers_expected = has.checkpoints || has.buffer_markers;
    const char* markers_named = "no markers (the device has neither)";
    if (markers_expected) {
        markers_named = has.checkpoints ? "NVIDIA's checkpoints on" : "AMD's buffer markers on";
    }
    expect(frames.markers().on() == markers_expected, markers_named);
    // two frames with passes marked, then everything finished: the markers read back
    for (int frame = 0; frame < 2; ++frame) {
        frames.mark({.phase = gm::Phase::kOpaque, .id = render::rt::ViewOrder::kShadows});
        frames.mark({.phase = gm::Phase::kOpaque, .id = render::rt::ViewOrder::kShadows, .end = true});
        frames.mark({.phase = gm::Phase::kWorld, .id = render::rt::ViewOrder::kToneMapping});
        frames.mark({.phase = gm::Phase::kWorld, .id = render::rt::ViewOrder::kToneMapping, .end = true});
        frames.end_frame(VK_NULL_HANDLE, VK_NULL_HANDLE);
    }
    const uint64_t last = frames.serial() - 1;
    expect(frames.finish(), "the frames submitted and finished");
    // (NVIDIA's checkpoints are read only from a lost device - vkGetQueueCheckpointDataNV's rule: their recording is
    // checked by the validation layer here; AMD's buffer markers are read back now, case 6)
    if (markers_expected && !has.checkpoints) {
        const std::vector<std::string> lines = frames.markers().reached(context.all_queues(), frames.serial(), frames.completed());
        print_lines("markers", lines);
        const std::string finished =
            text::format("the bottom of the pipe (finished) reached submission %llu: the end of the Gpu: stream",
                         static_cast<unsigned long long>(last));
        expect(contains(lines, finished), "the last marker finished: the last submission's stream ended");
        expect(contains(lines, "everything marked had finished"), "nothing left running");
        if (forced_buffer) {
            const std::array<uint32_t, 2> values = frames.markers().buffer_values();
            const uint32_t stream_end =
                vk::encode_marker({.phase = gm::Phase::kGpu, .id = gm::section::kStream, .end = true}, last);
            expect(std::get<0>(values) == stream_end && std::get<1>(values) == stream_end,
                   "the buffer's two values: the stream's end at the top and the bottom of the pipe");
        }
    }
    if (markers_expected) {
        const std::vector<std::string> passes = vk::describe_passes(frames.markers().passes_of(last));
        print_lines(text::format("submission %llu's passes", static_cast<unsigned long long>(last)).c_str(), passes);
        expect(contains(passes, "opaque_drawn: ray-traced sun shadows (100)") &&
                   contains(passes, "world_drawn: tone mapping (972)"),
               "the submission's passes kept");
    }
    // a fault address in a live buffer (ray tracing's buffers have device addresses)
    if (context.ray_tracing_enabled()) {
        vk::DeviceBuffer buffer;
        constexpr VkDeviceSize kBytes = 65536;
        if (memory.create_buffer(kBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 vk::BufferPlace::kDevice, &buffer)) {
            const VkBufferDeviceAddressInfo info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer.buffer};
            const VkDeviceAddress address = vkGetBufferDeviceAddress(context.device(), &info);
            memory.set_submission(kLost); // (round GF: when it was made and freed)
            memory.name_buffer(buffer, "the check's buffer");
            const std::string inside = memory.describe_address(address + 0x40);
            text::print("    a fault inside it: %s\n", inside.c_str());
            expect(inside.starts_with("in the check's buffer: a 0.06 MB buffer") &&
                       inside.find("(storage; made in submission") != std::string::npos &&
                       inside.find("at 0x40 bytes") != std::string::npos,
                   "a fault address named by the buffer holding it - its owner, size and usage");
            const std::string just_past = memory.describe_address(address + kBytes + 0x10);
            expect(just_past.empty() || just_past.starts_with("just past the end of") ||
                       just_past.starts_with("in "), // (another live buffer may sit right after it)
                   "an address just past a buffer's end said so");
            memory.set_submission(kNext);
            memory.destroy_buffer(&buffer);
            const std::string gone = memory.describe_address(address + 0x40);
            text::print("    after it was destroyed: %s\n", gone.c_str());
            expect(gone.starts_with(text::format("in a buffer FREED in submission %llu (a use after free) - the check's "
                                                 "buffer: a 0.06 MB buffer",
                                                 static_cast<unsigned long long>(kNext))) &&
                       gone.find("at 0x40 bytes") != std::string::npos,
                   "a destroyed buffer named as freed - a use after free, with when it was freed (round GF)");
        }
    }
    const std::vector<std::string> heaps = vk::describe_memory(context.physical_device(), has.memory_budget);
    print_lines("memory", heaps);
    expect(!heaps.empty() && (!has.memory_budget || contains(heaps, "budget")), "the heaps (with their budget)");
    // a loss simulated: nothing submitted or waited for after it, at once
    frames.note_lost("a simulated loss", VK_ERROR_DEVICE_LOST);
    expect(frames.lost().lost && std::string_view(frames.lost().where) == "a simulated loss", "the loss noted");
    const auto before = std::chrono::steady_clock::now();
    frames.mark({.phase = gm::Phase::kOpaque, .id = render::rt::ViewOrder::kShadows});
    const bool ended = frames.end_frame(VK_NULL_HANDLE, VK_NULL_HANDLE);
    const bool finished = frames.finish();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
    constexpr double kAtOnce = 0.5;
    expect(!ended && !finished && seconds < kAtOnce,
           text::format("after the loss a frame's end and a finish return at once, failed (%.3f s)", seconds));
    frames.shutdown();
    staging.shutdown();
    ring.shutdown();
    memory.shutdown();
    text::print("  validation layer: %s\n", context.validation_on() ? "on" : "not installed");
    expect(validation_messages() == 0,
           text::format("no message from the context or the validation layer (%d)", validation_messages()));
    context.destroy();
    return 0;
}

// Where the window opens: centred on display `display` (1 the first; 0: the second when there are two, else the first).
int window_position(int display)
{
    int displays = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&displays);
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    int position = static_cast<int>(SDL_WINDOWPOS_CENTERED);
    if (ids != nullptr && chosen <= displays) {
        const std::span<const SDL_DisplayID> listed(ids, static_cast<std::size_t>(displays));
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(core::at(listed, chosen - 1)));
    }
    SDL_free(ids);
    return position;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    int display = 0;
    int device = -1;
    for (std::size_t i = 1; i + 1 < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        if (argument == "--display") {
            display = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--device") {
            device = text::to_int(core::at(arguments, ++i));
        }
    }
    text::print("thug2_vulkan_crash_check: the GPU crash diagnostics\n");
    check_names();
    check_encoding();
    check_listener_marks();
    check_report_format();
    int result = 0;
    if (!vk::open_loader()) {
        text::print("no Vulkan loader for this process - the device's cases skipped\n");
        result = 2;
    } else {
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            text::print("SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        SDL_Window* window = SDL_CreateWindow("thug2 Vulkan crash check", kWindowWidth, kWindowHeight,
                                              SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (window == nullptr) {
            text::print("SDL_CreateWindow: %s\n", SDL_GetError());
            SDL_Quit();
            return 2;
        }
        const int position = window_position(display);
        SDL_SetWindowPosition(window, position, position);
        tests::show_behind(window);
        result = check_device(window, device, vk::CrashMarkerChoice::kBest);
        if (result == 0) {
            result = check_device(window, device, vk::CrashMarkerChoice::kBufferMarkers);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    text::print("%d failed\n", failures());
    if (failures() > 0) {
        return 1;
    }
    return result;
}
