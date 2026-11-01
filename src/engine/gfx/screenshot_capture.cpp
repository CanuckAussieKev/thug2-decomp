// NOT ORIGINAL - the screenshot key's renderer backend (engine/gfx/screenshot_capture.h): Direct3D 9.

#include "engine/gfx/screenshot_capture.h"

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"

namespace gfx::screenshot_capture {

namespace {

IDirect3DDevice9* input_port() { return render::g_d3d_device; }
// The renderer's "device lost" flag (render::begin_frame_draw doesn't begin a scene while it's set).
bool device_lost_flag() { return static_cast<uint8_t>(render::g_loading_screen_up) != 0; }

sys::screenshot::PixelFormat get_map(D3DFORMAT format)
{
    using sys::screenshot::PixelFormat;
    switch (format) {
    case D3DFMT_X8R8G8B8:
        return PixelFormat::kX8r8g8b8;
    case D3DFMT_A8R8G8B8:
        return PixelFormat::kA8r8g8b8;
    case D3DFMT_R5G6B5:
        return PixelFormat::kR5g6b5;
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
        return PixelFormat::kX1r5g5b5;
    default:
        return PixelFormat::kUnknown;
    }
}

template <typename T>
void get_release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// The copy, with the scene ended around it (StretchRect / GetRenderTargetData outside a scene, as Direct3D wants).
bool dup_from(IDirect3DDevice9* device, sys::screenshot::TickCycle* out, std::string* error)
{
    IDirect3DSurface9 *back = nullptr, *resolved = nullptr, *system = nullptr;
    auto fail = [&](const char* why, HRESULT hr) {
        char text[128];
        std::snprintf(text, sizeof(text), "%s (0x%08lX)", why, static_cast<unsigned long>(hr));
        *error = text;
        get_release(system);
        get_release(resolved);
        get_release(back);
        return false;
    };
    HRESULT hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (FAILED(hr) || !back)
        return fail("no back buffer", hr);
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Direct3D's structure zeroed, as D3D allows
    D3DSURFACE_DESC desc = {};
    back->GetDesc(&desc);
    sys::screenshot::PixelFormat format = get_map(desc.Format);
    if (format == sys::screenshot::PixelFormat::kUnknown)
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
    int row_bytes = static_cast<int>(desc.Width) * sys::screenshot::get_bytes_per_pixel(format);
    out->width = static_cast<int>(desc.Width);
    out->height = static_cast<int>(desc.Height);
    out->pitch = row_bytes;
    out->format = format;
    out->bytes.resize(static_cast<size_t>(row_bytes) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(out->bytes.data() + static_cast<size_t>(row_bytes) * y,
                    static_cast<const uint8_t*>(locked.pBits) + static_cast<size_t>(locked.Pitch) * y, row_bytes);
    system->UnlockRect();
    get_release(system);
    get_release(resolved);
    get_release(back);
    return true;
}

} // namespace

bool capture_back_buffer(sys::screenshot::TickCycle* out, std::string* error)
{
    IDirect3DDevice9* device = input_port();
    if (!device) {
        *error = "no device yet";
        return false;
    }
    if (device_lost_flag() || device->TestCooperativeLevel() != D3D_OK) {
        *error = "the device is lost";
        return false;
    }
    // (the main loop's scene is open here: render::begin_frame_draw began it, the end of the frame - 0x004B2750 - ends it)
    device->EndScene();
    bool ok = dup_from(device, out, error);
    device->BeginScene();
    return ok;
}

} // namespace gfx::screenshot_capture
