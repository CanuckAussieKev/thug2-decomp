// The loading screen: see loading_screen.h.

#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/loading_screen.h"
#include "engine/gfx/engine_sprite.h"

#include <windows.h>

#include <d3d9.h>
#include <mmsystem.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>

#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/loading_bar.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "platform/crt.h"
#include "platform/run_log.h"
#include "engine/sys/game_config.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"
#include <string>

namespace render {

gsl::owner<render::BitmapTexture*> g_loading_screen_picture;

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
HANDLE screen_mutex() { return g_screen_mutex; }
bool& load_screen_shown() { return g_loading_screen_up; }
uint32_t& bar_timer() { return g_loading_bar_timer; }
uint32_t fog_render_flag() { return g_fog_active; } // (the renderer's cached D3DRS_FOGENABLE)

// The Windows calls, through the exe's imports (as device.cpp's timer: so a test can stand in for them alone).
void get_lock() { WaitForSingleObject(screen_mutex(), INFINITE); }
void get_unlock() { ReleaseMutex(screen_mutex()); }
UINT start_timer(UINT delay, LPTIMECALLBACK callback) { return timeSetEvent(delay, 0, callback, 0, TIME_PERIODIC); }
void stop_timer(UINT timer) { timeKillEvent(timer); }

render::BillboardQuad* g_loading_screen_sprite;     // (nothing in this build sets it)
std::string g_picture_name; // the picture loaded

// The picture (a texture holder, 0x14 bytes: 0x004C7ED0; its texture at +0x10, through one more pointer with flag 1
// of +0x08) and the loading screen's other sprite.
render::BillboardQuad*& get_sprite() { return g_loading_screen_sprite; }

// The picture's name (a std::string in the original too: its assign at 0x004B7880 / 0x004B7750).
const char* picture_name() { return g_picture_name.c_str(); }
void set_picture_name(const char* text) { g_picture_name = text; }

// NOT ORIGINAL - the level select's loading pictures (tools/pipeline menu_level/loading_select.ts): a level whose
// loading_screen is "loadscrn_sel_<load>__<the original picture>" shows the level select's picture (16:9, text-free);
// [features] level_select_loading_screens=0 loads the original picture named after the "__" instead.
constexpr auto kSelectPrefix = std::to_array("loadscrn_sel_");
constexpr float kSelectAspect = 16.0f / 9.0f;
bool s_select_picture = false; // the picture up is one of those (drawn cover-fitted, with the bar's plate)
bool s_bar_styled = false;     // the bar's own settings are saved and ours are in
int s_saved_bar_width = 0;
std::array<std::array<uint8_t, 4>, 3> s_saved_colors = {};

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
    return mark ? mark + 2 : name;
}

// The bar's look over those pictures (the front end's: paper to safety yellow on ink; wider than the original's 140).
void apply_bar_style(bool select)
{
    if (select == s_bar_styled)
        return;
    if (select) {
        s_saved_bar_width = g_progress_width;
        std::memcpy(core::at(s_saved_colors, 0).data(), g_progress_from_color.data(), 4);
        std::memcpy(core::at(s_saved_colors, 1).data(), g_progress_to_color.data(), 4);
        std::memcpy(core::at(s_saved_colors, 2).data(), g_progress_edge_color.data(), 4);
        g_progress_width = 300;
        g_progress_from_color = {0xB9, 0xB5, 0xAC, 0x80};
        g_progress_to_color = {0xF2, 0xE2, 0x05, 0x80};
        g_progress_edge_color = {0x3A, 0x3A, 0x38, 0x80};
    } else {
        g_progress_width = s_saved_bar_width;
        std::memcpy(g_progress_from_color.data(), core::at(s_saved_colors, 0).data(), 4);
        std::memcpy(g_progress_to_color.data(), core::at(s_saved_colors, 1).data(), 4);
        std::memcpy(g_progress_edge_color.data(), core::at(s_saved_colors, 2).data(), 4);
    }
    s_bar_styled = select;
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
    if (!g_loading_screen_picture)
        return;
    get_lock();
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    input_port()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    DWORD min_filter = 0;
    input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    input_port()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    change_phase(kAddressMode0, 0x10001); // (clamped both ways)
    change_phase(kLighting, 0);
    set_pixel_shader(nullptr, nullptr);
    set_vertex_format(D3DFVF_XYZRHW | D3DFVF_TEX1);
    choose_blend_mode(0);
    set_texture(0, nullptr); // (the original passes a third 0, here and below)
    set_texture(0, image_of(g_loading_screen_picture)->texture); // (and its palette)
    auto width = static_cast<float>(g_back_buffer_width);
    auto height = static_cast<float>(g_back_buffer_height);
    // (not original: the level select's pictures are 16:9 and cover the screen - the overflow of another shape cut off
    // evenly - where the original's are stretched)
    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    if (s_select_picture && width > 0.0f && height > 0.0f) {
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
        {0.0f, 0.0f, 0.0f, 1.0f, u0, v1},
        {0.0f, height, 0.0f, 1.0f, u0, v0},
        {width, 0.0f, 0.0f, 1.0f, u1, v1},
        {width, height, 0.0f, 1.0f, u1, v0},
    }};
    input_port()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(PictureVertex));
    set_texture(0, nullptr);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
    get_unlock();
}

// 0x004B6F10
void hide_loading_screen()
{
    get_lock();
    input_port()->SetRenderState(D3DRS_FOGENABLE, fog_render_flag());
    if (bar_timer()) {
        stop_timer(bar_timer());
        bar_timer() = 0;
    }
    load_screen_shown() = false;
    get_unlock();
}

// 0x004B6F70
void free_loading_screen_sprite()
{
    get_lock();
    input_port()->SetRenderState(D3DRS_FOGENABLE, fog_render_flag());
    if (get_sprite()) {
        delete get_sprite(); // (the engine's delete through slot 0)
        get_sprite() = nullptr;
    }
    get_unlock();
}

// 0x004B7400
void show_progress_bar(float seconds)
{
    g_elapsed_seconds = 0.0f;                                                  // elapsed
    g_tick_seconds = 0.03f;                                                    // a tick's seconds (0x3CF5C28F)
    g_total_seconds = static_cast<float>(static_cast<double>(seconds) * 0.6f); // (0x006463C4)
    update_loading_bar_colors();
    if (!bar_timer())
        bar_timer() = start_timer(29, &draw_loading_screen_frame);
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
            std::snprintf(picked.data(), sizeof(picked), "%s", name);
            for (const char* suffix : {"_story", "_classic"}) {
                const size_t length = std::strlen(picked.data());
                const size_t tail = std::strlen(suffix);
                if (length > tail && std::strcmp(picked.data() + length - tail, suffix) == 0)
                    core::at(picked, length - tail) = '\0';
            }
            name = picked.data();
        }
    }
    input_port()->SetRenderState(D3DRS_FOGENABLE, FALSE);
    load_screen_shown() = false;
    if (!keep) {
        if (black) {
            s_select_picture = false;
            apply_bar_style(false);
            input_port()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        } else {
            if (g_loading_screen_picture) {
                if (std::strcmp(picture_name(), name) != 0) {
                    delete g_loading_screen_picture;
                    g_loading_screen_picture = nullptr;
                    set_picture_name(name);
                }
            }
            g_loading_screen_picture = new render::PcTexture;
            // the language's picture: French "fr/<name>", German "gr/<name>"
            std::array<char, 0xC8> localised{};
            const char* file = name;
            const int language = game_config::g_language;
            if (language == 3 && !is_select_name(name)) { // (no text on a level select picture: one for all languages)
                std::snprintf(localised.data(), sizeof(localised), "fr/%s", name);
                file = localised.data();
            } else if (language == 5 && !is_select_name(name)) {
                std::snprintf(localised.data(), sizeof(localised), "gr/%s", name);
                file = localised.data();
            }
            // (no check: a failed allocation calls through null, as the original)
            const bool loaded = g_loading_screen_picture->load_by_name(file, 1, 1);
            if (!loaded) {
                get_unlock();
                return;
            }
            s_select_picture = is_select_name(name);
            apply_bar_style(s_select_picture);
            input_port()->BeginScene();
            draw_loading_screen_picture();
            input_port()->EndScene();
        }
        input_port()->Present(nullptr, nullptr, nullptr, nullptr);
    }
    load_screen_shown() = true;
    get_unlock();
}

bool select_picture_up() { return s_select_picture; }

} // namespace render
