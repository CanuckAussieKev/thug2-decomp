// The loading screen's progress bar: its colour gradient, with PartyMod's bigger buffer built in, and its frame (the
// timer callback that draws the loading screen while the game loads).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include <cstdint>

#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/loading_bar.h"

#include <windows.h>
#include <d3d9.h>
#include <array>

#include "engine/gfx/loading_console.h"
#include "engine/gfx/loading_screen.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/sys/startup_init.h"
#include "engine/core/bits_as.h"
#include "engine/core/raw_view.h"

namespace render {

int g_gradient_width;
void* g_screen_mutex; // the loading screen's lock (a HANDLE), held while it is drawn
float g_tick_seconds;
float g_elapsed_seconds;
float g_total_seconds;

float g_float_ten{10.0f};
std::array<int, 2> g_bar_position{{258, 400}};
int g_progress_width{140};
int g_progress_height{8};
std::array<int, 2> g_bar_border{{5, 5}};
std::array<uint8_t, 4> g_progress_from_color{{0x00, 0x4C, 0x81, 0x80}};
std::array<uint8_t, 4> g_progress_to_color{{0xAC, 0xD3, 0x73, 0x80}};
std::array<uint8_t, 4> g_progress_edge_color{{0x28, 0x28, 0x28, 0x80}};
namespace {

// PartyMod's gradient table: 100000 bytes, enough for high resolutions (PartyMod's resbuffer, which its
// patchWindow pointed the table at).
std::array<uint8_t, 100000> s_gradient_buffer;

struct Rgb {
    int r, g, b;
};

float g_float_five{5.0f};
float g_float_twenty{20.0f};
float g_per640{0.0015625f};
float g_float420{420.0f};
float g_per480{0.0020833334f};
uint32_t g_border_argb;
Rgb g_gradient_end;
Rgb g_gradient_start;

// The bar's settings (SetLoadingBarStartColor etc.): colours as r, g, b(, a) bytes, and its width (in
// 640-wide screen units).
const uint8_t* start_color() { return g_progress_from_color.data(); }
const uint8_t* end_color() { return g_progress_to_color.data(); }
const uint8_t* border_color() { return g_progress_edge_color.data(); }
int& progress_bar_pixels() { return g_progress_width; }
int& get_screen_width() { return g_back_buffer_width; }
float& horiz_screen_ratio() { return g_display_factor_x; }

Rgb& gradient_start() { return g_gradient_start; }
Rgb& gradient_end() { return g_gradient_end; }
int& gradient_width() { return g_gradient_width; }
uint32_t& border_argb() { return g_border_argb; }

Rgb* gradient_table() { return raw::as<Rgb>(s_gradient_buffer.data()); }

// The bar's timing (0x004B7400): seconds per tick, seconds so far, seconds for the whole bar.
float& tick_seconds() { return g_tick_seconds; }
float& elapsed_seconds() { return g_elapsed_seconds; }
float& get_total_seconds() { return g_total_seconds; }
int& get_screen_height() { return g_back_buffer_height; }
uint32_t& bar_timer() { return g_loading_bar_timer; }     // the timer's id (timeSetEvent)
bool& load_screen_shown() { return g_loading_screen_up; } // shown (0x004B78B0): the main loop doesn't draw
HANDLE& screen_mutex() { return memory::bits_as<HANDLE>(g_screen_mutex); } // held while the loading screen is drawn
IDirect3DDevice9* input_port() { return g_d3d_device; }

struct BarVertex {
    float x, y, z, rhw;
    uint32_t color;
};
static_assert(sizeof(BarVertex) == 0x14, "the original's vertex");
constexpr uint32_t kBarVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE; // 0x44

void draw_quad(float x0, float y0, float x1, float y1, uint32_t left, uint32_t right)
{
    // (a strip: top left, bottom left, top right, bottom right)
    std::array<BarVertex, 4> quad = {{{x0, y0, 1.0f, 1.0f, left},
                         {x0, y1, 1.0f, 1.0f, left},
                         {x1, y0, 1.0f, 1.0f, right},
                         {x1, y1, 1.0f, 1.0f, right}}};
    input_port()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(BarVertex));
}
// The C runtime's _ftol2 (0x006236FC): truncated to a 64-bit integer, its low half (NaN and infinities: 0).
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
uint32_t opaque(const Rgb& c) { return 0xFF000000u | (c.r & 0xFF) << 16 | (c.g & 0xFF) << 8 | (c.b & 0xFF); }

} // namespace

uint8_t* loading_bar_gradient_buffer() { return s_gradient_buffer.data(); }

// 0x004B6B80
void update_loading_bar_colors()
{
    gradient_start() = {start_color()[0], start_color()[1], start_color()[2]};
    gradient_end() = {end_color()[0], end_color()[1], end_color()[2]};
    gradient_width() =
        static_cast<int>(static_cast<double>(get_screen_width()) * progress_bar_pixels() * (1.0f / 640.0f));
    border_argb() = 0x80000000u | border_color()[0] << 16 | border_color()[1] << 8 | border_color()[2];

    // One colour per pixel column, from the start colour towards the end colour. The original's table is at
    // 0x00701688 and overflows at high resolutions; PartyMod pointed it at a bigger buffer instead, though 4
    // bytes too early (it replaced the instruction's base, which is the table's address + 4). Here the table is
    // at the start of the buffer. Nothing reads the table.
    const int steps = static_cast<int>(static_cast<double>(gradient_width()) * horiz_screen_ratio());
    const Rgb start = gradient_start();
    const Rgb delta = {gradient_end().r - start.r, gradient_end().g - start.g, gradient_end().b - start.b};
    Rgb* table = gradient_table();
    for (int i = 0; i < steps; ++i) {
        table[i].r = delta.r * i / steps + gradient_start().r;
        table[i].g = delta.g * i / steps + gradient_start().g;
        table[i].b = delta.b * i / steps + gradient_start().b;
    }
}

// 0x004B6FF0: the loading screen's frame - the timer callback 0x004B7400 starts (every 29 ms, on winmm's timer
// thread, while the game loads on its own): the picture, then the bar - its frame (the border colour, half
// transparent), the gradient over its whole width, and the part not reached yet covered with the border colour
// again. The bar fills with time (the seconds DisplayLoadingScreen was given), not with what has loaded; it stops
// full. Not original: the loading console's panel (engine/gfx/loading_console.h) is drawn before the scene ends.
void __stdcall draw_loading_screen_frame(unsigned int, unsigned int, unsigned long, unsigned long, unsigned long)
{
    if (!bar_timer() || !load_screen_shown())
        return;
    WaitForSingleObject(screen_mutex(), INFINITE);
    input_port()->BeginScene();
    render::draw_loading_screen_picture(); // the picture, full screen

    // (x87 at double precision: the intermediate results unrounded, as in the original - only the ones it stores
    // are floats)
    const double x_scale = static_cast<double>(get_screen_width()) * g_per640;               // 1 / 640
    auto y_scale = static_cast<float>(static_cast<double>(get_screen_height()) * g_per480);  // 1 / 480
    const double elapsed = static_cast<double>(tick_seconds()) + elapsed_seconds();
    elapsed_seconds() = static_cast<float>(elapsed);
    double fraction = elapsed / get_total_seconds();
    if (fraction > 1.0)
        fraction = 1.0;
    const int left = (get_screen_width() - gradient_width()) / 2;
    const int frame_left = ftol(static_cast<double>(static_cast<float>(left)) - x_scale * g_float_five); // 5
    const int filled = ftol(gradient_width() * fraction) + left;
    const int frame_right = ftol((gradient_width() + frame_left) + x_scale * g_float_ten); // 10
    const int top = ftol(y_scale * static_cast<double>(g_float420));                       // 420
    const int bottom = ftol(top + y_scale * static_cast<double>(g_float_twenty));          // 20
    auto top_f = static_cast<float>(top), bottom_f = static_cast<float>(bottom);
    auto frame_left_f = static_cast<float>(frame_left), frame_right_f = static_cast<float>(frame_right);

    set_texture(0, nullptr);
    input_port()->SetRenderState(D3DRS_ZENABLE, FALSE);
    set_vertex_format(kBarVertexFormat);
    if (render::select_picture_up()) {
        // Not original - the level select pictures' plate: ink behind the bar (opaque, so it reads on any picture) with a
        // safety yellow rule on top, a little wider and taller than the bar's frame.
        const auto pad_x = static_cast<float>(x_scale * 18.0);
        const float pad_y = y_scale * 14.0f;
        const float rule = y_scale * 2.0f;
        draw_quad(frame_left_f - pad_x, top_f - pad_y, frame_right_f + pad_x, bottom_f + pad_y, 0xFF0A0A0A, 0xFF0A0A0A);
        draw_quad(frame_left_f - pad_x, top_f - pad_y, frame_right_f + pad_x, top_f - pad_y + rule, 0xFFF2E205,
                  0xFFF2E205);
    }
    draw_quad(frame_left_f, top_f, frame_right_f, bottom_f, border_argb(), border_argb());
    // the gradient, inset 5/480 of the screen's height top and bottom
    const double inset = y_scale * static_cast<double>(g_float_five); // 5
    auto gradient_top = static_cast<float>(top_f + inset);
    auto gradient_bottom = static_cast<float>(bottom_f - static_cast<double>(static_cast<float>(inset)));
    draw_quad(static_cast<float>(left), gradient_top, static_cast<float>(gradient_width() + left), gradient_bottom,
              opaque(gradient_start()), opaque(gradient_end()));
    // what isn't filled yet - from the fill's end to the frame's right edge, the frame's height: its colour again
    draw_quad(static_cast<float>(filled), top_f, frame_right_f, bottom_f, border_argb(), border_argb());
    input_port()->SetRenderState(D3DRS_ZENABLE, TRUE);
    // Not original - the loading console (nothing unless a loading screen is up with [features] loading_console on).
    gfx::loading_console::draw_panel();
    input_port()->EndScene();
    input_port()->Present(nullptr, nullptr, nullptr, nullptr);
    ReleaseMutex(screen_mutex());
}

} // namespace render

namespace render {

// 0x00640D10 (a static initialiser): the loading screen's lock, a mutex nobody owns.
void create_screen_mutex() { g_screen_mutex = CreateMutexA(nullptr, FALSE, nullptr); }

} // namespace render
