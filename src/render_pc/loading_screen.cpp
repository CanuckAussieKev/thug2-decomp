// The loading screen: see loading_screen.h.

#include "engine/gfx/engine_glue.h"
#include "render_pc/pc_engine.h"
#include "render_pc/loading_screen.h"
#include "engine/gfx/engine_sprite.h"

#include <windows.h>
#include <mmsystem.h>

#include <cstddef>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>
#include <iterator>

#include "engine/memory/mem.h"
#include "engine/gfx/engine_texture.h"
#include "render_pc/loading_bar.h"
#include "render_pc/pc_texture.h"
#include "render_pc/render_state.h"
#include "platform/run_log.h"
#include "engine/sys/game_config.h"
#include "base/core/checked_index.h"
#include <string>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/screen_2d.h"
#include "engine/gfx/engine_parts.h"
#include "base/core/language.h"

namespace render {

constexpr int kProgressBarWidth = 300;
// The loading bar's colours (red, green, blue, alpha).
constexpr int kProgressFromRed = 0xB9;
constexpr int kProgressFromGreen = 0xB5;
constexpr int kProgressFromBlue = 0xAC;
constexpr int kProgressToRed = 0xF2;
constexpr int kProgressToGreen = 0xE2;
constexpr int kProgressToBlue = 0x05;
constexpr int kProgressEdgeRed = 0x3A;
constexpr int kProgressEdgeBlue = 0x38;
constexpr int kProgressAlpha = 0x80;
constexpr float kHalf = 0.5F;
constexpr float kTickSeconds = 0.03F;
constexpr float kLoadSecondsScale = 0.6F;
constexpr int kLoadingTimerPeriod = 29; // milliseconds between redraws of the loading screen
constexpr std::size_t kPickedNameBytes = 0xC8;

namespace {

// The Windows calls, through the exe's imports (as device.cpp's timer: so a test can stand in for them alone).
void get_lock() { WaitForSingleObject(screen_2d().screen_mutex(), INFINITE); }
void get_unlock() { ReleaseMutex(screen_2d().screen_mutex()); }
UINT start_timer(UINT delay, LPTIMECALLBACK callback) { return timeSetEvent(delay, 0, callback, 0, TIME_PERIODIC); }
void stop_timer(UINT timer) { timeKillEvent(timer); }

// NOT ORIGINAL grouping - loading_screen.cpp's state (the original's globals). Made on first use, never destroyed
// (loading_screen_state()).
struct LoadingScreenState {
    std::unique_ptr<render::BillboardQuad> loading_screen_sprite_; // (nothing in this build sets it)
    std::string picture_name_; // the picture loaded
};

LoadingScreenState& loading_screen_state()
{
    static core::NoDestroy<LoadingScreenState> state;
    return state.get();
}

// The picture (a texture holder, 0x14 bytes: 0x004C7ED0; its texture at +0x10, through one more pointer with flag 1
// of +0x08) and the loading screen's other sprite.

// The picture's name (a std::string in the original too: its assign at 0x004B7880 / 0x004B7750).
const char* picture_name() { return loading_screen_state().picture_name_.c_str(); }
void set_picture_name(const char* text) { loading_screen_state().picture_name_ = text; }

// NOT ORIGINAL - the level select's loading pictures (tools/pipeline menu_level/loading_select.ts): a level whose
// loading_screen is "loadscrn_sel_<load>__<the original picture>" shows the level select's picture (16:9, text-free);
// [features] level_select_loading_screens=0 loads the original picture named after the "__" instead.
constexpr auto kSelectPrefix = std::to_array("loadscrn_sel_");
constexpr float kSelectAspect = 16.0F / 9.0F;
// NOT ORIGINAL grouping - the level-select picture, whether the bar is restyled and the bar's saved size (the
// original's globals). Made on first use, never destroyed (select_picture()).
struct SelectPicture {
    bool select_picture_ = false; // the picture up is one of those (drawn cover-fitted, with the bar's plate)
    bool bar_styled_ = false; // the bar's own settings are saved and ours are in
    int saved_bar_width_ = 0;
    std::array<std::array<uint8_t, 4>, 3> saved_colors_ = {};
};

SelectPicture& select_picture()
{
    static core::NoDestroy<SelectPicture> state;
    return state.get();
}

bool is_select_name(const char* name) { return std::strncmp(name, kSelectPrefix.data(), sizeof(kSelectPrefix) - 1) == 0; }
bool select_enabled()
{
    static const bool kOn = config_int("features", "level_select_loading_screens", 1) != 0;
    return kOn;
}
// "loadscrn_sel_load_bo__loadscrn_boston" -> "loadscrn_boston" (a name without the "__" is left as it is)
const char* original_of(const char* name)
{
    const char* mark = std::strstr(name, "__");
    return mark ? std::next(mark, 2) : name;
}

// The bar's look over those pictures (the front end's: paper to safety yellow on ink; wider than the original's 140).
void apply_bar_style(bool select)
{
    if (select == select_picture().bar_styled_) {
        return;
    }
    if (select) {
        select_picture().saved_bar_width_ = screen_2d().progress_width();
        std::memcpy(core::at(select_picture().saved_colors_, 0).data(), screen_2d().progress_from_color().data(), 4);
        std::memcpy(core::at(select_picture().saved_colors_, 1).data(), screen_2d().progress_to_color().data(), 4);
        std::memcpy(core::at(select_picture().saved_colors_, 2).data(), screen_2d().progress_edge_color().data(), 4);
        screen_2d().progress_width() = kProgressBarWidth;
        screen_2d().progress_from_color() = {kProgressFromRed, kProgressFromGreen, kProgressFromBlue, kProgressAlpha};
        screen_2d().progress_to_color() = {kProgressToRed, kProgressToGreen, kProgressToBlue, kProgressAlpha};
        screen_2d().progress_edge_color() = {kProgressEdgeRed, kProgressEdgeRed, kProgressEdgeBlue, kProgressAlpha};
    } else {
        screen_2d().progress_width() = select_picture().saved_bar_width_;
        std::memcpy(screen_2d().progress_from_color().data(), core::at(select_picture().saved_colors_, 0).data(), 4);
        std::memcpy(screen_2d().progress_to_color().data(), core::at(select_picture().saved_colors_, 1).data(), 4);
        std::memcpy(screen_2d().progress_edge_color().data(), core::at(select_picture().saved_colors_, 2).data(), 4);
    }
    select_picture().bar_styled_ = select;
    update_loading_bar_colors();
}

struct PictureVertex {
    float x, y, z, rhw;
    float u, v;
};
constexpr std::size_t kPictureVertexBytes = 0x18;

static_assert(sizeof(PictureVertex) == kPictureVertexBytes, "the original's vertex");

} // namespace

// 0x004B6CD0
void draw_loading_screen_picture()
{
    if (!screen_2d().loading_screen_picture()) {
        return;
    }
    get_lock();
    gpu().set_stage_state(0, GpuStageState::kColourOp, gpu_value::kOpSelectArg1);
    gpu().set_stage_state(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
    gpu().set_stage_state(0, GpuStageState::kColourArg1, gpu_value::kArgTexture);
    gpu().set_stage_state(0, GpuStageState::kAlphaArg1, gpu_value::kArgTexture);
    uint32_t min_filter = 0;
    gpu().get_sampler_state(0, GpuSamplerState::kMinFilter, &min_filter);
    gpu().set_sampler_state(0, GpuSamplerState::kMagFilter, gpu_value::kFilterLinear);
    gpu().set_sampler_state(0, GpuSamplerState::kMinFilter, gpu_value::kFilterLinear);
    change_phase(RenderStateId::kAddressMode0, kClampBothWays); // (clamped both ways)
    change_phase(RenderStateId::kLighting, 0);
    set_pixel_shader(nullptr, nullptr);
    set_vertex_format(kFvfXyzRhw | kFvfTex1);
    choose_blend_mode(0);
    set_texture(0, nullptr); // (the original passes a third 0, here and below)
    set_texture(0, image_of(screen_2d().loading_screen_picture().get())->texture); // (and its palette)
    auto width = static_cast<float>(renderer().back_buffer_width());
    auto height = static_cast<float>(renderer().back_buffer_height());
    // (not original: the level select's pictures are 16:9 and cover the screen - the overflow of another shape cut off
    // evenly - where the original's are stretched)
    float u0 = 0.0F;
    float u1 = 1.0F;
    float v0 = 0.0F;
    float v1 = 1.0F;
    if (select_picture().select_picture_ && width > 0.0F && height > 0.0F) {
        const float aspect = width / height;
        if (aspect < kSelectAspect) {
            u0 = (1.0F - (aspect / kSelectAspect)) * kHalf;
            u1 = 1.0F - u0;
        } else {
            v0 = (1.0F - (kSelectAspect / aspect)) * kHalf;
            v1 = 1.0F - v0;
        }
    }
    const std::array<PictureVertex, 4> quad = {{
        {.x = 0.0F, .y = 0.0F, .z = 0.0F, .rhw = 1.0F, .u = u0, .v = v1},
        {.x = 0.0F, .y = height, .z = 0.0F, .rhw = 1.0F, .u = u0, .v = v0},
        {.x = width, .y = 0.0F, .z = 0.0F, .rhw = 1.0F, .u = u1, .v = v1},
        {.x = width, .y = height, .z = 0.0F, .rhw = 1.0F, .u = u1, .v = v0},
    }};
    gpu().draw_from_memory(GpuPrimitive::kTriangleStrip, 2, quad.data(), sizeof(PictureVertex));
    set_texture(0, nullptr);
    gpu().set_sampler_state(0, GpuSamplerState::kMinFilter, min_filter);
    get_unlock();
}

// 0x004B6F10
void hide_loading_screen()
{
    get_lock();
    gpu().set_render_state(GpuRenderState::kFogEnable, renderer().fog_active());
    if (renderer().loading_bar_timer() != 0U) {
        stop_timer(renderer().loading_bar_timer());
        renderer().set_loading_bar_timer(0);
    }
    engine_parts().set_loading_screen_up(false);
    get_unlock();
}

// 0x004B6F70
void free_loading_screen_sprite()
{
    get_lock();
    gpu().set_render_state(GpuRenderState::kFogEnable, renderer().fog_active());
    memory::destroy_then_clear(loading_screen_state().loading_screen_sprite_); // (the engine's delete through slot 0)
    get_unlock();
}

// 0x004B7400
void show_progress_bar(float seconds)
{
    screen_2d().set_elapsed_seconds(0.0F);                                                        // elapsed
    screen_2d().tick_seconds() = kTickSeconds;                                                    // a tick's seconds (0x3CF5C28F)
    screen_2d().total_seconds() = static_cast<float>(static_cast<double>(seconds) * kLoadSecondsScale); // (0x006463C4)
    update_loading_bar_colors();
    if (renderer().loading_bar_timer() == 0U) {
        renderer().set_loading_bar_timer(start_timer(kLoadingTimerPeriod, &draw_loading_screen_frame));
    }
}

// 0x004B78B0
void show_loading_screen(const char* name, bool keep, bool black)
{
    get_lock();
    // Not original: a level select picture's name (the load script added "_story" / "_classic" to it in those modes,
    // as to the original picture's): off, the original picture it ends with; on, the picture without the suffix.
    std::array<char, kPickedNameBytes> picked{};
    if (!keep && !black && is_select_name(name)) {
        if (!select_enabled()) {
            name = original_of(name);
        } else {
            text::format_to(picked, "%s", name);
            for (const char* suffix : {"_story", "_classic"}) {
                const size_t length = std::strlen(picked.data());
                const size_t tail = std::strlen(suffix);
                if (length > tail && std::string_view(picked.data(), length).ends_with(suffix)) {
                    core::at(picked, length - tail) = '\0';
                }
            }
            name = picked.data();
        }
    }
    gpu().set_render_state(GpuRenderState::kFogEnable, 0);
    engine_parts().set_loading_screen_up(false);
    if (!keep) {
        if (black) {
            select_picture().select_picture_ = false;
            apply_bar_style(false);
            gpu().clear(0, nullptr, gpu_value::kClearTarget, 0, 1.0F, 0);
        } else {
            if (screen_2d().loading_screen_picture()) {
                if (std::strcmp(picture_name(), name) != 0) {
                    memory::destroy_then_clear(screen_2d().loading_screen_picture());
                    set_picture_name(name);
                }
            }
            memory::let_go(screen_2d().loading_screen_picture()); // (one of the same name is left, as the original leaves it)
            screen_2d().loading_screen_picture() = std::make_unique<render::PcTexture>();
            // the language's picture: French "fr/<name>", German "gr/<name>"
            std::array<char, kPickedNameBytes> localised{};
            const char* file = name;
            const int language = game_config::get_language();
            if (language == 3 && !is_select_name(name)) { // (no text on a level select picture: one for all languages)
                text::format_to(localised, "fr/%s", name);
                file = localised.data();
            } else if (language == core::kLanguageGerman && !is_select_name(name)) {
                text::format_to(localised, "gr/%s", name);
                file = localised.data();
            }
            // (no check: a failed allocation calls through null, as the original)
            const bool loaded = screen_2d().loading_screen_picture()->load_by_name(file, 1, 1);
            if (!loaded) {
                get_unlock();
                return;
            }
            select_picture().select_picture_ = is_select_name(name);
            apply_bar_style(select_picture().select_picture_);
            gpu().begin_scene();
            draw_loading_screen_picture();
            gpu().end_scene();
        }
        gpu().present();
    }
    engine_parts().set_loading_screen_up(true);
    get_unlock();
}

bool select_picture_up() { return select_picture().select_picture_; }

} // namespace render
