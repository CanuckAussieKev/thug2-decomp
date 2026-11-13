#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <volk.h>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - upscaling: the world drawn at a lower resolution and brought up to the output's by NVIDIA DLSS or AMD
// FSR (docs/design/vulkan.md "Upscaling"; [Graphics] upscaler - gpu_vulkan.h's UpscaleSettings).
//
// The frame, with an upscaler on:
//   1. The upscaler's view listener (rt::ViewOrder::kUpscale) asks render_pc for its view to be drawn smaller
//      (rt::set_view_scale: the quality's share of the width and height) and jittered: render_pc draws the world into
//      the viewport's top-left part (ViewDrawn::rect) with its projection moved by a sub-pixel offset - the Halton
//      (2, 3) sequence, ceil(8 x (output / drawn)^2) phases (FSR's and DLSS's advice), a new one each frame.
//   2. rt::view_begins: the motion vectors start (vulkan_motion.h): every pass on the view's target has the motion
//      attachment, every 3D draw writes its motion.
//   3. The effects at opaque_drawn / world_drawn (shadows, occlusion, the bounce light, reflections, fog, the sun, the
//      tone mapping) work on ViewDrawn::rect - the smaller picture - as they would on a smaller window; their histories
//      reproject through the jittered cameras (sub-pixel noise their clamps take).
//   4. world_drawn at kUpscale (after the tone mapping: the colour is the display's 0 - 1): the preparation pass
//      (compute) fills the motion where no opaque draw did from the depth and the cameras (the sky, cleared pixels),
//      copies the depth out as 32-bit floats and makes the reactive mask from what blended draws left (particles,
//      glows: how much the history must give way); the provider (DLSS / FSR) upscales the colour into an image of the
//      output's size, which is copied over ViewDrawn::output_rect. render_pc then draws the 2D at the full size.
// Images are made at the output's size (a quality change keeps them); everything is skipped - and the view not scaled -
// for a multisampled target, a split view, the editor's game-view picture.
namespace render::vulkan {

struct Backend;
struct Image;
class ContextAdditions;

// (UpscaleFrame's defaults: a 60 Hz frame, a far plane)
inline constexpr float kFrameMilliseconds = 16.7F;
inline constexpr float kFarZ = 1000.0F;

// What a provider is given for a frame (the caller holds the lock; `commands` is the stream outside any pass). The
// inputs are at (0, 0) of their images, `render` big; every image is in VK_IMAGE_LAYOUT_GENERAL.
struct UpscaleFrame {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    Image* colour = nullptr;   // RGBA16F, the display's 0 - 1 (after the tone mapping)
    Image* depth = nullptr;    // R32F: the hardware depth (0 near - 1 far; the world's projection)
    Image* motion = nullptr;   // RGBA16F: x, y the move to the pixel's place the frame before, in 0 - 1 of `render`
    Image* reactive = nullptr; // R32F: 0 - 0.9, how much the pixel is a blended draw's
    Image* output = nullptr;   // RGBA16F, `output_size` big: what is shown
    Extent render;
    Extent output_size;
    std::array<float, 2> jitter{}; // the projection's offset in pixels of `render` (x right, y down)
    bool reset = false;            // the history thrown away (the first frame, a new size)
    float sharpness = 0.0F;
    float frame_milliseconds = kFrameMilliseconds;
    float near_z = 1.0F;
    float far_z = kFarZ;
    float vertical_fov = 1.0F; // radians
    std::array<std::array<float, 4>, 4> view{};       // the world's camera (D3D's row vectors), for providers that ask
    std::array<std::array<float, 4>, 4> projection{}; // unjittered
    // Ray Reconstruction's guides (DLSS-RR; null without it): the colour before the ray-traced effects (what they
    // darken or add light to: the "albedo" the effects' light is measured against), no specular albedo (zeros), and
    // the view-space normal from the depth with roughness 1 in w.
    Image* albedo = nullptr;
    Image* specular_albedo = nullptr;
    Image* normal_roughness = nullptr;
};

// How the offset a provider is told (UpscaleFrame::jitter) is made from the projection's jitter j (pixels) and the
// pixel-centre alignment a = 0.5 (1 - drawn / output), per axis: sign x j + align x a. Each provider has its own (its
// library's convention, measured by thug2_vulkan_upscale_check against the alternatives).
struct UpscaleJitterTerms {
    std::array<float, 2> sign{1.0F, 1.0F};
    std::array<float, 2> align{0.0F, 0.0F};
};

// An upscaler (FSR, DLSS): made for a device and the largest sizes, then given frames.
class UpscaleProvider {
public:
    UpscaleProvider() = default;
    UpscaleProvider(const UpscaleProvider&) = delete;
    UpscaleProvider& operator=(const UpscaleProvider&) = delete;
    UpscaleProvider(UpscaleProvider&&) = delete;
    UpscaleProvider& operator=(UpscaleProvider&&) = delete;
    virtual ~UpscaleProvider() = default;
    [[nodiscard]] virtual const char* name() const = 0;
    // A line for the log: the library and its version.
    [[nodiscard]] virtual std::string describe() const = 0;
    // The frame upscaled into frame.output (recorded into frame.commands); false with the reason.
    virtual bool evaluate(Backend& state, const UpscaleFrame& frame, std::string* error) = 0;
    // Its device objects released (the device idle or about to be).
    virtual void release(Backend& state) = 0;
    // Its library's jitter convention (UpscaleJitterTerms).
    [[nodiscard]] virtual UpscaleJitterTerms jitter_terms() const = 0;
};

// FSR (vulkan_upscale_fsr.cpp): AMD's FidelityFX / FSR SDK's prebuilt, signed Vulkan upscaler (FSR 3.1, or FSR 4 on the
// GPUs it runs on), loaded from beside the exe. Null with the reason when it can't be made.
std::unique_ptr<UpscaleProvider> make_fsr_provider(Backend& state, Extent output, std::string* why);
// DLSS (vulkan_upscale_dlss.cpp): NVIDIA's Streamline, Super Resolution / DLAA (and Ray Reconstruction where asked:
// sl.dlss_d). Null with the reason when it can't be made (not an NVIDIA RTX device, Streamline not there or not
// initialised before the device - dlss_before_instance).
std::unique_ptr<UpscaleProvider> make_dlss_provider(Backend& state, Extent output, bool ray_reconstruction,
                                                    std::string* why);

// Streamline's additions to the instance and device (the context asks for them before it makes the instance when the
// upscaler may be DLSS); null in a build without DLSS.
[[nodiscard]] ContextAdditions* dlss_context_additions();
// Everything the upscaler set adds (Streamline's where DLSS may run, FSR's shader features): null while it is off.
// GpuContext::create_instance asks for it; a check sets it in its ContextOptions after set_upscale_settings.
[[nodiscard]] ContextAdditions* upscale_context_additions();
// Whether the device has DLSS Ray Reconstruction (after the device is made).
[[nodiscard]] bool ray_reconstruction_ready();
// Whether Ray Reconstruction denoises this frame's ray-traced effects (the effects' own denoisers are skipped then:
// their raw rays go into the frame). The caller holds the lock.
[[nodiscard]] bool ray_reconstruction_active();

// The jitter sequence: the offset (pixels, -0.5 - 0.5) of frame `index` of a `phases`-long cycle (Halton 2, 3).
[[nodiscard]] std::array<float, 2> upscale_jitter(uint32_t index, uint32_t phases);
// The cycle's length for a ratio of output to drawn (ceil(8 x ratio^2), at least 8).
[[nodiscard]] uint32_t upscale_jitter_phases(float output_over_drawn);

// The preparation pass alone (the check's): `view`'s motion attachment and depth into the frame's images. The caller
// holds the lock. False when it can't be made.
struct UpscaleImages {
    Image* colour = nullptr;
    Image* depth = nullptr;
    Image* motion = nullptr;
    Image* reactive = nullptr;
    Image* output = nullptr;
};
[[nodiscard]] UpscaleImages upscale_images();

// The work at world_drawn for `view` (the listener's; the check calls it too). False: nothing done.
bool record_upscale(Backend& state, const rt::ViewDrawn& view);
// The work at view_begins (the listener's; the check's).
void begin_upscaled_view(Backend& state, const rt::ViewDrawn& view);
// The settings while the backend's lock is held (the providers').
[[nodiscard]] const UpscaleSettings& upscale_settings_locked();
// Which provider runs (null: none yet / none).
[[nodiscard]] const char* upscale_provider_name();
[[nodiscard]] uint64_t upscale_passes();

// The usage the depth surfaces need (made samplable while an upscaler is set: the preparation pass reads the depth).
[[nodiscard]] VkImageUsageFlags upscale_depth_usage(const Backend& state, VkFormat format);
// (the check's) Other terms than the provider's own (null: its own again) - to show they do worse.
void set_upscale_jitter_terms(const UpscaleJitterTerms* terms);
// The terms the running provider uses (its own, or the check's).
[[nodiscard]] UpscaleJitterTerms upscale_jitter_terms();

// Released with the device.
void release_upscale(Backend& state);

} // namespace render::vulkan
