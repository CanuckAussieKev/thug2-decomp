#pragma once

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <array>
#include "engine/maths/rect.h"

// The Direct3D 9 device (Nx engine, PC).
namespace render {

// 0x004D8650: create Direct3D and the device - full screen (or windowed, PartyMod's Windowed setting) at the
// resolution setting's size, trying smaller ones and fewer features until it works (exits if nothing does) -
// and set up the 640x480 -> back buffer conversion. With PartyMod's changes: its resolution in place of
// 640x480, the window left alone (not maximised, moved or cursor-hidden - PartyMod's SDL window does that).
void init_direct_3d();

// 0x004D7C80: create the device at width x height (into 0x007D69E0) - hardware vertex processing if
// `hardware_vertices` (else software), 2x multisampling if `multisample` - and note the adapter (a Radeon 8500;
// an Intel 915G with Realtek audio). Returns the HRESULT.
HRESULT create_device(maths::PixelSize size, bool hardware_vertices, bool multisample);
// 0x004D7C20: DirectSoundEnumerateA's callback - keeps each device's description (0x00786AC8). Returns TRUE.
BOOL CALLBACK sound_device_found(GUID* guid, const char* description, const char* module, void* context);
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

// The device, what was made with it and its settings (device.cpp's record; the original's globals). The draw device
// is the same device, kept twice; the display factors scale 640x480 to the back buffer.
D3DPRESENT_PARAMETERS& present_parameters();
inline constexpr bool kShowWindowFlag{false}; // (the original showed the window maximised / minimised by it)

} // namespace render
