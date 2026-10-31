#pragma once

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

// The Direct3D 9 device (Nx engine, PC).
namespace Nx {

// 0x004D8650: create Direct3D and the device - full screen (or windowed, PartyMod's Windowed setting) at the
// resolution setting's size, trying smaller ones and fewer features until it works (exits if nothing does) -
// and set up the 640x480 -> back buffer conversion. With PartyMod's changes: its resolution in place of
// 640x480, the window left alone (not maximised, moved or cursor-hidden - PartyMod's SDL window does that).
void InitDirect3D();

// 0x004D7C80: create the device at width x height (into 0x007D69E0) - hardware vertex processing if
// `hardwareVertices` (else software), 2x multisampling if `multisample` - and note the adapter (a Radeon 8500;
// an Intel 915G with Realtek audio). Returns the HRESULT.
HRESULT __cdecl CreateDevice(int width, int height, bool hardwareVertices, bool multisample);
// 0x004D7C20: DirectSoundEnumerateA's callback - keeps each device's description (0x00786AC8). Returns TRUE.
BOOL CALLBACK SoundDeviceFound(GUID* guid, const char* description, const char* module, void* context);
// 0x004D8420: the default-pool objects - the dynamic buffers, nine screen-copy render targets (full size to a
// sixteenth), a depth surface, a 256x256 target for depth textures - then the device's capabilities and the
// render state sent to it.
void __cdecl CreateRenderTargets();
// 0x004D7AF0: release them (before a reset).
void __cdecl ReleaseRenderTargets();
// 0x004D7B60: the gamma (Settings\Gamma R, G, B in the registry; 0.25 each without) applied.
void __cdecl LoadGamma();
// 0x004D89C0: the disc read error screen - never returns.
[[noreturn]] void __cdecl ShowDiscError();

// 0x0044F870: the device's SetRenderState (a D3DRENDERSTATETYPE and its value). Returns the HRESULT.
long __cdecl SetRenderState(int state, uint32_t value);
// 0x0044F890: the device's Clear (rectangles or the whole viewport; D3DCLEAR_* flags). Returns the HRESULT.
long __cdecl ClearDevice(uint32_t rectCount, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);

extern IDirect3DDevice9* g_d3dDevice;
extern IDirect3DDevice9* g_drawDevice; // the same device, kept twice
extern D3DPRESENT_PARAMETERS g_presentParameters;
extern IDirect3DSurface9* g_screenSurfaces[9];
extern bool g_showWindowFlag;
extern bool g_streamOffsetSupported;
extern IDirect3DTexture9* g_screenTextures[9];
extern uint32_t g_loadingBarTimer; // the timer's id (timeSetEvent)
extern float g_screenScaleX; // 640x480 -> back buffer scale

extern float g_screenScaleY;
extern int g_screenOffsetX;
extern int g_screenOffsetY;
extern IDirect3DSurface9* g_depthSurface;
extern IDirect3DSurface9* g_backBuffer;
extern IDirect3DSurface9* g_backDepth;
extern bool g_borderAddressingSupported;
extern bool g_blendFactorSupported;
extern uint32_t g_maxAnisotropy;
extern bool g_antialiasing;
extern uint8_t g_setFrequencyAllowed;
extern int g_resolutionSetting;
extern IDirect3D9* g_direct3D;
extern HWND g_gameWindow;
} // namespace Nx
