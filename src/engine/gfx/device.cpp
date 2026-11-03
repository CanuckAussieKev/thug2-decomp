// Direct3D 9 start-up, with PartyMod's window and resolution changes built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/gfx/device.h"

#include <windows.h>

#include <d3d9.h>
#include <mmsystem.h>
#include <dsound.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <iterator>

#include "engine/core/stubs.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/pc_gamma.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/text.h"
#include "platform/platform_d3d.h"
#include "platform/sdl_window.h"
#include "platform/stopwatch.h"
#include "engine/sys/game_config.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "platform/registry.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/maths/rect.h"

namespace render {

D3DPRESENT_PARAMETERS& present_parameters()
{
    static D3DPRESENT_PARAMETERS present_parameters; // (zero-initialised, as the global was)
    return present_parameters;
}

namespace {

constexpr float get_bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kPer640 = get_bits(0x3ACCCCCD); // 0x0064A474, 1 / 640
constexpr float kPer480 = get_bits(0x3B088889); // 0x0064A47C, 1 / 480
constexpr float kOffsetLines = 16.0f;           // 0x00645D68

// The disc read error screen's texts (the original's pointers to them, never changed).
constexpr const char* kDiscErrorFrench1 = "Le disque utilis\xE9 pr\xE9sente une anomalie.";
constexpr const char* kDiscErrorFrench2 = "Il est peut-\xEAtre sale ou endommag\xE9.";
constexpr const char* kDiscErrorGerman1 = "Bei der benutzten CD ist ein Problem aufgetreten.";
constexpr const char* kDiscErrorGerman2 = "M\xF6glicherweise ist sie verschmutzt oder besch\xE4"
                                          "digt.";
constexpr const char* kDiscErrorEnglish1 = "There's a problem with the disc you're using.";
constexpr const char* kDiscErrorEnglish2 = "It may be dirty or damaged.";

// NOT ORIGINAL grouping - device.cpp's state (the original's globals). Made on first use, never destroyed
// (device_state()).
struct DeviceState {
    int device_width_{640};
    int device_height_{480};
};

DeviceState& device_state()
{
    static core::NoDestroy<DeviceState> state;
    return state.get();
}

// NOT ORIGINAL grouping - the adapter: its identifier, depth textures, the back buffer's and screen's formats, the
// depth bits, the debug font (the original's globals). Made on first use, never destroyed (adapter_info()).
struct AdapterInfo {
    D3DADAPTER_IDENTIFIER9 adapter_identifier_{};
    bool depth_textures_supported_{};
    std::array<char, 200> sound_device_name_{}; // the description of the last sound device DirectSound listed
    IDirect3DSurface9* depth_texture_target_{};
    uint32_t back_buffer_format_{};
    uint32_t screen_surface_format_{};
    uint32_t depth_bits_{};
    TypefaceData* debug_font_{};
};

AdapterInfo& adapter_info()
{
    static core::NoDestroy<AdapterInfo> state;
    return state.get();
}

// The size the device is being created at.
int& device_width() { return device_state().device_width_; }
int& device_height() { return device_state().device_height_; }

} // namespace

// 0x004D8650
void init_direct_3d()
{
    auto& gfx = renderer();
    gfx.set_loading_bar_timer(0);
    gfx.set_display_factor_x(1.0f); // 640x480 -> back buffer scale and offset
    gfx.set_display_factor_y(1.0f);
    gfx.set_screen_offset_x(0);
    gfx.set_screen_offset_y(16);
    adapter_info().depth_texture_target_ = nullptr;
    renderer().set_screen_copy_index(0);
    // Originally from PartyMod Fixes (its jump at 0x004D869C): the window is never maximised. The original:
    //   if (kShowWindowFlag) ShowWindow(Pane(), SW_MAXIMIZE);

    // (Direct3DCreate9 and MessageBoxA through the exe's imports)
    IDirect3D9* d3d = Direct3DCreate9(31); // the game's D3D_SDK_VERSION
    gfx.set_direct_3d(d3d);
    if (!d3d) {
        // (the text and the caption are the wrong way round)
        MessageBoxA(nullptr, "Error" /* "Error" */,
                    "Direct3D could not initialize. Please download and install DirectX 9.0 or higher."
                    /* "Direct3D could not initialize. Please download and install DirectX 9.0 or higher." */,
                    MB_OK);
        return;
    }
    D3DDISPLAYMODE mode;
    if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
        return;

    D3DPRESENT_PARAMETERS& pp = present_parameters();
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Direct3D's structure zeroed, as D3D allows
    pp = {};
    pp.hDeviceWindow = gfx.game_window();
    // Originally from PartyMod Fixes (its byte at 0x004D8720, written when its Windowed setting is on). The original: always full screen.
    pp.Windowed = sys::get_windowed() ? TRUE : FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    // The resolution setting (0 = the largest) picks where to start; smaller ones are tried after.
    unsigned first = 3 - gfx.resolution_setting();
    if (first > 3)
        first = 3;
    // Originally from PartyMod Fixes (its DWords at 0x004D8776 / 0x004D8792): the smallest, 640x480, is its window's size.
    const std::array<int, 4> widths = {{1280, 1024, 800, sys::get_window_width()}};
    const std::array<int, 4> heights = {{1024, 768, 600, sys::get_window_height()}};
    bool multisample = gfx.antialiasing();
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): its sample count, whatever AntiAliasing says.
    graphics_options::choose_multisample(d3d, D3DFMT_X8R8G8B8, D3DFMT_D24X8, sys::get_windowed());
    if (graphics_options::chosen_samples())
        multisample = true;
    device_width() = core::at(widths, first);
    device_height() = core::at(heights, first);
    pp.BackBufferWidth = core::at(widths, first);
    pp.BackBufferHeight = core::at(heights, first);
    if (multisample)
        pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.FullScreen_RefreshRateInHz = 0;
    gfx.set_d3d_device(nullptr);

    // Hardware vertex processing first, multisampling (if set) first; each at every size from `first` down.
    const std::array<std::array<bool, 2>, 4> attempts = {{{true, multisample}, {false, multisample}, {true, false}, {false, false}}};
    bool created = false;
    for (int attempt = 0; attempt < 4 && !created; ++attempt) {
        if (static_cast<int>(first) >= 4)
            continue;
        for (unsigned i = first; i < 4; ++i) {
            device_width() = core::at(widths, i);
            device_height() = core::at(heights, i);
            if (create_device({.width = core::at(widths, i), .height = core::at(heights, i)}, core::at(core::at(attempts, attempt), 0), core::at(core::at(attempts, attempt), 1)) >= 0) {
                created = true;
                break;
            }
        }
    }
    if (!created)
        crt::exit_fatal(0); // the game's exit() (without the static owners' teardown)
    // Originally from PartyMod Fixes (its NOPs at 0x004D8891): the window is left where it is. The original:
    //   SetWindowPos(Pane(), HWND_TOP, 0, 0, device_width(), device_height(), SWP_NOZORDER);

    IDirect3DDevice9* device = gfx.d3d_device();
    gfx.set_draw_device(device);
    // (the back buffer size as set by CreateDevice, unsigned)
    auto scale_x = static_cast<float>(static_cast<double>(pp.BackBufferWidth) * kPer640);
    gfx.set_display_factor_x(scale_x);
    auto scale_y = static_cast<float>(static_cast<double>(pp.BackBufferHeight) * kPer480);
    gfx.set_screen_offset_x(0);
    gfx.set_display_factor_y(scale_y);
    gfx.set_screen_offset_y(static_cast<int>(static_cast<int64_t>(static_cast<double>(scale_y) * kOffsetLines)));

    // Whether depth textures (D24S8) can be used.
    D3DDEVICE_CREATION_PARAMETERS creation;
    device->GetCreationParameters(&creation);
    if (d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, creation.DeviceType, pp.BackBufferFormat, 0, D3DRTYPE_TEXTURE,
                               D3DFMT_D24S8) == D3D_OK)
        adapter_info().depth_textures_supported_ = true;
    for (IDirect3DTexture9*& texture : gfx.screen_textures())
        texture = nullptr;
    gfx.set_z_buffer_surface(nullptr);
    reset_render_state();
    create_render_targets();
    // Originally from PartyMod Fixes (its jump at 0x004D8989): the cursor is left alone. The original hid it when full screen:
    //   if (!pp.Windowed) while (ShowCursor(FALSE) > 0) {}
    load_gamma();
    // the debug font (none on PC: the original calls the shared `return 0` stub)
    (void)stubs::return_zero();
    adapter_info().debug_font_ = nullptr;
    frame_clock::change_bits();
}

namespace {

// The default-pool objects (released before a device reset, made again after).

IDirect3DSurface9*& depth_texture_target() { return adapter_info().depth_texture_target_; }
D3DADAPTER_IDENTIFIER9& adapter_identifier() { return adapter_info().adapter_identifier_; }
// The description of the last sound device DirectSound listed.
std::array<char, 200>& sound_device_name() { return adapter_info().sound_device_name_; }

// The dynamic vertex buffer and the quads' index buffer (the platform layer's).
void create_dynamic_vertex_buffer(uint32_t size) { os_layer::create_dynamic_vertex_buffer(size); }
void create_dynamic_index_buffer(uint32_t quads) { os_layer::create_quad_index_buffer(quads); }

using DirectSoundEnumerateFn = HRESULT(WINAPI*)(BOOL(CALLBACK*)(GUID*, const char*, const char*, void*), void*);

static_assert(offsetof(D3DCAPS9, PixelShaderVersion) == 0xCC && offsetof(D3DCAPS9, DevCaps2) == 0xD4 &&
                  offsetof(D3DCAPS9, TextureAddressCaps) == 0x4C && offsetof(D3DCAPS9, MaxAnisotropy) == 0x6C &&
                  offsetof(D3DCAPS9, SrcBlendCaps) == 0x2C,
              "the caps the original reads");
static_assert(sizeof(D3DADAPTER_IDENTIFIER9) == 0x44C, "zeroed as 0x113 dwords");

} // namespace

// 0x004D7C20 (not a function in Ghidra: DirectSoundEnumerateA's callback)
BOOL CALLBACK sound_device_found(GUID* /*guid*/, const char* description, const char* /*module*/, void* /*context*/)
{
    const size_t length = std::strlen(description);
    if (length > 200)
        std::memcpy(sound_device_name().data(), description, 199); // (strncpy: not terminated, the 200th byte stays)
    else
        std::memcpy(sound_device_name().data(), description, length + 1);
    return TRUE; // (all are listed: the last one stays)
}

// 0x004D7C80
HRESULT create_device(maths::PixelSize size, bool hardware_vertices, bool multisample)
{
    const auto width = size.width;
    const auto height = size.height;
    auto& gfx = renderer();
    D3DPRESENT_PARAMETERS& pp = present_parameters();
    pp.BackBufferHeight = height;
    pp.MultiSampleType = multisample ? D3DMULTISAMPLE_2_SAMPLES : D3DMULTISAMPLE_NONE;
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the chosen sample count and quality instead of 2.
    if (graphics_options::chosen_samples()) {
        pp.MultiSampleType =
            multisample ? static_cast<D3DMULTISAMPLE_TYPE>(graphics_options::chosen_samples()) : D3DMULTISAMPLE_NONE;
        pp.MultiSampleQuality = multisample ? graphics_options::chosen_quality() : 0;
    }
    pp.BackBufferWidth = width;
    const DWORD flags =
        (hardware_vertices ? D3DCREATE_HARDWARE_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING) |
        D3DCREATE_MULTITHREADED;
    IDirect3DDevice9* device = gfx.d3d_device(); // (CreateDevice's out pointer: the device's own place)
    const HRESULT result =
        renderer().direct_3d()->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, gfx.game_window(), flags, &pp, &device);
    gfx.set_d3d_device(device);
    D3DADAPTER_IDENTIFIER9& id = adapter_identifier();
    std::memset(&id, 0, sizeof(id));
    renderer().direct_3d()->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    // (through the exe's import of DirectSoundEnumerateA)
    DirectSoundEnumerateA(&sound_device_found, nullptr);
    // A Radeon 8500 (0x00786AB5)...
    renderer().set_radeon8500(std::strstr(std::data(id.Description), "8500") != nullptr);
    // ... and an Intel 915G with Realtek HD audio's rear output: 0x0067DA40 off.
    if (std::memcmp(sound_device_name().data(), "Realtek HD Audio rear output", 29) == 0 && id.VendorId == 0x8086 &&
        id.DeviceId == 0x2582)
        gfx.set_frequency_setting_allowed(0);
    return result;
}

// 0x004D7AF0
void release_render_targets()
{
    auto& gfx = renderer();
    gfx.set_back_buffer(nullptr);
    gfx.set_back_depth(nullptr);
    for (int i = 0; i < 9; ++i) {
        core::at(renderer().screen_surfaces(), i)->Release(); // (the surface pointers are left as they were)
        core::at(renderer().screen_textures(), i)->Release();
        core::at(renderer().screen_textures(), i) = nullptr;
    }
    renderer().z_buffer_surface()->Release();
    gfx.set_z_buffer_surface(nullptr);
    if (depth_texture_target()) {
        depth_texture_target()->Release();
        depth_texture_target() = nullptr;
    }
    os_layer::release_dynamic_vertex_buffer();
    os_layer::release_quad_index_buffer();
    os_layer::release_occlusion_queries();
    graphics_options::release_scene_target(); // (NOT ORIGINAL: [Graphics] msaa's scene surface)
}

// 0x004D8420
void create_render_targets()
{
    auto& gfx = renderer();
    create_dynamic_vertex_buffer(0x40000);
    create_dynamic_index_buffer(0x1000);
    // The screen copies: full size twice, then quarter, eighth and sixteenth sizes.
    static const std::array<uint32_t, 9> kDivisors = {{1, 1, 4, 4, 4, 8, 8, 16, 16}};
    const D3DPRESENT_PARAMETERS& pp = present_parameters();
    for (int i = 0; i < 9; ++i) {
        gfx.d3d_device()->CreateTexture(pp.BackBufferWidth / core::at(kDivisors, i), pp.BackBufferHeight / core::at(kDivisors, i), 1,
                                    D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &core::at(renderer().screen_textures(), i),
                                    nullptr);
        core::at(renderer().screen_textures(), i)->GetSurfaceLevel(0, &core::at(renderer().screen_surfaces(), i));
    }
    IDirect3DSurface9* z_buffer = gfx.z_buffer_surface(); // (the out pointer: its own place)
    gfx.d3d_device()->CreateDepthStencilSurface(pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat,
                                            D3DMULTISAMPLE_NONE, 0, TRUE, &z_buffer, nullptr);
    gfx.set_z_buffer_surface(z_buffer);
    IDirect3DSurface9* surface = gfx.back_buffer(); // (the out pointers: their own places)
    gfx.d3d_device()->GetRenderTarget(0, &surface);
    gfx.set_back_buffer(surface);
    surface = gfx.back_depth();
    gfx.d3d_device()->GetDepthStencilSurface(&surface);
    gfx.set_back_depth(surface);
    renderer().back_buffer()->Release(); // (only the pointers are kept)
    renderer().back_depth()->Release();
    if (adapter_info().depth_textures_supported_) // depth textures
        gfx.d3d_device()->CreateRenderTarget(256, 256, pp.BackBufferFormat, D3DMULTISAMPLE_NONE, 0, FALSE,
                                         &depth_texture_target(), nullptr);
    D3DCAPS9 caps;
    gfx.d3d_device()->GetDeviceCaps(&caps);
    if (caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
        renderer().set_shaders_available(true);
    if (caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET)
        gfx.set_stream_offset_supported(true);
    gfx.set_max_anisotropy(caps.MaxAnisotropy);
    // NOT ORIGINAL - [Graphics] anisotropy and msaa (engine/gfx/graphics_options.h): the anisotropy this device can
    // give, and the multisampled scene surface for the screen copies.
    graphics_options::active_anisotropy() = graphics_options::device_anisotropy(
        graphics_options::get_pull().anisotropy,
        (caps.TextureFilterCaps & 0x400) != 0 /* D3DPTFILTERCAPS_MINFANISOTROPY */, caps.MaxAnisotropy);
    graphics_options::create_scene_target();
    if (caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER)
        gfx.set_border_addressing_supported(true);
    gfx.set_blend_factor_supported((caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) && (caps.DestBlendCaps & D3DPBLENDCAPS_BLENDFACTOR));
    renderer().set_back_buffer_width(static_cast<int>(pp.BackBufferWidth));
    renderer().set_back_buffer_height(static_cast<int>(pp.BackBufferHeight));
    adapter_info().back_buffer_format_ = pp.BackBufferFormat;
    adapter_info().depth_bits_ = pp.AutoDepthStencilFormat == D3DFMT_D16 ? 16 : 32; // depth bits
    if (core::at(renderer().screen_surfaces(), 0)) {
        D3DSURFACE_DESC desc;
        core::at(renderer().screen_surfaces(), 0)->GetDesc(&desc);
        adapter_info().screen_surface_format_ = desc.Format;
    }
    apply_render_state();
}

// 0x004D7B60
void load_gamma()
{
    float red = 0.25f, green = 0.25f, blue = 0.25f;
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, R"(Software\Activision\Tony Hawk's Underground 2\Settings\Gamma)", 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
        DWORD size = 4;
        sys::query_registry_value(key, "R", nullptr, &red, &size);
        size = 4;
        sys::query_registry_value(key, "G", nullptr, &green, &size);
        size = 4;
        sys::query_registry_value(key, "B", nullptr, &blue, &size);
        // (the key is left open)
    }
    render::set_device_gamma(red, green, blue); // the gamma ramp
}

// 0x004D89C0
void show_disc_error()
{
    auto& gfx = renderer();
    if (const uint32_t timer = gfx.loading_bar_timer()) {
        timeKillEvent(timer); // (the exe's import)
        gfx.set_loading_bar_timer(0);
    }
    if (!gfx.draw_device())
        init_direct_3d();
    // "There's a problem with the disc you're using." / "It may be dirty or damaged." in the language
    // (0x007CEFA0: 3 French, 5 German, else English).
    const char* first = nullptr;
    const char* second = nullptr;
    switch (game_config::get_language()) {
    case 3:
        first = kDiscErrorFrench1;
        second = kDiscErrorFrench2;
        break;
    case 5:
        first = kDiscErrorGerman1;
        second = kDiscErrorGerman2;
        break;
    default:
        first = kDiscErrorEnglish1;
        second = kDiscErrorEnglish2;
        break;
    }
    GlyphRun glyphs(0.0f);
    GlyphRun* text = &glyphs;
    text->set_font(adapter_info().debug_font_);
    text->set_x(48.0f);
    text->set_y(128.0f);
    text->set_xscale(std::bit_cast<float>(0x3F4CCCCDu)); // 0.8
    text->set_yscale(1.0f);
    text->set_color(0x80808080);
    text->set_next(nullptr);
    set_texture(1, nullptr);
    set_texture(2, nullptr);
    set_texture(3, nullptr);
    // Forever: the two lines, 16 apart, on a cleared screen.
    for (;;) {
        gfx.d3d_device()->Present(nullptr, nullptr, nullptr, nullptr);
        gfx.d3d_device()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        choose_blend_mode(5);
        set_texture(0, nullptr);
        change_phase(kAddressMode0, 0x10001);
        change_phase(kDepthBias, 0);
        change_phase(kPhaseAlphaTest, 1);
        change_phase(kZWrite, 0);
        gfx.d3d_device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        gfx.d3d_device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        text->set_y(text->y() - kOffsetLines);
        text->set_text(first);
        text->open_sketch();
        text->draw();
        text->close_sketch();
        text->set_y(text->y() + kOffsetLines);
        text->set_text(second);
        text->open_sketch();
        text->draw();
        text->close_sketch();
    }
}

// 0x0044F870
long set_render_state(int state, uint32_t value)
{
    return renderer().d3d_device()->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
}

// 0x0044F890
long clear_device(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    return renderer().d3d_device()->Clear(rect_count, static_cast<const D3DRECT*>(rects), flags, color, z, stencil);
}

} // namespace render
