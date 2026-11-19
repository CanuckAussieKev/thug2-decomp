#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu_markers.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_crash_markers.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_pass_profile.h"
#include "gpu_vulkan/vulkan_ring.h"

// NOT ORIGINAL - the Vulkan Gpu's frames (vulkan_frames.cpp; docs/design/vulkan.md, V7): what the Gpu records, in two
// command buffers per frame in flight, and when it runs.
//   - The uploads: the frame's transfers that nothing recorded before them in this submission touches (a texture's
//     level, a static buffer's bytes, a new image's first layout, a query's reset), from a staging ring. They run before
//     the stream, so a frame's loading is one submission, not one per unlock.
//   - The stream: everything else in the order the Gpu was called - rendering passes and their draws and clears, blits
//     and resolves, the transfers to something the frame already used, the present's copy into the swapchain.
// A submission is both (uploads first), numbered: a resource records the last submission that used it, and is free again
// once that one has finished. The frame ends with the present's submission; the next frame in flight's slot is then
// waited for (its fence), its deleted objects destroyed, its rings used from their start. A read-back finishes everything
// first (submitted and waited for); so does a staging ring that has grown past its limit while loading.
// (round GF) Objects are retired for the frame, not the submission: a frame finished part-way (a read-back) goes on
// recording after it, and what it captured at its start - the ray tracing scene's TLAS (its BLASes' addresses) and
// geometry table (buffers' addresses) - may still name them; they are destroyed only once the frame's slot comes round
// (its last submission waited for). Frames are numbered too (frame(), completed_frame()): a dynamic buffer's memory
// whose address a frame captured is not handed out again until that frame is over (vulkan_gpu.cpp discard_version).
namespace render::vulkan {

// Objects released while the device may still use them: destroyed when their frame has finished.
struct Retired {
    std::vector<Image> images;
    std::vector<DeviceBuffer> buffers;
    std::vector<VkAccelerationStructureKHR> structures; // (round GF: ray tracing's - destroyed before the buffers)
};

class Frames {
public:
    static constexpr uint32_t kSlots = Context::kFramesInFlight;

    Frames() = default;
    Frames(const Frames&) = delete;
    Frames& operator=(const Frames&) = delete;
    Frames(Frames&&) = delete;
    Frames& operator=(Frames&&) = delete;
    ~Frames() = default;

    // The rings a slot's frame uses: started again when the slot comes round.
    struct Rings {
        UploadRing* draws = nullptr;
        UploadRing* staging = nullptr;
    };
    // The slots' command pools, buffers, fences and acquire semaphores.
    bool init(const Context& context, Memory* memory, const Rings& rings);
    // Everything waited for, every retired object destroyed, the slots released.
    void shutdown();

    // The command buffers being recorded (begun on first use).
    VkCommandBuffer uploads();
    VkCommandBuffer stream();
    [[nodiscard]] bool stream_begun() const;
    // A new stream command buffer was begun since the last call (the Gpu's bound state is then forgotten).
    bool take_stream_restarted();

    // The submission being recorded, and the last one known to have finished.
    [[nodiscard]] uint64_t serial() const { return serial_; }
    [[nodiscard]] uint64_t completed() const { return completed_; }
    // (round GF) The frame being recorded (from 1, one more at each end_frame), and the last one whose every submission
    // is known finished (not the one being recorded, even after finish()).
    [[nodiscard]] uint64_t frame() const { return frame_; }
    [[nodiscard]] uint64_t completed_frame() const { return completed_frame_; }

    // Destroyed once the frame being recorded is over (its slot come round, its fence waited for).
    void retire(const Image& image);
    void retire(const DeviceBuffer& buffer);
    // (round GF) An acceleration structure and the buffer holding it (either may be null).
    void retire(VkAccelerationStructureKHR structure, const DeviceBuffer& memory);
    // Objects retired, not destroyed yet (the log's, the checks').
    [[nodiscard]] std::size_t retired_count() const;

    // What is recorded submitted and waited for (the queue idle): the earlier frames' retired objects destroyed (round
    // GF: the current frame's kept - it goes on), this slot's rings used from their start again. False on a Vulkan
    // failure.
    bool finish();
    // The frame's end: what is recorded submitted (waiting for `acquired` at the transfer stage and signalling
    // `presented`, when given), then the next slot made ready (its previous frame waited for).
    bool end_frame(VkSemaphore acquired, VkSemaphore presented);
    // The current slot's semaphore for the swapchain's acquire.
    [[nodiscard]] VkSemaphore acquire_semaphore() const;
    // The slot being recorded (0 .. kSlots - 1).
    [[nodiscard]] uint32_t slot_index() const { return current_; }
    // A semaphore the next submission waits for (at `stage`) and one it signals, besides the present's - the UI's own
    // windows' swapchain images (vulkan_ui_windows.cpp): whichever submission comes next, finish() or end_frame(),
    // takes them.
    struct Wait {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkPipelineStageFlags stage = 0;
    };
    void add_wait(const Wait& wait);
    void add_signal(VkSemaphore semaphore);

    // NOT ORIGINAL - (round GD: GPU crash diagnostics, vulkan_crash_markers.h) a pass's beginning or end marked in the
    // stream (begun for it), in the submission being recorded; the markers for a lost device's report.
    void mark(const gpu_markers::Mark& mark);
    [[nodiscard]] const CrashMarkers& markers() const { return markers_; }
    // (round PF) every pass timed on the device by its marks, with frame_profile on (vulkan_pass_profile.h).
    [[nodiscard]] bool passes_timed() const { return pass_profile_.on(); }
    // The device lost (VK_ERROR_DEVICE_LOST from a submission or a wait): nothing is submitted or waited for after it.
    // Where it was seen and the submission it was seen on (the one submitted, or waited for).
    struct Lost {
        bool lost = false;
        const char* where = "";
        uint64_t serial = 0;
        VkResult result = VK_SUCCESS;
    };
    [[nodiscard]] const Lost& lost() const { return lost_; }
    // Told by the Gpu when another call saw the device lost (the swapchain's acquire or present, a library's call).
    void note_lost(const char* where, VkResult result);

private:
    struct Slot {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer uploads = nullptr;
        VkCommandBuffer stream = nullptr;
        VkFence done = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        bool uploads_begun = false;
        bool stream_begun = false;
        bool pending = false;  // submitted, not yet waited for
        bool timed = false;    // (frame_profile) its stream's start and end timestamps written
        uint64_t submitted = 0; // its last submission's number
        uint64_t frame = 0;     // (round GF) the frame it holds
        Retired retired;
    };

    Slot& slot();
    [[nodiscard]] const Slot& slot() const;
    bool submit(VkSemaphore acquired, VkSemaphore presented);
    void destroy(Retired& retired);
    // The slot's command buffers made ready to record again (its submissions finished).
    void recycle(Slot& slot);
    // NOT ORIGINAL - [debug] frame_profile (platform/frame_profile.h): each submission's stream timed on the device
    // (timestamps at its start and end, read when its slot comes round): the "gpu" phase.
    void init_timing(const Context& context);
    void read_timing(Slot& slot);
    VkQueryPool timing_ = VK_NULL_HANDLE;
    double tick_ms_ = 0.0;

    VkDevice device_ = nullptr;
    VkQueue queue_ = nullptr;
    Memory* memory_ = nullptr;
    UploadRing* draws_ = nullptr;
    UploadRing* staging_ = nullptr;
    std::array<Slot, kSlots> slots_{};
    uint32_t current_ = 0;
    uint64_t serial_ = 1;
    uint64_t completed_ = 0;
    uint64_t frame_ = 1;           // (round GF)
    uint64_t completed_frame_ = 0; // (round GF)
    bool restarted_ = false;
    std::vector<VkSemaphore> waits_;
    std::vector<VkPipelineStageFlags> wait_stages_;
    std::vector<VkSemaphore> signals_;
    CrashMarkers markers_; // (round GD)
    PassProfile pass_profile_; // (round PF)
    Lost lost_;
    // A frame's fence waited for (wait_for_frame), a loss noted; true when it finished.
    bool wait(const Slot& slot);
};

// NOT ORIGINAL - (round PF) a step inside a pass marked from here to the end of the scope (gpu_markers.h section::kDiGrid
// ...: the crash markers' finer places, the pass profile's rows) - only when `commands` is the stream being recorded (a
// check program's own command buffers aren't marked).
class FrameStep {
public:
    FrameStep(Frames& frames, VkCommandBuffer commands, uint16_t section)
        : frames_(frames), section_(section), on_(frames.stream_begun() && commands == frames.stream())
    {
        if (on_) {
            frames_.mark({.phase = gpu_markers::Phase::kGpu, .id = section_, .end = false});
        }
    }
    ~FrameStep()
    {
        if (on_) {
            frames_.mark({.phase = gpu_markers::Phase::kGpu, .id = section_, .end = true});
        }
    }
    FrameStep(const FrameStep&) = delete;
    FrameStep& operator=(const FrameStep&) = delete;
    FrameStep(FrameStep&&) = delete;
    FrameStep& operator=(FrameStep&&) = delete;

private:
    Frames& frames_;
    uint16_t section_;
    bool on_;
};

} // namespace render::vulkan
