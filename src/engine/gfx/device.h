#pragma once

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <array>

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
HRESULT create_device(int width, int height, bool hardware_vertices, bool multisample);
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
IDirect3DDevice9* d3d_device();
void set_d3d_device(IDirect3DDevice9* value);
D3DPRESENT_PARAMETERS& present_parameters();
bool stream_offset_supported();
uint32_t loading_bar_timer();
void set_loading_bar_timer(uint32_t value);
float display_factor_x();
void set_display_factor_x(float value);
IDirect3DDevice9* draw_device();
std::array<IDirect3DTexture9*, 9>& screen_textures();
uint8_t frequency_setting_allowed();
int resolution_setting();
void set_resolution_setting(int value);
bool antialiasing();
void set_antialiasing(bool value);
int& screen_offset_y();
int& screen_offset_x();
float display_factor_y();
void set_display_factor_y(float value);
IDirect3DSurface9* z_buffer_surface();
std::array<IDirect3DSurface9*, 9>& screen_surfaces();
IDirect3DSurface9* back_depth();
IDirect3DSurface9* back_buffer();
bool blend_factor_supported();
bool border_addressing_supported();
uint32_t max_anisotropy();
HWND game_window();
void set_game_window(HWND value);
IDirect3D9* direct_3d();
void set_direct_3d(IDirect3D9* value);
inline constexpr bool kShowWindowFlag{false}; // (the original showed the window maximised / minimised by it)

} // namespace render
