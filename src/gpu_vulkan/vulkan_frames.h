#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_memory.h"
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
namespace render::vulkan {

// Objects released while the device may still use them: destroyed when their submission has finished.
struct Retired {
    std::vector<Image> images;
    std::vector<DeviceBuffer> buffers;
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

    void retire(const Image& image);
    void retire(const DeviceBuffer& buffer);

    // What is recorded submitted and waited for (the queue idle): every retired object destroyed, this slot's rings
    // used from their start again. False on a Vulkan failure.
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
    bool restarted_ = false;
    std::vector<VkSemaphore> waits_;
    std::vector<VkPipelineStageFlags> wait_stages_;
    std::vector<VkSemaphore> signals_;
};

} // namespace render::vulkan
