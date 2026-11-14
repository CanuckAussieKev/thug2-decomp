#pragma once

// NOT ORIGINAL - render::GpuContext: how the PC layer asks the graphics API for its instance, device and swap chain
// (docs/design/vulkan.md, V5e). render::Gpu (gpu.h) is what draws once the device exists; this is the small interface
// that makes it, resets it and reports what it can do. Direct3D 9's implementation is gpu_d3d9/d3d9_context.h (it
// makes the IDirect3D9, the device and its present parameters); a Vulkan one would make the instance, the physical and
// logical device and the swap chain here.
//
// - Settings, not present parameters: the back buffer's size, windowed, whether presenting waits for the display, the
//   multisample count and quality, and the colour and depth formats (Direct3D 9's format numbers, gpu_value::kFormat*).
//   Direct3D 9 fills its D3DPRESENT_PARAMETERS from them; Vulkan builds the swap chain's create info and the depth
//   image from them (vsync: FIFO present mode; off: MAILBOX or IMMEDIATE).
// - reset(): a lost Direct3D 9 device is reset with the present parameters it was made with (the engine's main loop
//   releases the default-pool objects first and makes them again). In Vulkan nothing is ever lost the same way: a
//   swap chain that went out of date (the window resized, a minimise, an alt-tab to another full-screen program)
//   is recreated at the stored settings, and the same release / restore steps recreate the frame's images. The
//   Direct3D 9 device-lost state is mapped by Gpu::test_cooperative_level (gpu_value::kResultDeviceLost) to
//   VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR after a present.
// - caps(): asked once the device is made (after the default-pool objects, where the original asked its device's
//   capabilities).
//
// Installed by app/ beside the Gpu; not installed, a call dereferences null.

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/gfx/gpu_handles.h"

namespace render {

// The device's presentation settings (the present parameters, API-neutral).
struct GpuSettings {
    uint32_t width{};
    uint32_t height{};
    bool windowed{};
    bool vsync{true}; // presenting waits for the display (Direct3D 9: the driver's default interval)
    // NOT ORIGINAL - [Graphics] present_mode (engine/sys/frame_pacing.h PresentMode: 0 vsync, 1 mailbox, 2 immediate):
    // the present mode wanted when vsync is off (Vulkan: where the surface offers it, else FIFO; Direct3D 9: immediate).
    uint32_t present_mode{};
    uint32_t samples{}; // multisampling: 0 none, else the sample count
    uint32_t sample_quality{};
    uint32_t colour_format{}; // gpu_value::kFormat*
    uint32_t depth_format{};
};

// What a device can do that the renderer asks (the original's capability checks).
struct GpuCaps {
    bool pixel_shaders_1_1{};     // ps.1.1 and up
    bool stream_offset{};         // vertex stream offsets that are not a multiple of the stride
    uint32_t max_anisotropy{};
    bool min_filter_anisotropic{}; // anisotropic minification
    bool border_addressing{};
    bool blend_factor{};          // the blend factor as a blend operand
};

// The adapter the device was made on (asked after the device).
inline constexpr std::size_t kGpuAdapterNameBytes = 512;
inline constexpr std::size_t kGpuDriverBytes = 64;
struct GpuAdapterInfo {
    std::array<char, kGpuAdapterNameBytes> description{};
    uint32_t vendor_id{};
    uint32_t device_id{};
    std::array<char, kGpuDriverBytes> driver{}; // the driver's version as its vendor numbers it ("" - unknown)
};

// A multisample count to ask about: the formats it must work with, and the window mode.
struct GpuSampleQuery {
    uint32_t samples{};
    uint32_t colour_format{};
    uint32_t depth_format{};
    bool windowed{};
};

// NOT ORIGINAL - a back buffer's size (set_back_buffer_size).
struct GpuBackBufferSize {
    uint32_t width{};
    uint32_t height{};
};

// A device to make: the back buffer's size, hardware or software vertex processing, the multisampling.
struct GpuDeviceRequest {
    uint32_t width{};
    uint32_t height{};
    bool hardware_vertices{};
    uint32_t samples{};
    uint32_t sample_quality{};
};

class GpuContext {
public:
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;
    GpuContext(GpuContext&&) = delete;
    GpuContext& operator=(GpuContext&&) = delete;

    // The API's instance made (Direct3D 9: Direct3DCreate9); false when the API is not there.
    virtual bool create_instance() = 0;
    // The display adapter can be asked its mode (Direct3D 9: GetAdapterDisplayMode).
    virtual bool adapter_ready() = 0;
    // The most quality levels both formats have at this many samples (0: not supported) - the sample counts to try.
    virtual uint32_t sample_quality_levels(const GpuSampleQuery& query) = 0;
    // The settings every device attempt shares (windowed, vsync, formats; the window it presents to is the platform's).
    virtual void configure(const GpuSettings& settings) = 0;
    // The device made at this size and multisampling (the settings above, also kept as the reset's), and the adapter
    // asked; the HRESULT-style result (negative: failed).
    virtual long create_device(const GpuDeviceRequest& request) = 0;
    // The settings the device was made with (the back buffer's size and formats, as it has them).
    [[nodiscard]] virtual GpuSettings settings() const = 0;
    [[nodiscard]] virtual const GpuAdapterInfo& adapter() const = 0;
    // The depth formats that can be sampled as a texture (D24S8) are supported on the device's adapter.
    virtual bool depth_textures_supported() = 0;
    virtual GpuCaps caps() = 0;
    // Whether the device has been made (and not yet released).
    [[nodiscard]] virtual bool device_made() const = 0;
    // The device used to draw has been set up (the original kept a second copy of the device after the start-up).
    virtual void mark_ready() = 0;
    [[nodiscard]] virtual bool ready() const = 0;
    // A lost device brought back at the stored settings; false if that failed (it is tried again next frame).
    virtual bool reset() = 0;
    // NOT ORIGINAL - the back buffer's size for the next reset (the editor mode's resizable window - the main loop's
    // resize: frame_loop::Hub::reset_device_if_lost); the other settings stay.
    virtual void set_back_buffer_size(const GpuBackBufferSize& size) = 0;
    // NOT ORIGINAL - [Graphics] present_mode changed while the game runs (GpuSettings::present_mode; 0 is vsync): true
    // when the device takes it now (Vulkan: its swap chain made again before the next frame); false: from the next start.
    virtual bool set_present_mode(uint32_t mode)
    {
        (void)mode;
        return false;
    }
    // NOT ORIGINAL - where the main loop is in a frame (Vulkan's frame generation and Reflex low latency -
    // gpu_vulkan/vulkan_framegen.h - time the frames by them; nothing happens without them): the simulation's start
    // (Reflex waits there for its best time to begin a frame) and end, and the draws' recording.
    enum class LatencyMarker : uint8_t { kSimulationStart, kSimulationEnd, kRenderSubmitStart, kRenderSubmitEnd };
    virtual void latency_marker(LatencyMarker marker) { (void)marker; }
    // NOT ORIGINAL - the frames handed to the display so far, the generated ones too (frame generation); 0 when no frame
    // is generated (every presented frame is a drawn one).
    [[nodiscard]] virtual uint64_t frames_shown() const { return 0; }
    // The device and then the instance given back.
    virtual void shutdown() = 0;

protected:
    GpuContext() = default;
    ~GpuContext() = default;
};

// The context app/ installed (with the Gpu).
GpuContext& gpu_context();
void install_gpu_context(GpuContext& context);

} // namespace render
