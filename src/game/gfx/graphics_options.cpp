// NOT ORIGINAL - image-quality options (see graphics_options.h).

#include "game/gfx/graphics_options.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdlib>

#include "game/sys/config.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Nx::GraphicsOptions {

namespace {

Options s_options;
int s_activeAnisotropy = 0;
int s_samples = 0;
DWORD s_quality = 0;

// The scene surface and its depth (default pool), and the full-size copy last drawn through them.
IDirect3DSurface9* s_sceneColour = nullptr;
IDirect3DSurface9* s_sceneDepth = nullptr;
IDirect3DTexture9* s_drawnFor = nullptr;
bool s_unresolved = false;

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
D3DPRESENT_PARAMETERS& PresentParameters() { return GameVariable<D3DPRESENT_PARAMETERS>(0x00786A7C); }
IDirect3DTexture9* ScreenCopy(int index) { return (&GameVariable<IDirect3DTexture9*>(0x00786D1C))[index]; }

constexpr float kMinScale = 0.25f;
constexpr float kMaxScale = 8.0f;

struct SupportQuery {
    IDirect3D9* d3d;
    D3DFORMAT colour, depth;
    BOOL windowed;
    DWORD quality; // out: the quality levels both formats have
};

bool Supported(int samples, void* context)
{
    auto* q = static_cast<SupportQuery*>(context);
    auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
    DWORD colourLevels = 0, depthLevels = 0;
    if (FAILED(q->d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, q->colour, q->windowed, type,
                                                  &colourLevels)) ||
        FAILED(q->d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, q->depth, q->windowed, type,
                                                  &depthLevels)))
        return false;
    DWORD levels = colourLevels < depthLevels ? colourLevels : depthLevels;
    if (levels == 0)
        return false;
    q->quality = levels;
    return true;
}

template <typename T>
void Release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool InUse() { return s_samples > 0 && s_sceneColour && s_sceneDepth; }

} // namespace

Options& Get() { return s_options; }

void Load()
{
    s_options.anisotropy = ParseAnisotropy(Config::GetInt(Config::kGraphics, "anisotropy", 0));
    s_options.msaa = ParseMultisample(Config::GetInt(Config::kGraphics, "msaa", 0));
    char text[32];
    Config::GetString(Config::kGraphics, "draw_distance_scale", "1", text, sizeof(text));
    s_options.drawDistanceScale = ParseScale(text);
    Config::GetString(Config::kGraphics, "lod_distance_scale", "1", text, sizeof(text));
    s_options.lodDistanceScale = ParseScale(text);
    if (s_options.anisotropy || s_options.msaa || s_options.drawDistanceScale != 1.0f ||
        s_options.lodDistanceScale != 1.0f)
        Log("graphics options: anisotropy=%d msaa=%d draw_distance_scale=%.3g lod_distance_scale=%.3g",
                   s_options.anisotropy, s_options.msaa, s_options.drawDistanceScale, s_options.lodDistanceScale);
}

int ParseAnisotropy(int value) { return value < 2 ? 0 : (value > 16 ? 16 : value); }

int ParseMultisample(int value) { return value < 2 ? 0 : (value > 16 ? 16 : value); }

float ParseScale(const char* text)
{
    if (!text || !*text)
        return 1.0f;
    char* end = nullptr;
    double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value) || !(value > 0.0))
        return 1.0f;
    float scale = static_cast<float>(value);
    return scale < kMinScale ? kMinScale : (scale > kMaxScale ? kMaxScale : scale);
}

int DeviceAnisotropy(int option, bool deviceCanFilter, uint32_t maxAnisotropy)
{
    if (option < 2 || !deviceCanFilter || maxAnisotropy < 2)
        return 0;
    return static_cast<uint32_t>(option) > maxAnisotropy ? static_cast<int>(maxAnisotropy) : option;
}

uint32_t MinFilter(bool linear, int anisotropy)
{
    if (!linear)
        return D3DTEXF_POINT;
    return anisotropy > 0 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR;
}

int PickSamples(int option, bool (*supported)(int samples, void* context), void* context)
{
    for (int samples = option; samples >= 2; --samples)
        if (supported(samples, context))
            return samples;
    return 0;
}

float Scaled(float distance, float scale) { return scale == 1.0f ? distance : distance * scale; }

int& ActiveAnisotropy() { return s_activeAnisotropy; }

int ChosenSamples() { return s_samples; }

uint32_t ChosenQuality() { return s_quality; }

void ChooseMultisample(IDirect3D9* d3d, uint32_t backBufferFormat, uint32_t depthFormat, bool windowed)
{
    s_samples = 0;
    s_quality = 0;
    if (!s_options.msaa || !d3d)
        return;
    SupportQuery query = {d3d, static_cast<D3DFORMAT>(backBufferFormat), static_cast<D3DFORMAT>(depthFormat),
                          windowed ? TRUE : FALSE, 0};
    s_samples = PickSamples(s_options.msaa, &Supported, &query);
    if (s_samples)
        s_quality = query.quality - 1; // (the highest)
    Log("graphics options: msaa=%d asked, %d samples (quality %lu) chosen", s_options.msaa, s_samples,
               static_cast<unsigned long>(s_quality));
}

void CreateSceneTarget()
{
    ReleaseSceneTarget();
    const D3DPRESENT_PARAMETERS& pp = PresentParameters();
    if (s_samples <= 0 || pp.MultiSampleType == D3DMULTISAMPLE_NONE)
        return; // (the device was made without: the fallbacks in InitDirect3D)
    IDirect3DDevice9* device = Device();
    HRESULT colour = device->CreateRenderTarget(pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat,
                                                pp.MultiSampleType, pp.MultiSampleQuality, FALSE, &s_sceneColour,
                                                nullptr);
    HRESULT depth = device->CreateDepthStencilSurface(pp.BackBufferWidth, pp.BackBufferHeight,
                                                      pp.AutoDepthStencilFormat, pp.MultiSampleType,
                                                      pp.MultiSampleQuality, TRUE, &s_sceneDepth, nullptr);
    if (FAILED(colour) || FAILED(depth)) {
        Log("graphics options: no multisampled scene surface (0x%08lX / 0x%08lX) - screen effects unsmoothed",
                   static_cast<unsigned long>(colour), static_cast<unsigned long>(depth));
        ReleaseSceneTarget();
    }
}

void ReleaseSceneTarget()
{
    Release(s_sceneColour);
    Release(s_sceneDepth);
    s_drawnFor = nullptr;
    s_unresolved = false;
}

bool SetSceneTarget(IDirect3DTexture9* texture)
{
    if (!InUse() || !texture || (texture != ScreenCopy(0) && texture != ScreenCopy(1)))
        return false;
    Device()->SetRenderTarget(0, s_sceneColour);
    Device()->SetDepthStencilSurface(s_sceneDepth);
    s_drawnFor = texture;
    s_unresolved = true;
    return true;
}

void ResolveSceneTarget(IDirect3DTexture9* texture)
{
    if (!InUse() || !s_unresolved || texture != s_drawnFor || !texture)
        return;
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) && surface) {
        Device()->StretchRect(s_sceneColour, nullptr, surface, nullptr, D3DTEXF_NONE);
        surface->Release();
    }
    s_unresolved = false;
}

} // namespace Nx::GraphicsOptions
