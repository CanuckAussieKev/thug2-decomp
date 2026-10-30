// Direct3D 9 start-up, with PartyMod's window and resolution changes built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/device.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/stubs.h"
#include "game/gfx/graphics_options.h"
#include "game/gfx/pc_gamma.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/text.h"
#include "game/sys/platform_d3d.h"
#include "game/sys/sdl_window.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/sys/game_config.h"


namespace Nx {

GAME_GLOBAL(IDirect3DDevice9*, g_d3dDevice, 0x007D69E0);
GAME_GLOBAL(D3DPRESENT_PARAMETERS, g_presentParameters, 0x00786A7C);
GAME_GLOBAL(bool, g_streamOffsetSupported, 0x00786AB6);
GAME_GLOBAL(bool, g_showWindowFlag, 0x00786AB7); // (the original showed the window maximised / minimised by it)
GAME_GLOBAL(uint32_t, g_loadingBarTimer, 0x00786D08); // the timer's id (timeSetEvent)
GAME_GLOBAL(float, g_screenScaleX, 0x00786D80); // 640x480 -> back buffer scale
GAME_GLOBAL(IDirect3DDevice9*, g_drawDevice, 0x00786D0C); // the same device, kept twice
GAME_GLOBAL(IDirect3DTexture9*[9], g_screenTextures, 0x00786D1C);

GAME_GLOBAL(uint8_t, g_setFrequencyAllowed, 0x0067DA40, 1);
GAME_GLOBAL(int, g_resolutionSetting, 0x007D643C);
GAME_GLOBAL(bool, g_antialiasing, 0x007D6434);
GAME_GLOBAL(int, g_screenOffsetY, 0x00786D8C);
GAME_GLOBAL(int, g_screenOffsetX, 0x00786D88);
GAME_GLOBAL(float, g_screenScaleY, 0x00786D84);
GAME_GLOBAL(IDirect3DSurface9*, g_depthSurface, 0x00786D64);
GAME_GLOBAL(IDirect3DSurface9*[9], g_screenSurfaces, 0x00786D40); // their level 0
GAME_GLOBAL(IDirect3DSurface9*, g_backDepth, 0x00786D14);
GAME_GLOBAL(IDirect3DSurface9*, g_backBuffer, 0x00786D10);
GAME_GLOBAL(bool, g_blendFactorSupported, 0x00786AC1);
GAME_GLOBAL(bool, g_borderAddressingSupported, 0x00786AC0);
GAME_GLOBAL(uint32_t, g_maxAnisotropy, 0x00786ABC);
GAME_GLOBAL(HWND, g_gameWindow, 0x007D6A28);
GAME_GLOBAL(IDirect3D9*, g_direct3D, 0x007D69DC);
namespace {

constexpr float Bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kPer640 = Bits(0x3ACCCCCD); // 0x0064A474, 1 / 640
constexpr float kPer480 = Bits(0x3B088889); // 0x0064A47C, 1 / 480
constexpr float kOffsetLines = 16.0f;       // 0x00645D68

GAME_GLOBAL(int, g_deviceWidth, 0x0067EBCC, 640);
GAME_GLOBAL(int, g_deviceHeight, 0x0067EBD0, 480);
GAME_GLOBAL_UNCHECKED(const char*, g_discErrorFrench1, 0x0067EBD4, "Le disque utilis\xE9 pr\xE9sente une anomalie.");
GAME_GLOBAL_UNCHECKED(const char*, g_discErrorFrench2, 0x0067EBD8, "Il est peut-\xEAtre sale ou endommag\xE9.");
GAME_GLOBAL_UNCHECKED(const char*, g_discErrorGerman1, 0x0067EBDC, "Bei der benutzten CD ist ein Problem aufgetreten.");
GAME_GLOBAL_UNCHECKED(const char*, g_discErrorGerman2, 0x0067EBE0, "M\xF6glicherweise ist sie verschmutzt oder besch\xE4" "digt.");
GAME_GLOBAL_UNCHECKED(const char*, g_discErrorEnglish1, 0x0067EBE4, "There's a problem with the disc you're using.");
GAME_GLOBAL_UNCHECKED(const char*, g_discErrorEnglish2, 0x0067EBE8, "It may be dirty or damaged.");
GAME_GLOBAL(D3DADAPTER_IDENTIFIER9, g_adapterIdentifier, 0x00786630);
GAME_GLOBAL(bool, g_depthTexturesSupported, 0x00786AB9);
GAME_GLOBAL(char[200], g_soundDeviceName, 0x00786AC8); // the description of the last sound device DirectSound listed
GAME_GLOBAL(IDirect3DSurface9*, g_depthTextureTarget, 0x00786D18);
GAME_GLOBAL(uint32_t, g_backBufferFormat, 0x00786D74);
GAME_GLOBAL(uint32_t, g_screenSurfaceFormat, 0x00786D78);
GAME_GLOBAL(uint32_t, g_depthBits, 0x00786D7C);
GAME_GLOBAL(SFont*, g_debugFont, 0x00786D90);

IDirect3D9*& Direct3D() { return g_direct3D; }
IDirect3DDevice9*& Device() { return g_d3dDevice; }
D3DPRESENT_PARAMETERS& PresentParameters() { return g_presentParameters; }
HWND Window() { return g_gameWindow; }
// The size the device is being created at.
int& DeviceWidth() { return g_deviceWidth; }
int& DeviceHeight() { return g_deviceHeight; }

} // namespace

// 0x004D8650
void InitDirect3D()
{
    g_loadingBarTimer = 0;
    g_screenScaleX = 1.0f; // 640x480 -> back buffer scale and offset
    g_screenScaleY = 1.0f;
    g_screenOffsetX = 0;
    g_screenOffsetY = 16;
    g_depthTextureTarget = 0;
    reinterpret_cast<uint32_t&>(g_screenCopyIndex) = 0;
    // Originally from PartyMod Fixes (its jump at 0x004D869C): the window is never maximised. The original:
    //   if (g_showWindowFlag) ShowWindow(Window(), SW_MAXIMIZE);

    // (Direct3DCreate9 and MessageBoxA through the exe's imports)
    IDirect3D9* d3d = GameFunction<IDirect3D9*(WINAPI*)(UINT)>(0x005BEB14)(31); // the game's D3D_SDK_VERSION
    Direct3D() = d3d;
    if (!d3d) {
        // (the text and the caption are the wrong way round)
        GameVariable<decltype(&MessageBoxA)>(0x00645224)(nullptr, GAME_TEXT(0x0064A938, "Error") /* "Error" */,
                                                         GAME_TEXT(0x0064A940, "Direct3D could not initialize. Please download and install DirectX 9.0 or higher.")
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
    unsigned first = 3 - g_resolutionSetting;
    if (first > 3)
        first = 3;
    // Originally from PartyMod Fixes (its DWords at 0x004D8776 / 0x004D8792): the smallest, 640x480, is its window's size.
    const int widths[4] = {1280, 1024, 800, Sys::WindowWidth()};
    const int heights[4] = {1024, 768, 600, Sys::WindowHeight()};
    bool multisample = g_antialiasing;
    // NOT ORIGINAL - [Graphics] msaa (game/gfx/graphics_options.h): its sample count, whatever AntiAliasing says.
    GraphicsOptions::ChooseMultisample(d3d, D3DFMT_X8R8G8B8, D3DFMT_D24X8, Sys::Windowed());
    if (GraphicsOptions::ChosenSamples())
        multisample = true;
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
    g_drawDevice = device;
    // (the back buffer size as set by CreateDevice, unsigned)
    float scaleX = static_cast<float>(static_cast<double>(pp.BackBufferWidth) * kPer640);
    g_screenScaleX = scaleX;
    float scaleY = static_cast<float>(static_cast<double>(pp.BackBufferHeight) * kPer480);
    g_screenOffsetX = 0;
    g_screenScaleY = scaleY;
    g_screenOffsetY = static_cast<int>(static_cast<int64_t>(static_cast<double>(scaleY) * kOffsetLines));

    // Whether depth textures (D24S8) can be used.
    D3DDEVICE_CREATION_PARAMETERS creation;
    device->GetCreationParameters(&creation);
    if (d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, creation.DeviceType, pp.BackBufferFormat, 0, D3DRTYPE_TEXTURE,
                               D3DFMT_D24S8) == D3D_OK)
        g_depthTexturesSupported = true;
    for (IDirect3DTexture9*& texture : g_screenTextures)
        texture = nullptr;
    g_depthSurface = 0;
    ResetRenderState();
    CreateRenderTargets();
    // Originally from PartyMod Fixes (its jump at 0x004D8989): the cursor is left alone. The original hid it when full screen:
    //   if (!pp.Windowed) while (ShowCursor(FALSE) > 0) {}
    LoadGamma();
    // the debug font (none on PC: the original calls the shared `return 0` stub)
    g_debugFont = reinterpret_cast<SFont*>(static_cast<uintptr_t>(Stubs::ReturnZero()));
    Tmr::SetFlags();
}

namespace {

// The default-pool objects (released before a device reset, made again after).
IDirect3DTexture9** ScreenTextures() { return g_screenTextures; } // [9]
IDirect3DSurface9** ScreenSurfaces() { return g_screenSurfaces; } // [9], their level 0
IDirect3DSurface9*& DepthSurface() { return g_depthSurface; }
IDirect3DSurface9*& BackBuffer() { return g_backBuffer; }
IDirect3DSurface9*& BackDepth() { return g_backDepth; }
IDirect3DSurface9*& DepthTextureTarget() { return g_depthTextureTarget; }
D3DADAPTER_IDENTIFIER9& AdapterIdentifier() { return g_adapterIdentifier; }
// The description of the last sound device DirectSound listed.
char* SoundDeviceName() { return g_soundDeviceName; }

// The dynamic vertex buffer and the quads' index buffer (the platform layer's).
void CreateDynamicVertexBuffer(uint32_t size) { Platform::CreateDynamicVertexBuffer(size); }
void CreateDynamicIndexBuffer(uint32_t quads) { Platform::CreateQuadIndexBuffer(quads); }

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
    // NOT ORIGINAL - [Graphics] msaa (game/gfx/graphics_options.h): the chosen sample count and quality instead of 2.
    if (GraphicsOptions::ChosenSamples()) {
        pp.MultiSampleType = multisample ? static_cast<D3DMULTISAMPLE_TYPE>(GraphicsOptions::ChosenSamples())
                                         : D3DMULTISAMPLE_NONE;
        pp.MultiSampleQuality = multisample ? GraphicsOptions::ChosenQuality() : 0;
    }
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
    g_radeon8500 = std::strstr(id.Description, "8500") != nullptr;
    // ... and an Intel 915G with Realtek HD audio's rear output: 0x0067DA40 off.
    if (std::memcmp(SoundDeviceName(), "Realtek HD Audio rear output", 29) == 0 && id.VendorId == 0x8086 &&
        id.DeviceId == 0x2582)
        g_setFrequencyAllowed = 0;
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
    Platform::ReleaseDynamicVertexBuffer();
    Platform::ReleaseQuadIndexBuffer();
    Platform::ReleaseOcclusionQueries();
    GraphicsOptions::ReleaseSceneTarget(); // (NOT ORIGINAL: [Graphics] msaa's scene surface)
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
    if (g_depthTexturesSupported) // depth textures
        Device()->CreateRenderTarget(256, 256, pp.BackBufferFormat, D3DMULTISAMPLE_NONE, 0, FALSE,
                                     &DepthTextureTarget(), nullptr);
    D3DCAPS9 caps;
    Device()->GetDeviceCaps(&caps);
    if (caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
        g_shadersAvailable = true;
    if (caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET)
        g_streamOffsetSupported = true;
    g_maxAnisotropy = caps.MaxAnisotropy;
    // NOT ORIGINAL - [Graphics] anisotropy and msaa (game/gfx/graphics_options.h): the anisotropy this device can
    // give, and the multisampled scene surface for the screen copies.
    GraphicsOptions::ActiveAnisotropy() = GraphicsOptions::DeviceAnisotropy(
        GraphicsOptions::Get().anisotropy, (caps.TextureFilterCaps & 0x400) != 0 /* D3DPTFILTERCAPS_MINFANISOTROPY */,
        caps.MaxAnisotropy);
    GraphicsOptions::CreateSceneTarget();
    if (caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER)
        g_borderAddressingSupported = true;
    g_blendFactorSupported =
        (caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) && (caps.DestBlendCaps & D3DPBLENDCAPS_BLENDFACTOR);
    reinterpret_cast<uint32_t&>(g_backBufferWidth) = pp.BackBufferWidth;
    reinterpret_cast<uint32_t&>(g_backBufferHeight) = pp.BackBufferHeight;
    g_backBufferFormat = pp.BackBufferFormat;
    g_depthBits = pp.AutoDepthStencilFormat == D3DFMT_D16 ? 16 : 32; // depth bits
    if (ScreenSurfaces()[0]) {
        D3DSURFACE_DESC desc;
        ScreenSurfaces()[0]->GetDesc(&desc);
        g_screenSurfaceFormat = desc.Format;
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
    Nx::SetDeviceGamma(red, green, blue); // the gamma ramp
}

// 0x004D89C0
void __cdecl ShowDiscError()
{
    uint32_t& timer = g_loadingBarTimer;
    if (timer) {
        GameVariable<UINT(WINAPI*)(UINT)>(0x00645268)(timer); // timeKillEvent (the exe's import)
        timer = 0;
    }
    if (!g_drawDevice)
        InitDirect3D();
    // "There's a problem with the disc you're using." / "It may be dirty or damaged." in the language
    // (0x007CEFA0: 3 French, 5 German, else English).
    const char* first;
    const char* second;
    switch (GameConfig::g_language) {
    case 3:
        first = g_discErrorFrench1;
        second = g_discErrorFrench2;
        break;
    case 5:
        first = g_discErrorGerman1;
        second = g_discErrorGerman2;
        break;
    default:
        first = g_discErrorEnglish1;
        second = g_discErrorEnglish2;
        break;
    }
    alignas(4) uint8_t storage[sizeof(SText)];
    SText* text = reinterpret_cast<SText*>(storage);
    text->Construct(0.0f);
    text->font = g_debugFont;
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
