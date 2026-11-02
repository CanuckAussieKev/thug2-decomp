// The loading screen: see loading_screen.h.

#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/loading_screen.h"
#include "engine/gfx/engine_sprite.h"

#include <windows.h>

#include <d3d9.h>
#include <mmsystem.h>

#include <cstddef>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>
#include <iterator>

#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/loading_bar.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "platform/run_log.h"
#include "engine/sys/game_config.h"
#include "engine/core/checked_index.h"
#include <string>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/shared_state.h"

namespace render {

LoadingScreenShared& loading_screen_shared()
{
    static core::NoDestroy<LoadingScreenShared> state;
    return state.get();
}

namespace {

HANDLE screen_mutex() { return loading_bar_shared().screen_mutex_; }


// The Windows calls, through the exe's imports (as device.cpp's timer: so a test can stand in for them alone).
void get_lock() { WaitForSingleObject(screen_mutex(), INFINITE); }
void get_unlock() { ReleaseMutex(screen_mutex()); }
UINT start_timer(UINT delay, LPTIMECALLBACK callback) { return timeSetEvent(delay, 0, callback, 0, TIME_PERIODIC); }
void stop_timer(UINT timer) { timeKillEvent(timer); }

// NOT ORIGINAL grouping - loading_screen.cpp's state (the original's globals). Made on first use, never destroyed
// (loading_screen_state()).
struct LoadingScreenState {
    std::unique_ptr<render::BillboardQuad> loading_screen_sprite_{}; // (nothing in this build sets it)
    std::string picture_name_{}; // the picture loaded
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
constexpr float kSelectAspect = 16.0f / 9.0f;
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
    if (select == select_picture().bar_styled_)
        return;
    if (select) {
        select_picture().saved_bar_width_ = loading_bar_shared().progress_width_;
        std::memcpy(core::at(select_picture().saved_colors_, 0).data(), loading_bar_shared().progress_from_color_.data(), 4);
        std::memcpy(core::at(select_picture().saved_colors_, 1).data(), loading_bar_shared().progress_to_color_.data(), 4);
        std::memcpy(core::at(select_picture().saved_colors_, 2).data(), loading_bar_shared().progress_edge_color_.data(), 4);
        loading_bar_shared().progress_width_ = 300;
        loading_bar_shared().progress_from_color_ = {0xB9, 0xB5, 0xAC, 0x80};
        loading_bar_shared().progress_to_color_ = {0xF2, 0xE2, 0x05, 0x80};
        loading_bar_shared().progress_edge_color_ = {0x3A, 0x3A, 0x38, 0x80};
    } else {
        loading_bar_shared().progress_width_ = select_picture().saved_bar_width_;
        std::memcpy(loading_bar_shared().progress_from_color_.data(), core::at(select_picture().saved_colors_, 0).data(), 4);
        std::memcpy(loading_bar_shared().progress_to_color_.data(), core::at(select_picture().saved_colors_, 1).data(), 4);
        std::memcpy(loading_bar_shared().progress_edge_color_.data(), core::at(select_picture().saved_colors_, 2).data(), 4);
    }
    select_picture().bar_styled_ = select;
    update_loading_bar_colors();
}

struct PictureVertex {
    float x, y, z, rhw;
    float u, v;
};
static_assert(sizeof(PictureVertex) == 0x18, "the original's vertex");

} // namespace

// 0x004B6CD0
void draw_loading_screen_picture()
{
    if (!loading_screen_shared().loading_screen_picture_)
        return;
    get_lock();
    d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    d3d_device()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    DWORD min_filter = 0;
    d3d_device()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    d3d_device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    change_phase(kAddressMode0, 0x10001); // (clamped both ways)
    change_phase(kLighting, 0);
    set_pixel_shader(nullptr, nullptr);
    set_vertex_format(D3DFVF_XYZRHW | D3DFVF_TEX1);
    choose_blend_mode(0);
    set_texture(0, nullptr); // (the original passes a third 0, here and below)
    set_texture(0, image_of(loading_screen_shared().loading_screen_picture_.get())->texture); // (and its palette)
    auto width = static_cast<float>(back_buffer_width());
    auto height = static_cast<float>(back_buffer_height());
    // (not original: the level select's pictures are 16:9 and cover the screen - the overflow of another shape cut off
    // evenly - where the original's are stretched)
    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    if (select_picture().select_picture_ && width > 0.0f && height > 0.0f) {
        const float aspect = width / height;
        if (aspect < kSelectAspect) {
            u0 = (1.0f - aspect / kSelectAspect) * 0.5f;
            u1 = 1.0f - u0;
        } else {
            v0 = (1.0f - kSelectAspect / aspect) * 0.5f;
            v1 = 1.0f - v0;
        }
    }
    const std::array<PictureVertex, 4> quad = {{
        {.x = 0.0f, .y = 0.0f, .z = 0.0f, .rhw = 1.0f, .u = u0, .v = v1},
        {.x = 0.0f, .y = height, .z = 0.0f, .rhw = 1.0f, .u = u0, .v = v0},
        {.x = width, .y = 0.0f, .z = 0.0f, .rhw = 1.0f, .u = u1, .v = v1},
        {.x = width, .y = height, .z = 0.0f, .rhw = 1.0f, .u = u1, .v = v0},
    }};
    d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(PictureVertex));
    set_texture(0, nullptr);
    d3d_device()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    get_unlock();
}

// 0x004B6F10
void hide_loading_screen()
{
    get_lock();
    d3d_device()->SetRenderState(D3DRS_FOGENABLE, fog_active());
    if (loading_bar_timer()) {
        stop_timer(loading_bar_timer());
        set_loading_bar_timer(0);
    }
    set_loading_screen_up(false);
    get_unlock();
}

// 0x004B6F70
void free_loading_screen_sprite()
{
    get_lock();
    d3d_device()->SetRenderState(D3DRS_FOGENABLE, fog_active());
    memory::destroy_then_clear(loading_screen_state().loading_screen_sprite_); // (the engine's delete through slot 0)
    get_unlock();
}

// 0x004B7400
void show_progress_bar(float seconds)
{
    loading_bar_shared().elapsed_seconds_ = 0.0f;                                                  // elapsed
    loading_bar_shared().tick_seconds_ = 0.03f;                                                    // a tick's seconds (0x3CF5C28F)
    loading_bar_shared().total_seconds_ = static_cast<float>(static_cast<double>(seconds) * 0.6f); // (0x006463C4)
    update_loading_bar_colors();
    if (!loading_bar_timer())
        set_loading_bar_timer(start_timer(29, &draw_loading_screen_frame));
}

// 0x004B78B0
void show_loading_screen(const char* name, bool keep, bool black)
{
    get_lock();
    // Not original: a level select picture's name (the load script added "_story" / "_classic" to it in those modes,
    // as to the original picture's): off, the original picture it ends with; on, the picture without the suffix.
    std::array<char, 0xC8> picked{};
    if (!keep && !black && is_select_name(name)) {
        if (!select_enabled()) {
            name = original_of(name);
        } else {
            text::format_to(picked, "%s", name);
            for (const char* suffix : {"_story", "_classic"}) {
                const size_t length = std::strlen(picked.data());
                const size_t tail = std::strlen(suffix);
                if (length > tail && std::string_view(picked.data(), length).ends_with(suffix))
                    core::at(picked, length - tail) = '\0';
            }
            name = picked.data();
        }
    }
    d3d_device()->SetRenderState(D3DRS_FOGENABLE, FALSE);
    set_loading_screen_up(false);
    if (!keep) {
        if (black) {
            select_picture().select_picture_ = false;
            apply_bar_style(false);
            d3d_device()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        } else {
            if (loading_screen_shared().loading_screen_picture_) {
                if (std::strcmp(picture_name(), name) != 0) {
                    memory::destroy_then_clear(loading_screen_shared().loading_screen_picture_);
                    set_picture_name(name);
                }
            }
            memory::let_go(loading_screen_shared().loading_screen_picture_); // (one of the same name is left, as the original leaves it)
            loading_screen_shared().loading_screen_picture_ = std::make_unique<render::PcTexture>();
            // the language's picture: French "fr/<name>", German "gr/<name>"
            std::array<char, 0xC8> localised{};
            const char* file = name;
            const int language = game_config::get_language();
            if (language == 3 && !is_select_name(name)) { // (no text on a level select picture: one for all languages)
                text::format_to(localised, "fr/%s", name);
                file = localised.data();
            } else if (language == 5 && !is_select_name(name)) {
                text::format_to(localised, "gr/%s", name);
                file = localised.data();
            }
            // (no check: a failed allocation calls through null, as the original)
            const bool loaded = loading_screen_shared().loading_screen_picture_->load_by_name(file, 1, 1);
            if (!loaded) {
                get_unlock();
                return;
            }
            select_picture().select_picture_ = is_select_name(name);
            apply_bar_style(select_picture().select_picture_);
            d3d_device()->BeginScene();
            draw_loading_screen_picture();
            d3d_device()->EndScene();
        }
        d3d_device()->Present(nullptr, nullptr, nullptr, nullptr);
    }
    set_loading_screen_up(true);
    get_unlock();
}

bool select_picture_up() { return select_picture().select_picture_; }

} // namespace render
