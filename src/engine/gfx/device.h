#pragma once

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

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

extern IDirect3DDevice9* g_d3d_device;
extern IDirect3DDevice9* g_draw_device; // the same device, kept twice
extern D3DPRESENT_PARAMETERS g_present_parameters;
extern IDirect3DSurface9* g_screen_surfaces[9];
extern bool g_show_window_flag;
extern bool g_stream_offset_supported;
extern IDirect3DTexture9* g_screen_textures[9];
extern uint32_t g_loading_bar_timer; // the timer's id (timeSetEvent)
extern float g_display_factor_x;     // 640x480 -> back buffer scale

extern float g_display_factor_y;
extern int g_screen_offset_x;
extern int g_screen_offset_y;
extern IDirect3DSurface9* g_z_buffer_surface;
extern IDirect3DSurface9* g_back_buffer;
extern IDirect3DSurface9* g_back_depth;
extern bool g_border_addressing_supported;
extern bool g_blend_factor_supported;
extern uint32_t g_max_anisotropy;
extern bool g_antialiasing;
extern uint8_t g_set_frequency_allowed;
extern int g_resolution_setting;
extern IDirect3D9* g_direct_3d;
extern HWND g_game_window;
} // namespace render
