// Direct3D 9 start-up, with PartyMod's window and resolution changes built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/device.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>

#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/hook.h"


namespace Nx {

namespace {

constexpr float Bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kPer640 = Bits(0x3ACCCCCD); // 0x0064A474, 1 / 640
constexpr float kPer480 = Bits(0x3B088889); // 0x0064A47C, 1 / 480
constexpr float kOffsetLines = 16.0f;       // 0x00645D68

IDirect3D9*& Direct3D() { return GameVariable<IDirect3D9*>(0x007D69DC); }
IDirect3DDevice9*& Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
D3DPRESENT_PARAMETERS& PresentParameters() { return GameVariable<D3DPRESENT_PARAMETERS>(0x00786A7C); }
HWND Window() { return GameVariable<HWND>(0x007D6A28); }
// The size the device is being created at.
int& DeviceWidth() { return GameVariable<int>(0x0067EBCC); }
int& DeviceHeight() { return GameVariable<int>(0x0067EBD0); }

// Create the device at width x height (into Device(); 0x004D7C80, not rewritten yet): hardware vertex
// processing if `hardwareVertices` (else software), 2x multisampling if `multisample`. Returns the HRESULT.
HRESULT CreateDevice(int width, int height, bool hardwareVertices, bool multisample)
{
    return GameFunction<HRESULT(__cdecl*)(int, int, bool, bool)>(0x004D7C80)(width, height, hardwareVertices,
                                                                             multisample);
}

} // namespace

// 0x004D8650
void InitDirect3D()
{
    GameVariable<uint32_t>(0x00786D08) = 0;
    GameVariable<float>(0x00786D80) = 1.0f; // 640x480 -> back buffer scale and offset
    GameVariable<float>(0x00786D84) = 1.0f;
    GameVariable<int>(0x00786D88) = 0;
    GameVariable<int>(0x00786D8C) = 16;
    GameVariable<uint32_t>(0x00786D18) = 0;
    GameVariable<uint32_t>(0x00786D68) = 0;
    // Originally from PartyMod Fixes (its jump at 0x004D869C): the window is never maximised. The original:
    //   if (GameVariable<bool>(0x00786AB7)) ShowWindow(Window(), SW_MAXIMIZE);

    // (Direct3DCreate9 and MessageBoxA through the exe's imports)
    IDirect3D9* d3d = GameFunction<IDirect3D9*(WINAPI*)(UINT)>(0x005BEB14)(31); // the game's D3D_SDK_VERSION
    Direct3D() = d3d;
    if (!d3d) {
        // (the text and the caption are the wrong way round)
        GameVariable<decltype(&MessageBoxA)>(0x00645224)(nullptr, reinterpret_cast<const char*>(0x0064A938) /* "Error" */,
                                                         reinterpret_cast<const char*>(0x0064A940)
                                                         /* "Direct3D could not initialize. Please download and install DirectX 9.0 or higher." */,
                                                         MB_OK);
        return;
    }
    D3DDISPLAYMODE mode;
    if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
        return;

    D3DPRESENT_PARAMETERS& pp = PresentParameters();
    pp = {};
    pp.hDeviceWindow = Window();
    // Originally from PartyMod Fixes (its byte at 0x004D8720, written when its Windowed setting is on). The original: always full screen.
    pp.Windowed = Sys::Windowed() ? TRUE : FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    // The resolution setting (0 = the largest) picks where to start; smaller ones are tried after.
    unsigned first = 3 - GameVariable<int>(0x007D643C);
    if (first > 3)
        first = 3;
    // Originally from PartyMod Fixes (its DWords at 0x004D8776 / 0x004D8792): the smallest, 640x480, is its window's size.
    const int widths[4] = {1280, 1024, 800, Sys::WindowWidth()};
    const int heights[4] = {1024, 768, 600, Sys::WindowHeight()};
    bool multisample = GameVariable<bool>(0x007D6434);
    DeviceWidth() = widths[first];
    DeviceHeight() = heights[first];
    pp.BackBufferWidth = widths[first];
    pp.BackBufferHeight = heights[first];
    if (multisample)
        pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.FullScreen_RefreshRateInHz = 0;
    Device() = nullptr;

    // Hardware vertex processing first, multisampling (if set) first; each at every size from `first` down.
    const bool attempts[4][2] = {{true, multisample}, {false, multisample}, {true, false}, {false, false}};
    bool created = false;
    for (int attempt = 0; attempt < 4 && !created; ++attempt) {
        if (static_cast<int>(first) >= 4)
            continue;
        for (unsigned i = first; i < 4; ++i) {
            DeviceWidth() = widths[i];
            DeviceHeight() = heights[i];
            if (CreateDevice(widths[i], heights[i], attempts[attempt][0], attempts[attempt][1]) >= 0) {
                created = true;
                break;
            }
        }
    }
    if (!created)
        GameFunction<void(__cdecl*)(int)>(0x00624AFE)(0); // the game's exit()
    // Originally from PartyMod Fixes (its NOPs at 0x004D8891): the window is left where it is. The original:
    //   SetWindowPos(Window(), HWND_TOP, 0, 0, DeviceWidth(), DeviceHeight(), SWP_NOZORDER);

    IDirect3DDevice9* device = Device();
    GameVariable<IDirect3DDevice9*>(0x00786D0C) = device;
    // (the back buffer size as set by CreateDevice, unsigned)
    float scaleX = static_cast<float>(static_cast<double>(pp.BackBufferWidth) * kPer640);
    GameVariable<float>(0x00786D80) = scaleX;
    float scaleY = static_cast<float>(static_cast<double>(pp.BackBufferHeight) * kPer480);
    GameVariable<int>(0x00786D88) = 0;
    GameVariable<float>(0x00786D84) = scaleY;
    GameVariable<int>(0x00786D8C) = static_cast<int>(static_cast<int64_t>(static_cast<double>(scaleY) * kOffsetLines));

    // Whether depth textures (D24S8) can be used.
    D3DDEVICE_CREATION_PARAMETERS creation;
    device->GetCreationParameters(&creation);
    if (d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, creation.DeviceType, pp.BackBufferFormat, 0, D3DRTYPE_TEXTURE,
                               D3DFMT_D24S8) == D3D_OK)
        GameVariable<bool>(0x00786AB9) = true;
    for (uint32_t address = 0x00786D1C; address <= 0x00786D3C; address += 4)
        GameVariable<uint32_t>(address) = 0;
    GameVariable<uint32_t>(0x00786D64) = 0;
    GameFunction<void(__cdecl*)()>(0x004D7D70)(); // render state defaults
    GameFunction<void(__cdecl*)()>(0x004D8420)();
    // Originally from PartyMod Fixes (its jump at 0x004D8989): the cursor is left alone. The original hid it when full screen:
    //   if (!pp.Windowed) while (ShowCursor(FALSE) > 0) {}
    GameFunction<void(__cdecl*)()>(0x004D7B60)(); // settings from the registry
    GameVariable<void*>(0x00786D90) = GameFunction<void*(__cdecl*)()>(0x005BA1D0)(); // the debug font (none on PC)
    GameFunction<void(__cdecl*)()>(0x005BE7C0)();
}

// 0x0044F870
long __cdecl SetRenderState(int state, uint32_t value)
{
    return Device()->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
}

// 0x0044F890
long __cdecl ClearDevice(uint32_t rectCount, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    return Device()->Clear(rectCount, static_cast<const D3DRECT*>(rects), flags, color, z, stencil);
}

} // namespace Nx

REPLACE(0x004D8650, Nx::InitDirect3D);
REPLACE(0x0044F870, Nx::SetRenderState);
REPLACE(0x0044F890, Nx::ClearDevice);
