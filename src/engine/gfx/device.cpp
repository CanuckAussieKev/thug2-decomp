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

#include "engine/core/stubs.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/pc_gamma.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/text.h"
#include "platform/platform_d3d.h"
#include "platform/sdl_window.h"
#include "platform/stopwatch.h"
#include "engine/sys/game_config.h"
#include "platform/crt.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace render {

IDirect3DDevice9* g_d3d_device;
D3DPRESENT_PARAMETERS g_present_parameters;
bool g_stream_offset_supported;
bool g_show_window_flag;         // (the original showed the window maximised / minimised by it)
uint32_t g_loading_bar_timer;    // the timer's id (timeSetEvent)
float g_display_factor_x;        // 640x480 -> back buffer scale
IDirect3DDevice9* g_draw_device; // the same device, kept twice
std::array<IDirect3DTexture9*, 9> g_screen_textures;

uint8_t g_set_frequency_allowed{1};
int g_resolution_setting;
bool g_antialiasing;
int g_screen_offset_y;
int g_screen_offset_x;
float g_display_factor_y;
IDirect3DSurface9* g_z_buffer_surface;
std::array<IDirect3DSurface9*, 9> g_screen_surfaces; // their level 0
IDirect3DSurface9* g_back_depth;
IDirect3DSurface9* g_back_buffer;
bool g_blend_factor_supported;
bool g_border_addressing_supported;
uint32_t g_max_anisotropy;
HWND g_game_window;
IDirect3D9* g_direct_3d;
namespace {

constexpr float get_bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kPer640 = get_bits(0x3ACCCCCD); // 0x0064A474, 1 / 640
constexpr float kPer480 = get_bits(0x3B088889); // 0x0064A47C, 1 / 480
constexpr float kOffsetLines = 16.0f;           // 0x00645D68

int g_device_width{640};
int g_device_height{480};
const char* g_disc_error_french1{"Le disque utilis\xE9 pr\xE9sente une anomalie."};
const char* g_disc_error_french2{"Il est peut-\xEAtre sale ou endommag\xE9."};
const char* g_disc_error_german1{"Bei der benutzten CD ist ein Problem aufgetreten."};
const char* g_disc_error_german2{"M\xF6glicherweise ist sie verschmutzt oder besch\xE4"
                                 "digt."};
const char* g_disc_error_english1{"There's a problem with the disc you're using."};
const char* g_disc_error_english2{"It may be dirty or damaged."};
D3DADAPTER_IDENTIFIER9 g_adapter_identifier;
bool g_depth_textures_supported;
std::array<char, 200> g_sound_device_name; // the description of the last sound device DirectSound listed
IDirect3DSurface9* g_depth_texture_target;
uint32_t g_back_buffer_format;
uint32_t g_screen_surface_format;
uint32_t g_depth_bits;
TypefaceData* g_debug_font;

IDirect3D9*& direct_3d() { return g_direct_3d; }
IDirect3DDevice9*& input_port() { return g_d3d_device; }
D3DPRESENT_PARAMETERS& present_parameters() { return g_present_parameters; }
HWND pane() { return g_game_window; }
// The size the device is being created at.
int& device_width() { return g_device_width; }
int& device_height() { return g_device_height; }

} // namespace

// 0x004D8650
void init_direct_3d()
{
    g_loading_bar_timer = 0;
    g_display_factor_x = 1.0f; // 640x480 -> back buffer scale and offset
    g_display_factor_y = 1.0f;
    g_screen_offset_x = 0;
    g_screen_offset_y = 16;
    g_depth_texture_target = nullptr;
    memory::bits_as<uint32_t>(g_screen_copy_index) = 0;
    // Originally from PartyMod Fixes (its jump at 0x004D869C): the window is never maximised. The original:
    //   if (g_show_window_flag) ShowWindow(Pane(), SW_MAXIMIZE);

    // (Direct3DCreate9 and MessageBoxA through the exe's imports)
    IDirect3D9* d3d = Direct3DCreate9(31); // the game's D3D_SDK_VERSION
    direct_3d() = d3d;
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
    pp.hDeviceWindow = pane();
    // Originally from PartyMod Fixes (its byte at 0x004D8720, written when its Windowed setting is on). The original: always full screen.
    pp.Windowed = sys::get_windowed() ? TRUE : FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    // The resolution setting (0 = the largest) picks where to start; smaller ones are tried after.
    unsigned first = 3 - g_resolution_setting;
    if (first > 3)
        first = 3;
    // Originally from PartyMod Fixes (its DWords at 0x004D8776 / 0x004D8792): the smallest, 640x480, is its window's size.
    const std::array<int, 4> widths = {{1280, 1024, 800, sys::get_window_width()}};
    const std::array<int, 4> heights = {{1024, 768, 600, sys::get_window_height()}};
    bool multisample = g_antialiasing;
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): its sample count, whatever AntiAliasing says.
    graphics_options::choose_multisample(d3d, D3DFMT_X8R8G8B8, D3DFMT_D24X8, sys::get_windowed());
    if (graphics_options::chosen_samples())
        multisample = true;
    device_width() = widths[first];
    device_height() = heights[first];
    pp.BackBufferWidth = widths[first];
    pp.BackBufferHeight = heights[first];
    if (multisample)
        pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.FullScreen_RefreshRateInHz = 0;
    input_port() = nullptr;

    // Hardware vertex processing first, multisampling (if set) first; each at every size from `first` down.
    const std::array<std::array<bool, 2>, 4> attempts = {{{true, multisample}, {false, multisample}, {true, false}, {false, false}}};
    bool created = false;
    for (int attempt = 0; attempt < 4 && !created; ++attempt) {
        if (static_cast<int>(first) >= 4)
            continue;
        for (unsigned i = first; i < 4; ++i) {
            device_width() = widths[i];
            device_height() = heights[i];
            if (create_device(widths[i], heights[i], attempts[attempt][0], attempts[attempt][1]) >= 0) {
                created = true;
                break;
            }
        }
    }
    if (!created)
        crt::get_exit(0); // the game's exit()
    // Originally from PartyMod Fixes (its NOPs at 0x004D8891): the window is left where it is. The original:
    //   SetWindowPos(Pane(), HWND_TOP, 0, 0, device_width(), device_height(), SWP_NOZORDER);

    IDirect3DDevice9* device = input_port();
    g_draw_device = device;
    // (the back buffer size as set by CreateDevice, unsigned)
    auto scale_x = static_cast<float>(static_cast<double>(pp.BackBufferWidth) * kPer640);
    g_display_factor_x = scale_x;
    auto scale_y = static_cast<float>(static_cast<double>(pp.BackBufferHeight) * kPer480);
    g_screen_offset_x = 0;
    g_display_factor_y = scale_y;
    g_screen_offset_y = static_cast<int>(static_cast<int64_t>(static_cast<double>(scale_y) * kOffsetLines));

    // Whether depth textures (D24S8) can be used.
    D3DDEVICE_CREATION_PARAMETERS creation;
    device->GetCreationParameters(&creation);
    if (d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, creation.DeviceType, pp.BackBufferFormat, 0, D3DRTYPE_TEXTURE,
                               D3DFMT_D24S8) == D3D_OK)
        g_depth_textures_supported = true;
    for (IDirect3DTexture9*& texture : g_screen_textures)
        texture = nullptr;
    g_z_buffer_surface = nullptr;
    reset_render_state();
    create_render_targets();
    // Originally from PartyMod Fixes (its jump at 0x004D8989): the cursor is left alone. The original hid it when full screen:
    //   if (!pp.Windowed) while (ShowCursor(FALSE) > 0) {}
    load_gamma();
    // the debug font (none on PC: the original calls the shared `return 0` stub)
    g_debug_font = cell::pointer<TypefaceData>(static_cast<uintptr_t>(stubs::return_zero()));
    frame_clock::change_bits();
}

namespace {

// The default-pool objects (released before a device reset, made again after).
IDirect3DTexture9** screen_textures() { return g_screen_textures.data(); } // [9]
IDirect3DSurface9** screen_surfaces() { return g_screen_surfaces.data(); } // [9], their level 0
IDirect3DSurface9*& z_buffer_surface() { return g_z_buffer_surface; }
IDirect3DSurface9*& back_buffer() { return g_back_buffer; }
IDirect3DSurface9*& back_depth() { return g_back_depth; }
IDirect3DSurface9*& depth_texture_target() { return g_depth_texture_target; }
D3DADAPTER_IDENTIFIER9& adapter_identifier() { return g_adapter_identifier; }
// The description of the last sound device DirectSound listed.
char* sound_device_name() { return g_sound_device_name.data(); }

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
        std::memcpy(sound_device_name(), description, 199); // (strncpy: not terminated, the 200th byte stays)
    else
        std::memcpy(sound_device_name(), description, length + 1);
    return TRUE; // (all are listed: the last one stays)
}

// 0x004D7C80
HRESULT create_device(int width, int height, bool hardware_vertices, bool multisample)
{
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
    const HRESULT result =
        direct_3d()->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pane(), flags, &pp, &input_port());
    D3DADAPTER_IDENTIFIER9& id = adapter_identifier();
    std::memset(&id, 0, sizeof(id));
    direct_3d()->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    // (through the exe's import of DirectSoundEnumerateA)
    DirectSoundEnumerateA(&sound_device_found, nullptr);
    // A Radeon 8500 (0x00786AB5)...
    g_radeon8500 = std::strstr(id.Description, "8500") != nullptr;
    // ... and an Intel 915G with Realtek HD audio's rear output: 0x0067DA40 off.
    if (std::memcmp(sound_device_name(), "Realtek HD Audio rear output", 29) == 0 && id.VendorId == 0x8086 &&
        id.DeviceId == 0x2582)
        g_set_frequency_allowed = 0;
    return result;
}

// 0x004D7AF0
void release_render_targets()
{
    back_buffer() = nullptr;
    back_depth() = nullptr;
    for (int i = 0; i < 9; ++i) {
        screen_surfaces()[i]->Release(); // (the surface pointers are left as they were)
        screen_textures()[i]->Release();
        screen_textures()[i] = nullptr;
    }
    z_buffer_surface()->Release();
    z_buffer_surface() = nullptr;
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
    create_dynamic_vertex_buffer(0x40000);
    create_dynamic_index_buffer(0x1000);
    // The screen copies: full size twice, then quarter, eighth and sixteenth sizes.
    static const std::array<uint32_t, 9> kDivisors = {{1, 1, 4, 4, 4, 8, 8, 16, 16}};
    const D3DPRESENT_PARAMETERS& pp = present_parameters();
    for (int i = 0; i < 9; ++i) {
        input_port()->CreateTexture(pp.BackBufferWidth / kDivisors[i], pp.BackBufferHeight / kDivisors[i], 1,
                                    D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &screen_textures()[i],
                                    nullptr);
        screen_textures()[i]->GetSurfaceLevel(0, &screen_surfaces()[i]);
    }
    input_port()->CreateDepthStencilSurface(pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat,
                                            D3DMULTISAMPLE_NONE, 0, TRUE, &z_buffer_surface(), nullptr);
    input_port()->GetRenderTarget(0, &back_buffer());
    input_port()->GetDepthStencilSurface(&back_depth());
    back_buffer()->Release(); // (only the pointers are kept)
    back_depth()->Release();
    if (g_depth_textures_supported) // depth textures
        input_port()->CreateRenderTarget(256, 256, pp.BackBufferFormat, D3DMULTISAMPLE_NONE, 0, FALSE,
                                         &depth_texture_target(), nullptr);
    D3DCAPS9 caps;
    input_port()->GetDeviceCaps(&caps);
    if (caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
        g_shaders_available = true;
    if (caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET)
        g_stream_offset_supported = true;
    g_max_anisotropy = caps.MaxAnisotropy;
    // NOT ORIGINAL - [Graphics] anisotropy and msaa (engine/gfx/graphics_options.h): the anisotropy this device can
    // give, and the multisampled scene surface for the screen copies.
    graphics_options::active_anisotropy() = graphics_options::device_anisotropy(
        graphics_options::get_pull().anisotropy,
        (caps.TextureFilterCaps & 0x400) != 0 /* D3DPTFILTERCAPS_MINFANISOTROPY */, caps.MaxAnisotropy);
    graphics_options::create_scene_target();
    if (caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER)
        g_border_addressing_supported = true;
    g_blend_factor_supported =
        (caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) && (caps.DestBlendCaps & D3DPBLENDCAPS_BLENDFACTOR);
    memory::bits_as<uint32_t>(g_back_buffer_width) = pp.BackBufferWidth;
    memory::bits_as<uint32_t>(g_back_buffer_height) = pp.BackBufferHeight;
    g_back_buffer_format = pp.BackBufferFormat;
    g_depth_bits = pp.AutoDepthStencilFormat == D3DFMT_D16 ? 16 : 32; // depth bits
    if (screen_surfaces()[0]) {
        D3DSURFACE_DESC desc;
        screen_surfaces()[0]->GetDesc(&desc);
        g_screen_surface_format = desc.Format;
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
        RegQueryValueExA(key, "R", nullptr, nullptr, raw::bytes(&red), &size);
        size = 4;
        RegQueryValueExA(key, "G", nullptr, nullptr, raw::bytes(&green), &size);
        size = 4;
        RegQueryValueExA(key, "B", nullptr, nullptr, raw::bytes(&blue), &size);
        // (the key is left open)
    }
    render::set_device_gamma(red, green, blue); // the gamma ramp
}

// 0x004D89C0
void show_disc_error()
{
    uint32_t& timer = g_loading_bar_timer;
    if (timer) {
        timeKillEvent(timer); // (the exe's import)
        timer = 0;
    }
    if (!g_draw_device)
        init_direct_3d();
    // "There's a problem with the disc you're using." / "It may be dirty or damaged." in the language
    // (0x007CEFA0: 3 French, 5 German, else English).
    const char* first = nullptr;
    const char* second = nullptr;
    switch (game_config::g_language) {
    case 3:
        first = g_disc_error_french1;
        second = g_disc_error_french2;
        break;
    case 5:
        first = g_disc_error_german1;
        second = g_disc_error_german2;
        break;
    default:
        first = g_disc_error_english1;
        second = g_disc_error_english2;
        break;
    }
    GlyphRun glyphs(0.0f);
    GlyphRun* text = &glyphs;
    text->font = g_debug_font;
    text->x = 48.0f;
    text->y = 128.0f;
    text->xscale = std::bit_cast<float>(0x3F4CCCCDu); // 0.8
    text->yscale = 1.0f;
    text->color = 0x80808080;
    text->next = nullptr;
    set_texture(1, nullptr);
    set_texture(2, nullptr);
    set_texture(3, nullptr);
    // Forever: the two lines, 16 apart, on a cleared screen.
    for (;;) {
        input_port()->Present(nullptr, nullptr, nullptr, nullptr);
        input_port()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        choose_blend_mode(5);
        set_texture(0, nullptr);
        change_phase(kAddressMode0, 0x10001);
        change_phase(kDepthBias, 0);
        change_phase(kPhaseAlphaTest, 1);
        change_phase(kZWrite, 0);
        input_port()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        input_port()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        text->y = text->y - kOffsetLines;
        text->text = first;
        text->open_sketch();
        text->draw();
        text->close_sketch();
        text->y = text->y + kOffsetLines;
        text->text = second;
        text->open_sketch();
        text->draw();
        text->close_sketch();
    }
}

// 0x0044F870
long set_render_state(int state, uint32_t value)
{
    return input_port()->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
}

// 0x0044F890
long clear_device(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    return input_port()->Clear(rect_count, static_cast<const D3DRECT*>(rects), flags, color, z, stencil);
}

} // namespace render
