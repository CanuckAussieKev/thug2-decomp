// NOT ORIGINAL - Direct3D 9's render::GpuContext (d3d9_context.h): the one place that creates Direct3D, the device and
// its swap chain. Each operation is the Direct3D call (or the few calls in a row) the original's start-up made, in the
// same order; the present parameters are kept here in Direct3D's form, set from the API-neutral settings.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (the windowed setting).

#include "render_d3d9/d3d9_context.h"

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/core/no_destroy.h"
#include "engine/gfx/gpu_context.h"
#include "platform/sdl_window.h"
#include "render_d3d9/d3d9_state.h"

namespace render::d3d9 {

namespace {

constexpr int kD3dSdkVersion = 31; // the game's D3D_SDK_VERSION
static_assert(sizeof(D3DADAPTER_IDENTIFIER9::Description) == sizeof(GpuAdapterInfo::description));

class D3d9Context final : public GpuContext {
public:
    constexpr D3d9Context() = default;
    D3d9Context(const D3d9Context&) = delete;
    D3d9Context& operator=(const D3d9Context&) = delete;
    D3d9Context(D3d9Context&&) = delete;
    D3d9Context& operator=(D3d9Context&&) = delete;
    ~D3d9Context() = default;

    bool create_instance() override
    {
        IDirect3D9* d3d = Direct3DCreate9(kD3dSdkVersion);
        state().set_direct_3d(d3d);
        return d3d != nullptr;
    }

    bool adapter_ready() override
    {
        D3DDISPLAYMODE mode;
        return SUCCEEDED(state().direct_3d()->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode));
    }

    uint32_t sample_quality_levels(uint32_t samples, uint32_t colour_format, uint32_t depth_format,
                                   bool windowed) override
    {
        IDirect3D9* const d3d = state().direct_3d();
        const auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
        DWORD colour_levels = 0;
        DWORD depth_levels = 0;
        if (FAILED(d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                                   static_cast<D3DFORMAT>(colour_format), windowed ? TRUE : FALSE, type,
                                                   &colour_levels)) ||
            FAILED(d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                                   static_cast<D3DFORMAT>(depth_format), windowed ? TRUE : FALSE, type,
                                                   &depth_levels))) {
            return 0;
        }
        return std::min<uint32_t>(colour_levels, depth_levels);
    }

    void configure(const GpuSettings& settings) override
    {
        D3DPRESENT_PARAMETERS& pp = present_parameters_;
        pp = D3DPRESENT_PARAMETERS{.SwapEffect = D3DSWAPEFFECT_DISCARD}; // (cleared; its swap effect as set below)
        pp.hDeviceWindow = sys::game_window();
        // Originally from PartyMod Fixes (its byte at 0x004D8720, written when its Windowed setting is on). The original: always full screen.
        pp.Windowed = settings.windowed ? TRUE : FALSE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferCount = 1;
        pp.EnableAutoDepthStencil = TRUE;
        pp.PresentationInterval = settings.vsync ? D3DPRESENT_INTERVAL_DEFAULT : D3DPRESENT_INTERVAL_IMMEDIATE;
        pp.BackBufferFormat = static_cast<D3DFORMAT>(settings.colour_format);
        pp.AutoDepthStencilFormat = static_cast<D3DFORMAT>(settings.depth_format);
        pp.FullScreen_RefreshRateInHz = 0;
        state().set_d3d_device(nullptr);
    }

    long create_device(const GpuDeviceRequest& request) override
    {
        D3DPRESENT_PARAMETERS& pp = present_parameters_;
        pp.BackBufferHeight = request.height;
        pp.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(request.samples);
        pp.MultiSampleQuality = request.sample_quality;
        pp.BackBufferWidth = request.width;
        const DWORD flags = (request.hardware_vertices ? D3DCREATE_HARDWARE_VERTEXPROCESSING
                                                       : D3DCREATE_SOFTWARE_VERTEXPROCESSING) |
                            D3DCREATE_MULTITHREADED;
        IDirect3DDevice9* device = state().d3d_device(); // (CreateDevice's out pointer: the device's own place)
        const HRESULT result =
            state().direct_3d()->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, sys::game_window(), flags, &pp, &device);
        state().set_d3d_device(device);
        D3DADAPTER_IDENTIFIER9 id{};
        state().direct_3d()->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
        adapter_ = {};
        std::memcpy(adapter_.description.data(), id.Description, adapter_.description.size());
        adapter_.vendor_id = id.VendorId;
        adapter_.device_id = id.DeviceId;
        return result;
    }

    [[nodiscard]] GpuSettings settings() const override
    {
        const D3DPRESENT_PARAMETERS& pp = present_parameters_;
        return {.width = pp.BackBufferWidth,
                .height = pp.BackBufferHeight,
                .windowed = pp.Windowed != FALSE,
                .vsync = pp.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE,
                .samples = static_cast<uint32_t>(pp.MultiSampleType),
                .sample_quality = pp.MultiSampleQuality,
                .colour_format = static_cast<uint32_t>(pp.BackBufferFormat),
                .depth_format = static_cast<uint32_t>(pp.AutoDepthStencilFormat)};
    }

    [[nodiscard]] const GpuAdapterInfo& adapter() const override { return adapter_; }

    bool depth_textures_supported() override
    {
        D3DDEVICE_CREATION_PARAMETERS creation;
        state().d3d_device()->GetCreationParameters(&creation);
        return state().direct_3d()->CheckDeviceFormat(D3DADAPTER_DEFAULT, creation.DeviceType,
                                                      present_parameters_.BackBufferFormat, 0, D3DRTYPE_TEXTURE,
                                                      D3DFMT_D24S8) == D3D_OK;
    }

    GpuCaps caps() override
    {
        D3DCAPS9 caps;
        state().d3d_device()->GetDeviceCaps(&caps);
        return {.pixel_shaders_1_1 = caps.PixelShaderVersion >= D3DPS_VERSION(1, 1),
                .stream_offset = (caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET) != 0U,
                .max_anisotropy = caps.MaxAnisotropy,
                .min_filter_anisotropic = (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0U,
                .border_addressing = (caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER) != 0U,
                .blend_factor = ((caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0U) &&
                                ((caps.DestBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0U)};
    }

    [[nodiscard]] bool device_made() const override { return state().d3d_device() != nullptr; }

    void mark_ready() override { state().set_draw_device(state().d3d_device()); }

    [[nodiscard]] bool ready() const override { return state().draw_device() != nullptr; }

    bool reset() override { return state().d3d_device()->Reset(&present_parameters_) >= 0; }

    void shutdown() override
    {
        if (state().d3d_device()) {
            state().d3d_device()->Release();
        }
        if (state().direct_3d()) {
            state().direct_3d()->Release();
        }
        state().set_d3d_device(nullptr);
        state().set_direct_3d(nullptr);
    }

private:
    D3DPRESENT_PARAMETERS present_parameters_{}; // (zero-initialised, as the global was)
    GpuAdapterInfo adapter_{};
};

} // namespace

GpuContext& gpu_context()
{
    static core::NoDestroy<D3d9Context> objects;
    return objects.get();
}

} // namespace render::d3d9
