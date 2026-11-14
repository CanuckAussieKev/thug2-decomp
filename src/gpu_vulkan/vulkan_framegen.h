#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <volk.h>

#include "engine/gfx/gpu_context.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_upscale.h"

// NOT ORIGINAL - frame generation and low latency ([Graphics] frame_generation, frame_generation_provider, reflex -
// gpu_vulkan.h's FrameGenSettings; docs/design/vulkan.md "Frame generation"). Frames made between the drawn ones by
// NVIDIA DLSS Frame Generation (Streamline's DLSS-G, multi-frame 2x - 4x where the device and driver allow;
// vulkan_framegen_dlss.cpp) or AMD FSR 3 frame generation (2x, its own swapchain; vulkan_framegen_fsr.cpp) - DLSS-G
// first on an NVIDIA device (auto), FSR where DLSS-G can't run. The frame:
//   1. Its start (GpuContext::latency_marker - the main loop's simulation start, also a render-only frame's): Reflex
//      waits for its best time to begin (with DLSS-G, or reflex=on/boost), the markers are set.
//   2. The upscaler's work (vulkan_upscale.h; frame generation turns it on at native resolution when upscaler=off): its
//      depth and motion vectors - the upscaler's own images, the render's size - and its camera are the frame's inputs
//      (framegen_inputs), a cut (frame_blend::cuts: a teleport, a respawn) or a history reset throwing the made frames'
//      history away.
//   3. The world finished (world_drawn, last - after the art style and the grade): the back buffer copied out as the
//      HUD-less colour, at the swapchain's size (the frame's 2D isn't in it).
//   4. The present (record_present): the frame's back buffer and the HUD-less colour told apart into the UI image (the
//      2D: where they differ, opaque, with the frame's colour; elsewhere clear) for DLSS-G; FSR finds the 2D from the
//      HUD-less colour itself. The provider is told this frame is on or off and presents: the made frames come before it,
//      without the 2D moving with the motion.
// A frame is off (made frames none, the provider's resources kept) without the inputs or the HUD-less colour - the front
// end's and the pause menu's frames (ViewDrawn::menu), loading screens, a split view, the level editor - and the first
// frame back on starts a fresh history.
namespace render::vulkan {

struct Backend;

// A provider's frame: the inputs (the upscaler's images at (0, 0) of theirs, `render` big; VK_IMAGE_LAYOUT_GENERAL), the
// HUD-less colour and the UI image (the swapchain's size; null without them), whether made frames are wanted, the
// history thrown away.
struct FrameGenFrame {
    bool on = false;
    bool reset = false;
    VkCommandBuffer commands = VK_NULL_HANDLE; // the stream, outside any pass
    const UpscaleFrame* inputs = nullptr;      // (at framegen_inputs)
    Image* hudless = nullptr;                  // (at the present)
    Image* ui = nullptr;
    Extent shown{};      // the swapchain's size
    uint32_t multiplier = 2;
};

class FrameGenProvider {
public:
    FrameGenProvider() = default;
    FrameGenProvider(const FrameGenProvider&) = delete;
    FrameGenProvider& operator=(const FrameGenProvider&) = delete;
    FrameGenProvider(FrameGenProvider&&) = delete;
    FrameGenProvider& operator=(FrameGenProvider&&) = delete;
    virtual ~FrameGenProvider() = default;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual std::string describe() const = 0;
    // The most frames shown for each drawn one (2 - 4).
    [[nodiscard]] virtual uint32_t most_multiplier() const = 0;
    // Whether it wants the UI image (DLSS-G) as well as the HUD-less colour.
    [[nodiscard]] virtual bool wants_ui() const = 0;
    // The frame's inputs (after the upscaler, recorded into frame.commands); false with the reason.
    virtual bool inputs(Backend& state, const FrameGenFrame& frame, std::string* error) = 0;
    // Before the present: the frame on or off, its HUD-less colour and UI; false with the reason.
    virtual bool before_present(Backend& state, const FrameGenFrame& frame, std::string* error) = 0;
    // After the present: the frames the display was handed since the last call (drawn and made).
    virtual uint64_t after_present(Backend& state) = 0;
    // Its device objects released (the device idle or about to be).
    virtual void release(Backend& state) = 0;
    // Its features made so far / released (the checks').
    [[nodiscard]] virtual uint64_t made() const = 0;
    [[nodiscard]] virtual uint64_t released() const = 0;
};

// The settings while the backend's lock is held.
[[nodiscard]] const FrameGenSettings& frame_generation_settings_locked();
// Whether frame generation may run DLSS-G (on, provider auto or dlss): Streamline is initialised with DLSS-G, Reflex and
// PCL before the instance.
[[nodiscard]] bool frame_generation_wants_dlss();
// Whether anything of this is asked for (frame generation, or Reflex alone): the upscaler's context additions are made
// for it.
[[nodiscard]] bool frame_generation_wants_context();
// The device's additions for FSR's frame generation (its present and image-acquire queues), where it may run.
void frame_generation_device_wants(VkPhysicalDevice physical, ContextAdditions::DeviceWants* wants);
void frame_generation_device_made(const ContextAdditions::DeviceMade& made);
// Before the device's first swapchain (create_device): the provider chosen - FSR's takes the swapchain over.
void prepare_frame_generation(Backend& state);
// The frame's markers and Reflex's wait (GpuContext::latency_marker).
void frame_generation_marker(Backend& state, GpuContext::LatencyMarker marker);
// The upscaler's frame done (record_upscale; its images hold the depth and motion): whether this frame's history is to be
// reset (a cut) - asked before the upscaler runs - and the inputs handed over after.
[[nodiscard]] bool frame_generation_cut(Backend& state);
void framegen_inputs(Backend& state, const UpscaleFrame& frame, const rt::ViewDrawn& view);
// The present (record_present): before the swapchain image is acquired, and after the present.
void frame_generation_before_present(Backend& state);
void frame_generation_present_marker(Backend& state, bool start);
void frame_generation_after_present(Backend& state);
// (the UI's own windows - vulkan_ui_windows.cpp) another swapchain made: DLSS-G mustn't take it
void frame_generation_other_swapchain(bool making);
// The frames shown (GpuContext::frames_shown).
[[nodiscard]] uint64_t frame_generation_frames_shown();
// Released with the device objects.
void release_frame_generation(Backend& state);

// The providers (null with the reason when they can't run here).
std::unique_ptr<FrameGenProvider> make_dlssg_provider(Backend& state, std::string* why);
std::unique_ptr<FrameGenProvider> make_fsr_framegen_provider(Backend& state, std::string* why);
// FSR's swapchain owner (vulkan_framegen_fsr.cpp): the context's swapchain made through it (null in a build without FSR).
SwapchainOwner* fsr_swapchain_owner();
// The FSR frame generation's present queues made (frame_generation_device_made hands them on).
void fsr_framegen_queues(const ContextAdditions::DeviceMade& made);
// Reflex (vulkan_framegen_dlss.cpp): the mode set (once the device is made), the frame's markers, and its wait.
void reflex_marker(GpuContext::LatencyMarker marker);
void reflex_present_marker(bool start);
[[nodiscard]] bool reflex_running();
// (DLSS-G's) another swapchain made: DLSS-G unloaded while it is (Streamline would take it otherwise).
void dlssg_other_swapchain(bool making);
// The frame presented (or given up): Streamline's frame token let go - the next frame takes a new one.
void streamline_frame_done();
// Released before the context is destroyed (GpuContext::shutdown): the provider and its features.
void shutdown_frame_generation(Backend& state);

} // namespace render::vulkan
