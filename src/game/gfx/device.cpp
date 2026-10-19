// Direct3D 9 start-up, with PartyMod's window and resolution changes built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/device.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/gfx/render_state.h"
#include "game/gfx/text.h"
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
    ResetRenderState();
    CreateRenderTargets();
    // Originally from PartyMod Fixes (its jump at 0x004D8989): the cursor is left alone. The original hid it when full screen:
    //   if (!pp.Windowed) while (ShowCursor(FALSE) > 0) {}
    LoadGamma();
    GameVariable<void*>(0x00786D90) = GameFunction<void*(__cdecl*)()>(0x005BA1D0)(); // the debug font (none on PC)
    GameFunction<void(__cdecl*)()>(0x005BE7C0)();
}

namespace {

// The default-pool objects (released before a device reset, made again after).
IDirect3DTexture9** ScreenTextures() { return &GameVariable<IDirect3DTexture9*>(0x00786D1C); } // [9]
IDirect3DSurface9** ScreenSurfaces() { return &GameVariable<IDirect3DSurface9*>(0x00786D40); } // [9], their level 0
IDirect3DSurface9*& DepthSurface() { return GameVariable<IDirect3DSurface9*>(0x00786D64); }
IDirect3DSurface9*& BackBuffer() { return GameVariable<IDirect3DSurface9*>(0x00786D10); }
IDirect3DSurface9*& BackDepth() { return GameVariable<IDirect3DSurface9*>(0x00786D14); }
IDirect3DSurface9*& DepthTextureTarget() { return GameVariable<IDirect3DSurface9*>(0x00786D18); }
D3DADAPTER_IDENTIFIER9& AdapterIdentifier() { return GameVariable<D3DADAPTER_IDENTIFIER9>(0x00786630); }
// The description of the last sound device DirectSound listed.
char* SoundDeviceName() { return &GameVariable<char>(0x00786AC8); }

// The dynamic vertex and index buffers (0x005F4620 etc., not rewritten yet; cdecl).
void CreateDynamicVertexBuffer(uint32_t size) { GameFunction<void(__cdecl*)(uint32_t)>(0x005F4620)(size); }
void CreateDynamicIndexBuffer(uint32_t size) { GameFunction<void(__cdecl*)(uint32_t)>(0x005F4690)(size); }

using DirectSoundEnumerateFn = HRESULT(WINAPI*)(BOOL(CALLBACK*)(GUID*, const char*, const char*, void*), void*);

static_assert(offsetof(D3DCAPS9, PixelShaderVersion) == 0xCC && offsetof(D3DCAPS9, DevCaps2) == 0xD4 &&
                  offsetof(D3DCAPS9, TextureAddressCaps) == 0x4C && offsetof(D3DCAPS9, MaxAnisotropy) == 0x6C &&
                  offsetof(D3DCAPS9, SrcBlendCaps) == 0x2C,
              "the caps the original reads");
static_assert(sizeof(D3DADAPTER_IDENTIFIER9) == 0x44C, "zeroed as 0x113 dwords");

} // namespace

// 0x004D7C20 (not a function in Ghidra: DirectSoundEnumerateA's callback)
BOOL CALLBACK SoundDeviceFound(GUID* /*guid*/, const char* description, const char* /*module*/, void* /*context*/)
{
    size_t length = std::strlen(description);
    if (length > 200)
        std::memcpy(SoundDeviceName(), description, 199); // (strncpy: not terminated, the 200th byte stays)
    else
        std::memcpy(SoundDeviceName(), description, length + 1);
    return TRUE; // (all are listed: the last one stays)
}

// 0x004D7C80
HRESULT __cdecl CreateDevice(int width, int height, bool hardwareVertices, bool multisample)
{
    D3DPRESENT_PARAMETERS& pp = PresentParameters();
    pp.BackBufferHeight = height;
    pp.MultiSampleType = multisample ? D3DMULTISAMPLE_2_SAMPLES : D3DMULTISAMPLE_NONE;
    pp.BackBufferWidth = width;
    DWORD flags = (hardwareVertices ? D3DCREATE_HARDWARE_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING) |
                  D3DCREATE_MULTITHREADED;
    HRESULT result = Direct3D()->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window(), flags, &pp, &Device());
    D3DADAPTER_IDENTIFIER9& id = AdapterIdentifier();
    std::memset(&id, 0, sizeof(id));
    Direct3D()->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    // (through the exe's import of DirectSoundEnumerateA)
    GameVariable<DirectSoundEnumerateFn>(0x0064502C)(&SoundDeviceFound, nullptr);
    // A Radeon 8500 (0x00786AB5)...
    GameVariable<bool>(0x00786AB5) = std::strstr(id.Description, "8500") != nullptr;
    // ... and an Intel 915G with Realtek HD audio's rear output: 0x0067DA40 off.
    if (std::memcmp(SoundDeviceName(), "Realtek HD Audio rear output", 29) == 0 && id.VendorId == 0x8086 &&
        id.DeviceId == 0x2582)
        GameVariable<uint8_t>(0x0067DA40) = 0;
    return result;
}

// 0x004D7AF0
void __cdecl ReleaseRenderTargets()
{
    BackBuffer() = nullptr;
    BackDepth() = nullptr;
    for (int i = 0; i < 9; ++i) {
        ScreenSurfaces()[i]->Release(); // (the surface pointers are left as they were)
        ScreenTextures()[i]->Release();
        ScreenTextures()[i] = nullptr;
    }
    DepthSurface()->Release();
    DepthSurface() = nullptr;
    if (DepthTextureTarget()) {
        DepthTextureTarget()->Release();
        DepthTextureTarget() = nullptr;
    }
    GameFunction<void(__cdecl*)()>(0x005F4650)(); // the dynamic vertex and index buffers
    GameFunction<void(__cdecl*)()>(0x005F4670)();
    GameFunction<void(__cdecl*)()>(0x005F52D0)();
}

// 0x004D8420
void __cdecl CreateRenderTargets()
{
    CreateDynamicVertexBuffer(0x40000);
    CreateDynamicIndexBuffer(0x1000);
    // The screen copies: full size twice, then quarter, eighth and sixteenth sizes.
    static const uint32_t kDivisors[9] = {1, 1, 4, 4, 4, 8, 8, 16, 16};
    D3DPRESENT_PARAMETERS& pp = PresentParameters();
    for (int i = 0; i < 9; ++i) {
        Device()->CreateTexture(pp.BackBufferWidth / kDivisors[i], pp.BackBufferHeight / kDivisors[i], 1,
                                D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &ScreenTextures()[i], nullptr);
        ScreenTextures()[i]->GetSurfaceLevel(0, &ScreenSurfaces()[i]);
    }
    Device()->CreateDepthStencilSurface(pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat,
                                        D3DMULTISAMPLE_NONE, 0, TRUE, &DepthSurface(), nullptr);
    Device()->GetRenderTarget(0, &BackBuffer());
    Device()->GetDepthStencilSurface(&BackDepth());
    BackBuffer()->Release(); // (only the pointers are kept)
    BackDepth()->Release();
    if (GameVariable<bool>(0x00786AB9)) // depth textures
        Device()->CreateRenderTarget(256, 256, pp.BackBufferFormat, D3DMULTISAMPLE_NONE, 0, FALSE,
                                     &DepthTextureTarget(), nullptr);
    D3DCAPS9 caps;
    Device()->GetDeviceCaps(&caps);
    if (caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
        GameVariable<bool>(0x00786AB8) = true;
    if (caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET)
        GameVariable<bool>(0x00786AB6) = true;
    GameVariable<uint32_t>(0x00786ABC) = caps.MaxAnisotropy;
    if (caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER)
        GameVariable<bool>(0x00786AC0) = true;
    GameVariable<bool>(0x00786AC1) =
        (caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) && (caps.DestBlendCaps & D3DPBLENDCAPS_BLENDFACTOR);
    GameVariable<uint32_t>(0x00786D6C) = pp.BackBufferWidth;
    GameVariable<uint32_t>(0x00786D70) = pp.BackBufferHeight;
    GameVariable<uint32_t>(0x00786D74) = pp.BackBufferFormat;
    GameVariable<uint32_t>(0x00786D7C) = pp.AutoDepthStencilFormat == D3DFMT_D16 ? 16 : 32; // depth bits
    if (ScreenSurfaces()[0]) {
        D3DSURFACE_DESC desc;
        ScreenSurfaces()[0]->GetDesc(&desc);
        GameVariable<uint32_t>(0x00786D78) = desc.Format;
    }
    ApplyRenderState();
}

// 0x004D7B60
void __cdecl LoadGamma()
{
    float red = 0.25f, green = 0.25f, blue = 0.25f;
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Activision\\Tony Hawk's Underground 2\\Settings\\Gamma", 0,
                      KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD size = 4;
        RegQueryValueExA(key, "R", nullptr, nullptr, reinterpret_cast<BYTE*>(&red), &size);
        size = 4;
        RegQueryValueExA(key, "G", nullptr, nullptr, reinterpret_cast<BYTE*>(&green), &size);
        size = 4;
        RegQueryValueExA(key, "B", nullptr, nullptr, reinterpret_cast<BYTE*>(&blue), &size);
        // (the key is left open)
    }
    GameFunction<void(__cdecl*)(float, float, float)>(0x004D07B0)(red, green, blue); // the gamma ramp
}

// 0x004D89C0
void __cdecl ShowDiscError()
{
    uint32_t& timer = GameVariable<uint32_t>(0x00786D08);
    if (timer) {
        GameVariable<UINT(WINAPI*)(UINT)>(0x00645268)(timer); // timeKillEvent (the exe's import)
        timer = 0;
    }
    if (!GameVariable<IDirect3DDevice9*>(0x00786D0C))
        InitDirect3D();
    // "There's a problem with the disc you're using." / "It may be dirty or damaged." in the language
    // (0x007CEFA0: 3 French, 5 German, else English).
    const char* first;
    const char* second;
    switch (GameVariable<int>(0x007CEFA0)) {
    case 3:
        first = GameVariable<const char*>(0x0067EBD4);
        second = GameVariable<const char*>(0x0067EBD8);
        break;
    case 5:
        first = GameVariable<const char*>(0x0067EBDC);
        second = GameVariable<const char*>(0x0067EBE0);
        break;
    default:
        first = GameVariable<const char*>(0x0067EBE4);
        second = GameVariable<const char*>(0x0067EBE8);
        break;
    }
    alignas(4) uint8_t storage[sizeof(SText)];
    SText* text = reinterpret_cast<SText*>(storage);
    text->Construct(0.0f);
    text->font = GameVariable<SFont*>(0x00786D90);
    text->x = 48.0f;
    text->y = 128.0f;
    text->xscale = std::bit_cast<float>(0x3F4CCCCDu); // 0.8
    text->yscale = 1.0f;
    text->color = 0x80808080;
    std::memset(text->base, 0, 4); // (the draw list's next)
    SetTexture(1, nullptr);
    SetTexture(2, nullptr);
    SetTexture(3, nullptr);
    // Forever: the two lines, 16 apart, on a cleared screen.
    for (;;) {
        Device()->Present(nullptr, nullptr, nullptr, nullptr);
        Device()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        SetBlendMode(5);
        SetTexture(0, nullptr);
        SetState(kAddressMode0, 0x10001);
        SetState(kDepthBias, 0);
        SetState(kAlphaRef, 1);
        SetState(kZWrite, 0);
        Device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        Device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        text->y = text->y - kOffsetLines;
        text->text = first;
        text->ResetVertices();
        text->Draw();
        text->DrawVertices();
        text->y = text->y + kOffsetLines;
        text->text = second;
        text->ResetVertices();
        text->Draw();
        text->DrawVertices();
    }
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
REPLACE(0x004D7C20, Nx::SoundDeviceFound);
REPLACE(0x004D7C80, Nx::CreateDevice);
REPLACE(0x004D7AF0, Nx::ReleaseRenderTargets);
REPLACE(0x004D8420, Nx::CreateRenderTargets);
REPLACE(0x004D7B60, Nx::LoadGamma);
REPLACE(0x004D89C0, Nx::ShowDiscError);
