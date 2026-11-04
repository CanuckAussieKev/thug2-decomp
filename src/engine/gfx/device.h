#pragma once

#include <cstdint>

// The Direct3D 9 device (Nx engine, PC): what the rest of the engine calls. Its Direct3D side - the device made,
// its present parameters - is in engine/gfx/device_d3d.h.
namespace render {

// 0x004D8650: create Direct3D and the device - full screen (or windowed, PartyMod's Windowed setting) at the
// resolution setting's size, trying smaller ones and fewer features until it works (exits if nothing does) -
// and set up the 640x480 -> back buffer conversion. With PartyMod's changes: its resolution in place of
// 640x480, the window left alone (not maximised, moved or cursor-hidden - PartyMod's SDL window does that).
void init_direct_3d();

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

// 0x0044F870: the device's SetRenderState (a D3DRENDERSTATETYPE and its value). Returns the HRESULT.
long set_render_state(int state, uint32_t value);
// 0x0044F890: the device's Clear (rectangles or the whole viewport; D3DCLEAR_* flags). Returns the HRESULT.
long clear_device(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);

// The back buffer's width the device was made with (its present parameters': engine/gfx/device_d3d.h).
uint32_t presented_width();
// Whether the screen copies (create_render_targets' render-target textures) were made: their first surface is set.
bool screen_copies_made();
inline constexpr bool kShowWindowFlag{false}; // (the original showed the window maximised / minimised by it)

} // namespace render
