#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu_markers.h"
#include "gpu_vulkan/vulkan_context.h"

// NOT ORIGINAL - (round PF: path-tracer cost) every pass's time on the device, from the passes' marks. The GPU crash
// markers' marks (engine/gfx/gpu_markers.h: each view listener's call at its ViewOrder, the world's surfaces, the ray
// tracing scene's builds, the UI, the present, the stream itself) are also where a timestamp is written when
// `[debug] frame_profile` is on - one profiler for every pass, no pass timing itself. A timestamp at the bottom of the
// pipe at a pass's beginning and end (the time from the work before it finishing to its own finishing - the usual way
// to split a serial command stream); read when the frame's slot comes round (no wait). Nested passes (a listener's call
// inside the world's surfaces, everything inside the stream) are counted inclusive and exclusive. Every
// kReportFrames frames the log gets one line a pass: "gpu pass: <name> <inclusive> ms (own <exclusive>) a frame,
// <calls> a frame" - the stream's own time is what no pass covers (the raster draws outside the world's surfaces,
// barriers, copies).
namespace render::vulkan {

class PassProfile {
public:
    static constexpr uint32_t kStampsPerSlot = 1024;
    static constexpr uint32_t kReportFrames = 300;

    // Made only with frame_profile on and timestamps on the queue (else every call does nothing).
    void init(const Context& context, uint32_t slots);
    void shutdown(VkDevice device);
    [[nodiscard]] bool on() const { return pool_ != VK_NULL_HANDLE; }

    // A slot's stream begun: its stamps reset (outside any rendering).
    void stream_begun(VkCommandBuffer commands, uint32_t slot);
    // A pass's mark in the slot's stream.
    void mark(VkCommandBuffer commands, uint32_t slot, const gpu_markers::Mark& mark);
    // The slot's submission finished (its fence waited for): its stamps read and added up (the frame it held: its number).
    struct Finished {
        uint32_t slot = 0;
        uint64_t frame = 0;
    };
    void read(const Finished& finished);

private:
    struct Stamp {
        gpu_markers::Phase phase = gpu_markers::Phase::kGpu;
        uint16_t id = 0;
        bool end = false;
    };
    struct SlotStamps {
        std::vector<Stamp> stamps;
        bool reset = false;
    };
    struct Sum {
        double inclusive_ms = 0.0;
        double own_ms = 0.0;
        uint32_t calls = 0;
    };
    void report();
    void frame_done(); // (round PERF5) the frame's own times against each pass's usual: a slow frame logged

    VkDevice device_ = nullptr;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    double tick_ms_ = 0.0;
    uint64_t valid_mask_ = ~0ULL;
    std::vector<SlotStamps> slots_;
    std::map<uint32_t, Sum> sums_; // by (phase << 16) | id
    uint64_t last_frame_ = 0;
    uint32_t frames_ = 0;
    // (round PERF5) this frame's own time a pass and each pass's usual (an average over the last ~30 frames)
    std::map<uint32_t, double> frame_own_;
    std::map<uint32_t, double> usual_own_;
    double usual_frame_ = 0.0;
    uint32_t slow_logged_ = 0;
    uint32_t followed_ = 0; // (frames the usual times have followed)
    uint32_t dropped_ = 0; // marks past kStampsPerSlot in a submission
};

// (round PERF3) With frame_profile on and the device's support: the flag a pipeline is made with to have its compiled
// shaders' statistics read (else 0), and those statistics in the log ("shader stats: <name>: ...").
// The context sets whether they can be (Context::pipeline_statistics); source names the pass in the log.
void set_pipeline_statistics(bool on);
[[nodiscard]] VkPipelineCreateFlags pipeline_statistics_flags();
void log_pipeline_statistics(VkDevice device, VkPipeline pipeline, const std::string& source);

} // namespace render::vulkan
