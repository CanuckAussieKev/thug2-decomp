#pragma once

#include <cstdint>

#include "base/maths/rect.h"
#include "engine/gfx/gpu.h"

// The Direct3D 9 device (Nx engine, PC): what the rest of the engine calls. The device itself is made through the
// GpuContext (engine/gfx/gpu_context.h).
namespace render {

// 0x004D8650: create Direct3D and the device - full screen (or windowed, PartyMod's Windowed setting) at the
// resolution setting's size, trying smaller ones and fewer features until it works (exits if nothing does) -
// and set up the 640x480 -> back buffer conversion. With PartyMod's changes: its resolution in place of
// 640x480, the window left alone (not maximised, moved or cursor-hidden - PartyMod's SDL window does that).
void init_direct_3d();

// 0x004D7C80: create the device at the size - hardware vertex processing if `hardware_vertices` (else software), 2x
// multisampling if `multisample` (the msaa option's count and quality when set) - and note the adapter (a Radeon 8500;
// an Intel 915G with Realtek audio). Returns the HRESULT.
long create_device(maths::PixelSize size, bool hardware_vertices, bool multisample);
// 0x004D7C20: told each sound output device's description (audio::Device::list_outputs; DirectSoundEnumerateA's
// callback) - keeps it (0x00786AC8).
void sound_device_found(const char* description);

// 0x004D8420: the default-pool objects - the dynamic buffers, nine screen-copy render targets (full size to a
// sixteenth), a depth surface, a 256x256 target for depth textures - then the device's capabilities and the
// render state sent to it.
void create_render_targets();
// 0x004D7AF0: release them (before a reset).
void release_render_targets();
// 0x004D7B60: the gamma (Settings\Gamma R, G, B in the registry; 0.25 each without) applied.
void load_gamma();
// 0x004D89C0: the disc read error screen - never returns.
[[noreturn]] void show_disc_error();

// 0x0044F870: the device's SetRenderState (a D3DRENDERSTATETYPE and its value), through the Gpu (engine/gfx/gpu.h).
// Returns the HRESULT.
long set_render_state(GpuRenderState state, uint32_t value);
long set_render_state(int state, uint32_t value);
// 0x0044F890: the device's Clear (rectangles or the whole viewport; D3DCLEAR_* flags), through the Gpu. Returns the
// HRESULT.
long clear_device(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);

// The back buffer's size the device was made with (its settings', gpu_context.h).
uint32_t presented_width();
uint32_t presented_height();
// Whether the device has been made (and not yet released): the Gpu may be called.
bool device_made();
// The default-pool surfaces the frame is drawn through (create_render_targets makes them): screen copy `index`
// (0-8: full size to a sixteenth; render-target textures), the depth surface they are drawn with, and the back buffer
// with its depth. As handles: the caller sets them with Gpu::set_render_target / set_depth_surface or binds the texture.
GpuTexture* screen_copy_texture(int index);
GpuSurface* scene_depth_surface();
GpuSurface* back_buffer_surface();
GpuSurface* back_depth_surface();
// Whether the screen copies (create_render_targets' render-target textures) were made: their first surface is set.
bool screen_copies_made();

// NOT ORIGINAL - the game view (engine/gfx/game_view.h; its frame: render_pc/game_view_frame.h): the frame's targets
// above made again at the picture's size - the picture (a render-target texture, its level the back buffer), its depth,
// the screen copies and their depth - in the default pool (released with the device's, before a reset). make: made at
// `width` x `height` (kept when they already are; false when one can't be made - nothing kept). While in use (from
// use_view_targets(true) to use_view_targets(false)) the frame's targets and presented_width / height are the picture's.
bool make_view_targets(uint32_t width, uint32_t height);
void release_view_targets();
void use_view_targets(bool on);
bool view_targets_in_use();
GpuTexture* view_picture();
inline constexpr bool kShowWindowFlag{false}; // (the original showed the window maximised / minimised by it)

} // namespace render

namespace os_layer {

// 0x005F55E0 (cdecl, pc_d3d_resources.cpp): the buffers, the queries, the device and Direct3D released.
void release_direct_3d();

} // namespace os_layer
