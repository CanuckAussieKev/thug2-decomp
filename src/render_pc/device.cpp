// Direct3D 9 start-up, with PartyMod's window and resolution changes built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "render_pc/device.h"

#include <windows.h>

#include <mmsystem.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/audio/audio_device.h"
#include "engine/gfx/mesh_formats.h"
#include "base/core/stubs.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/graphics_options.h"
#include "render_pc/pc_gamma.h"
#include "render_pc/render_state.h"
#include "render_pc/text.h"
#include "render_pc/pc_d3d_resources.h"
#include "platform/sdl_window.h"
#include "engine/sys/stopwatch.h"
#include "engine/sys/game_config.h"
#include "platform/crt.h"
#include "base/core/checked_index.h"
#include "platform/registry.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "base/maths/fpu_precision.h"
#include "base/maths/rect.h"
#include "base/core/language.h"
#include "render_pc/graphics_options_d3d.h"

namespace render {

constexpr int kDefaultDeviceWidth = 640;
constexpr int kDefaultDeviceHeight = 480;
constexpr std::size_t kSoundDeviceNameSize = 200;
constexpr std::size_t kSoundDeviceNameCopyBytes = 199; // one less: strncpy's result isn't terminated
constexpr int kScreenOffsetLines = 16;
constexpr std::size_t kRearOutputNameBytes = 29; // "Realtek HD Audio rear output" with its terminator
constexpr int kVendorIdIntel = 0x8086;
constexpr int kDeviceIdRearOutputChipset = 0x2582;
constexpr int kScreenSurfaceCount = 9;
constexpr int kDynamicVertexBufferBytes = 0x40000;
constexpr int kDynamicIndexBufferBytes = 0x1000;
constexpr int kSmallTargetSize = 256;
constexpr int kDepthBits16 = 16;
constexpr int kDepthBits32 = 32;
constexpr float kFlashChannel = 0.25F;
constexpr float kDiscErrorX = 48.0F;
constexpr float kDiscErrorY = 128.0F;
constexpr uint32_t kDiscErrorScaleBits = 0x3F4CCCCDU; // 0.8
constexpr uint32_t kNeutralColour = 0x80808080;

namespace {

constexpr std::size_t kScreenCopyCount = 9;

// NOT ORIGINAL grouping - the default-pool objects the frame is drawn through (the original's globals; the renderer's
// handles, not Direct3D's types): the screen copies (full size to a sixteenth) and their level 0, the depth surface
// they are drawn with, the back buffer with its depth. Made on first use, never destroyed (frame_targets()).
struct FrameTargets {
    std::array<GpuTexture*, kScreenCopyCount> screen_textures_{};
    std::array<GpuSurface*, kScreenCopyCount> screen_surfaces_{};
    GpuSurface* z_buffer_{};
    GpuSurface* back_depth_{};
    GpuSurface* back_buffer_{};
};

FrameTargets& frame_targets()
{
    static core::NoDestroy<FrameTargets> state;
    return state.get();
}

} // namespace

uint32_t presented_width() { return gpu_context().settings().width; }

uint32_t presented_height() { return gpu_context().settings().height; }

bool device_made() { return gpu_context().device_made(); }

GpuTexture* screen_copy_texture(int index) { return core::at(frame_targets().screen_textures_, index); }

GpuSurface* scene_depth_surface() { return frame_targets().z_buffer_; }

GpuSurface* back_buffer_surface() { return frame_targets().back_buffer_; }

GpuSurface* back_depth_surface() { return frame_targets().back_depth_; }

bool screen_copies_made() { return core::at(frame_targets().screen_surfaces_, 0) != nullptr; }

namespace {

constexpr float get_bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kPer640 = get_bits(0x3ACCCCCD); // 0x0064A474, 1 / 640
constexpr float kPer480 = get_bits(0x3B088889); // 0x0064A47C, 1 / 480
constexpr float kOffsetLines = 16.0F;           // 0x00645D68

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
    int device_width_{kDefaultDeviceWidth};
    int device_height_{kDefaultDeviceHeight};
};

DeviceState& device_state()
{
    static core::NoDestroy<DeviceState> state;
    return state.get();
}

// NOT ORIGINAL grouping - the adapter: its identifier, depth textures, the back buffer's and screen's formats, the
// depth bits, the debug font (the original's globals). Made on first use, never destroyed (adapter_info()).
struct AdapterInfo {
    bool depth_textures_supported_{};
    std::array<char, kSoundDeviceNameSize> sound_device_name_{}; // the description of the last sound device listed
    GpuSurface* depth_texture_target_{};
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
    gfx.set_display_factor_x(1.0F); // 640x480 -> back buffer scale and offset
    gfx.set_display_factor_y(1.0F);
    gfx.set_screen_offset_x(0);
    gfx.set_screen_offset_y(kScreenOffsetLines);
    adapter_info().depth_texture_target_ = nullptr;
    renderer().set_screen_copy_index(0);
    // Originally from PartyMod Fixes (its jump at 0x004D869C): the window is never maximised. The original:
    //   if (kShowWindowFlag) ShowWindow(Pane(), SW_MAXIMIZE);

    // (Direct3DCreate9 and MessageBoxA through the exe's imports)
    GpuContext& context = gpu_context();
    if (!context.create_instance()) {
        // (the text and the caption are the wrong way round)
        MessageBoxA(nullptr, "Error" /* "Error" */,
                    "Direct3D could not initialize. Please download and install DirectX 9.0 or higher."
                    /* "Direct3D could not initialize. Please download and install DirectX 9.0 or higher." */,
                    MB_OK);
        return;
    }
    if (!context.adapter_ready()) {
        return;
    }

    // Originally from PartyMod Fixes (its byte at 0x004D8720, written when its Windowed setting is on). The original: always full screen.
    context.configure({.windowed = sys::get_windowed(),
                       .vsync = true,
                       .colour_format = gpu_value::kFormatX8R8G8B8,
                       .depth_format = gpu_value::kFormatD24X8});
    // The resolution setting (0 = the largest) picks where to start; smaller ones are tried after.
    unsigned first = 3 - gfx.resolution_setting();
    first = std::min<unsigned int>(first, 3);
    // Originally from PartyMod Fixes (its DWords at 0x004D8776 / 0x004D8792): the smallest, 640x480, is its window's size.
    const std::array<int, 4> widths = {{1280, 1024, 800, sys::get_window_width()}};
    const std::array<int, 4> heights = {{1024, 768, 600, sys::get_window_height()}};
    bool multisample = gfx.antialiasing();
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): its sample count, whatever AntiAliasing says.
    graphics_options::choose_multisample(gpu_value::kFormatX8R8G8B8, gpu_value::kFormatD24X8, sys::get_windowed());
    if (graphics_options::chosen_samples() != 0) {
        multisample = true;
    }
    device_width() = core::at(widths, first);
    device_height() = core::at(heights, first);

    // Hardware vertex processing first, multisampling (if set) first; each at every size from `first` down.
    const std::array<std::array<bool, 2>, 4> attempts = {{{true, multisample}, {false, multisample}, {true, false}, {false, false}}};
    bool created = false;
    for (int attempt = 0; attempt < 4 && !created; ++attempt) {
        if (static_cast<int>(first) >= 4) {
            continue;
        }
        for (unsigned i = first; i < 4; ++i) {
            device_width() = core::at(widths, i);
            device_height() = core::at(heights, i);
            if (create_device({.width = core::at(widths, i), .height = core::at(heights, i)}, core::at(core::at(attempts, attempt), 0), core::at(core::at(attempts, attempt), 1)) >= 0) {
                created = true;
                break;
            }
        }
    }
    if (!created) {
        crt::exit_fatal(0); // the game's exit() (without the static owners' teardown)
    }
    // Originally from PartyMod Fixes (its NOPs at 0x004D8891): the window is left where it is. The original:
    //   SetWindowPos(Pane(), HWND_TOP, 0, 0, device_width(), device_height(), SWP_NOZORDER);

    context.mark_ready();
    const GpuSettings made = context.settings();
    // (the back buffer size as set by CreateDevice, unsigned)
    auto scale_x = static_cast<float>(static_cast<double>(made.width) * kPer640);
    gfx.set_display_factor_x(scale_x);
    auto scale_y = static_cast<float>(static_cast<double>(made.height) * kPer480);
    gfx.set_screen_offset_x(0);
    gfx.set_display_factor_y(scale_y);
    gfx.set_screen_offset_y(static_cast<int>(static_cast<int64_t>(static_cast<double>(scale_y) * kOffsetLines)));

    // Whether depth textures (D24S8) can be used.
    if (context.depth_textures_supported()) {
        adapter_info().depth_textures_supported_ = true;
    }
    for (GpuTexture*& texture : frame_targets().screen_textures_) {
        texture = nullptr;
    }
    frame_targets().z_buffer_ = nullptr;
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

GpuSurface*& depth_texture_target() { return adapter_info().depth_texture_target_; }
// The description of the last sound device listed.
std::array<char, kSoundDeviceNameSize>& sound_device_name() { return adapter_info().sound_device_name_; }

// The dynamic vertex buffer and the quads' index buffer (the platform layer's).
void create_dynamic_vertex_buffer(uint32_t size) { os_layer::create_dynamic_vertex_buffer(size); }
void create_dynamic_index_buffer(uint32_t quads) { os_layer::create_quad_index_buffer(quads); }

} // namespace

// 0x004D7C20 (not a function in Ghidra: DirectSoundEnumerateA's callback; the sound output's listing calls it with
// each description, and goes on to the next - the original returned TRUE)
void sound_device_found(const char* description)
{
    const size_t length = std::strlen(description);
    if (length > kSoundDeviceNameSize) {
        std::memcpy(sound_device_name().data(), description, kSoundDeviceNameCopyBytes); // (strncpy: not terminated, the 200th byte stays)
    } else {
        std::memcpy(sound_device_name().data(), description, length + 1);
    }
    // (all are listed: the last one stays)
}

// 0x004D7C80
long create_device(maths::PixelSize size, bool hardware_vertices, bool multisample)
{
    const auto width = size.width;
    const auto height = size.height;
    auto& gfx = renderer();
    // Two samples; NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the chosen sample count and quality
    // instead of 2.
    uint32_t samples = multisample ? 2U : 0U;
    uint32_t sample_quality = 0;
    if (graphics_options::chosen_samples() != 0) {
        samples = multisample ? static_cast<uint32_t>(graphics_options::chosen_samples()) : 0U;
        sample_quality = multisample ? graphics_options::chosen_quality() : 0U;
    }
    GpuContext& context = gpu_context();
    const long result = context.create_device({.width = static_cast<uint32_t>(width),
                                               .height = static_cast<uint32_t>(height),
                                               .hardware_vertices = hardware_vertices,
                                               .samples = samples,
                                               .sample_quality = sample_quality});
    // NOT ORIGINAL - Direct3D 9 loaded its x87 word into this thread here (the device made without
    // D3DCREATE_FPU_PRESERVE): the same word whatever the renderer (base/maths/fpu_precision.h).
    maths::use_device_fpu_precision();
    const GpuAdapterInfo& adapter = context.adapter();
    // (the exe's import of DirectSoundEnumerateA: the sound output's devices)
    audio::device().list_outputs(&sound_device_found);
    // A Radeon 8500 (0x00786AB5)...
    renderer().set_radeon8500(std::strstr(adapter.description.data(), "8500") != nullptr);
    // ... and an Intel 915G with Realtek HD audio's rear output: 0x0067DA40 off.
    if (std::memcmp(sound_device_name().data(), "Realtek HD Audio rear output", kRearOutputNameBytes) == 0 &&
        adapter.vendor_id == kVendorIdIntel && adapter.device_id == kDeviceIdRearOutputChipset) {
        gfx.set_frequency_setting_allowed(0);
    }
    return result;
}

// 0x004D7AF0
void release_render_targets()
{
    auto& gfx = renderer();
    frame_targets().back_buffer_ = nullptr;
    frame_targets().back_depth_ = nullptr;
    for (int i = 0; i < kScreenSurfaceCount; ++i) {
        gpu().release_surface(core::at(frame_targets().screen_surfaces_, i)); // (the surface pointers are left as they were)
        gpu().release_texture(core::at(frame_targets().screen_textures_, i));
        core::at(frame_targets().screen_textures_, i) = nullptr;
    }
    gpu().release_surface(frame_targets().z_buffer_);
    frame_targets().z_buffer_ = nullptr;
    if (depth_texture_target()) {
        gpu().release_surface(depth_texture_target());
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
    create_dynamic_vertex_buffer(kDynamicVertexBufferBytes);
    create_dynamic_index_buffer(kDynamicIndexBufferBytes);
    GpuContext& context = gpu_context();
    // The screen copies: full size twice, then quarter, eighth and sixteenth sizes.
    static const std::array<uint32_t, 9> kDivisors = {{1, 1, 4, 4, 4, 8, 8, 16, 16}};
    const GpuSettings made = context.settings();
    for (int i = 0; i < kScreenSurfaceCount; ++i) {
        gpu().create_texture({.width = made.width / core::at(kDivisors, i),
                              .height = made.height / core::at(kDivisors, i),
                              .levels = 1,
                              .usage = gpu_value::kUsageRenderTarget,
                              .format = gpu_value::kFormatX8R8G8B8,
                              .pool = gpu_value::kPoolDefault},
                             &core::at(frame_targets().screen_textures_, i));
        gpu().texture_surface(core::at(frame_targets().screen_textures_, i), 0, &core::at(frame_targets().screen_surfaces_, i));
    }
    gpu().create_depth_surface({.width = made.width,
                                .height = made.height,
                                .format = made.depth_format,
                                .samples = 0,
                                .quality = 0,
                                .lockable_or_discard = true},
                               &frame_targets().z_buffer_);
    gpu().render_target(0, &frame_targets().back_buffer_); // (the out pointers: their own places)
    gpu().depth_surface(&frame_targets().back_depth_);
    gpu().release_surface(frame_targets().back_buffer_); // (only the pointers are kept)
    gpu().release_surface(frame_targets().back_depth_);
    if (adapter_info().depth_textures_supported_) { // depth textures
        gpu().create_render_target({.width = kSmallTargetSize,
                                    .height = kSmallTargetSize,
                                    .format = made.colour_format,
                                    .samples = 0,
                                    .quality = 0,
                                    .lockable_or_discard = false},
                                   &depth_texture_target());
    }
    const GpuCaps caps = context.caps();
    if (caps.pixel_shaders_1_1) {
        renderer().set_shaders_available(true);
    }
    if (caps.stream_offset) {
        gfx.set_stream_offset_supported(true);
    }
    gfx.set_max_anisotropy(caps.max_anisotropy);
    // NOT ORIGINAL - [Graphics] anisotropy and msaa (engine/gfx/graphics_options.h): the anisotropy this device can
    // give, and the multisampled scene surface for the screen copies.
    graphics_options::active_anisotropy() = graphics_options::device_anisotropy(
        graphics_options::get_pull().anisotropy, caps.min_filter_anisotropic, caps.max_anisotropy);
    graphics_options::create_scene_target();
    if (caps.border_addressing) {
        gfx.set_border_addressing_supported(true);
    }
    gfx.set_blend_factor_supported(caps.blend_factor);
    renderer().set_back_buffer_width(static_cast<int>(made.width));
    renderer().set_back_buffer_height(static_cast<int>(made.height));
    adapter_info().back_buffer_format_ = made.colour_format;
    adapter_info().depth_bits_ = made.depth_format == gpu_value::kFormatD16 ? kDepthBits16 : kDepthBits32; // depth bits
    if (core::at(frame_targets().screen_surfaces_, 0)) {
        GpuSurfaceDesc desc;
        gpu().surface_desc(core::at(frame_targets().screen_surfaces_, 0), &desc);
        adapter_info().screen_surface_format_ = desc.format;
    }
    apply_render_state();
}

// 0x004D7B60
void load_gamma()
{
    float red = kFlashChannel;
    float green = kFlashChannel;
    float blue = kFlashChannel;
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
    if (!gpu_context().ready()) {
        init_direct_3d();
    }
    // "There's a problem with the disc you're using." / "It may be dirty or damaged." in the language
    // (0x007CEFA0: 3 French, 5 German, else English).
    const char* first = nullptr;
    const char* second = nullptr;
    switch (game_config::get_language()) {
    case 3:
        first = kDiscErrorFrench1;
        second = kDiscErrorFrench2;
        break;
    case core::kLanguageGerman:
        first = kDiscErrorGerman1;
        second = kDiscErrorGerman2;
        break;
    default:
        first = kDiscErrorEnglish1;
        second = kDiscErrorEnglish2;
        break;
    }
    GlyphRun glyphs(0.0F);
    GlyphRun* text = &glyphs;
    text->set_font(adapter_info().debug_font_);
    text->set_x(kDiscErrorX);
    text->set_y(kDiscErrorY);
    text->set_xscale(std::bit_cast<float>(kDiscErrorScaleBits)); // 0.8
    text->set_yscale(1.0F);
    text->set_color(kNeutralColour);
    text->set_next(nullptr);
    set_texture(1, nullptr);
    set_texture(2, nullptr);
    set_texture(3, nullptr);
    // Forever: the two lines, 16 apart, on a cleared screen.
    for (;;) {
        gpu().present();
        gpu().clear(0, nullptr, gpu_value::kClearTarget | gpu_value::kClearDepth, 0, 1.0F, 0);
        choose_blend_mode(kBlendModeAlpha);
        set_texture(0, nullptr);
        change_phase(RenderStateId::kAddressMode0, kClampBothWays);
        change_phase(RenderStateId::kDepthBias, 0);
        change_phase(RenderStateId::kPhaseAlphaTest, 1);
        change_phase(RenderStateId::kZWrite, 0);
        gpu().set_stage_state(0, GpuStageState::kTextureTransformFlags, gpu_value::kTextureTransformDisable);
        gpu().set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
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
long set_render_state(GpuRenderState state, uint32_t value) { return gpu().set_render_state(state, value); }
long set_render_state(int state, uint32_t value)
{
    return set_render_state(static_cast<GpuRenderState>(state), value);
}

// 0x0044F890
long clear_device(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    return gpu().clear(rect_count, static_cast<const GpuRect*>(rects), flags, color, z, stencil);
}

} // namespace render
