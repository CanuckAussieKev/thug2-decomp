// NOT ORIGINAL - the screenshot key's renderer backend (game/gfx/screenshot_capture.h): Direct3D 9.

#include "game/gfx/screenshot_capture.h"

#include <d3d9.h>

#include <cstdio>
#include <cstring>

#include "hook/game.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"

namespace Gfx::ScreenshotCapture {

namespace {

IDirect3DDevice9* Device() { return Nx::g_d3dDevice; }
// The renderer's "device lost" flag (Nx::StartRender doesn't begin a scene while it's set).
bool DeviceLostFlag() { return static_cast<uint8_t>(Nx::g_loadingScreenUp) != 0; }

Sys::Screenshot::PixelFormat Map(D3DFORMAT format)
{
    using Sys::Screenshot::PixelFormat;
    switch (format) {
    case D3DFMT_X8R8G8B8: return PixelFormat::X8R8G8B8;
    case D3DFMT_A8R8G8B8: return PixelFormat::A8R8G8B8;
    case D3DFMT_R5G6B5: return PixelFormat::R5G6B5;
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5: return PixelFormat::X1R5G5B5;
    default: return PixelFormat::Unknown;
    }
}

template <typename T>
void Release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// The copy, with the scene ended around it (StretchRect / GetRenderTargetData outside a scene, as Direct3D wants).
bool Copy(IDirect3DDevice9* device, Sys::Screenshot::Frame* out, std::string* error)
{
    IDirect3DSurface9 *back = nullptr, *resolved = nullptr, *system = nullptr;
    auto fail = [&](const char* why, HRESULT hr) {
        char text[128];
        std::snprintf(text, sizeof(text), "%s (0x%08lX)", why, static_cast<unsigned long>(hr));
        *error = text;
        Release(system);
        Release(resolved);
        Release(back);
        return false;
    };
    HRESULT hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (FAILED(hr) || !back)
        return fail("no back buffer", hr);
    D3DSURFACE_DESC desc = {};
    back->GetDesc(&desc);
    Sys::Screenshot::PixelFormat format = Map(desc.Format);
    if (format == Sys::Screenshot::PixelFormat::Unknown)
        return fail("the back buffer's format isn't one we read", static_cast<HRESULT>(desc.Format));
    IDirect3DSurface9* source = back;
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        // (multisampled - [Graphics] AntiAliasing: resolved into a plain render target first)
        hr = device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved,
                                        nullptr);
        if (FAILED(hr))
            return fail("CreateRenderTarget failed", hr);
        hr = device->StretchRect(back, nullptr, resolved, nullptr, D3DTEXF_NONE);
        if (FAILED(hr))
            return fail("StretchRect failed", hr);
        source = resolved;
    }
    hr = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &system, nullptr);
    if (FAILED(hr))
        return fail("CreateOffscreenPlainSurface failed", hr);
    hr = device->GetRenderTargetData(source, system);
    if (FAILED(hr))
        return fail("GetRenderTargetData failed", hr);
    D3DLOCKED_RECT locked = {};
    hr = system->LockRect(&locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr))
        return fail("LockRect failed", hr);
    int rowBytes = static_cast<int>(desc.Width) * Sys::Screenshot::BytesPerPixel(format);
    out->width = static_cast<int>(desc.Width);
    out->height = static_cast<int>(desc.Height);
    out->pitch = rowBytes;
    out->format = format;
    out->bytes.resize(static_cast<size_t>(rowBytes) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(out->bytes.data() + static_cast<size_t>(rowBytes) * y,
                    static_cast<const uint8_t*>(locked.pBits) + static_cast<size_t>(locked.Pitch) * y, rowBytes);
    system->UnlockRect();
    Release(system);
    Release(resolved);
    Release(back);
    return true;
}

} // namespace

bool CaptureBackBuffer(Sys::Screenshot::Frame* out, std::string* error)
{
    IDirect3DDevice9* device = Device();
    if (!device) {
        *error = "no device yet";
        return false;
    }
    if (DeviceLostFlag() || device->TestCooperativeLevel() != D3D_OK) {
        *error = "the device is lost";
        return false;
    }
    // (the main loop's scene is open here: Nx::StartRender began it, the end of the frame - 0x004B2750 - ends it)
    device->EndScene();
    bool ok = Copy(device, out, error);
    device->BeginScene();
    return ok;
}

} // namespace Gfx::ScreenshotCapture
