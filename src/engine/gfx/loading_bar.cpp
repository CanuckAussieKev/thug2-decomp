// The loading screen's progress bar: its colour gradient, with PartyMod's bigger buffer built in, and its frame (the
// timer callback that draws the loading screen while the game loads).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "base/core/checked_index.h"
#include "engine/gfx/loading_bar.h"

#include <windows.h>
#include <d3d9.h>
#include <array>

#include "engine/gfx/loading_console.h"
#include "engine/gfx/loading_screen.h"
#include "engine/gfx/render_state.h"
#include "engine/sys/startup_init.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/screen_2d.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/screen.h"

namespace render {

constexpr std::size_t kGradientBufferBytes = 100000;
constexpr uint32_t kOpaqueAlphaBits = 0xFF000000U;
constexpr int kByteMask = 0xFF;
constexpr int kByte1Shift = 8;
constexpr int kByte2Shift = 16;
constexpr uint32_t kBorderAlphaBits = 0x80000000U;
constexpr uint32_t kFrameShadowColour = 0xFF0A0A0A;
constexpr uint32_t kFrameRuleColour = 0xFFF2E205;

namespace {

// PartyMod's gradient table: 100000 bytes, enough for high resolutions (PartyMod's resbuffer, which its
// patchWindow pointed the table at).
struct Rgb {
    int r, g, b;
};

constexpr float kFloatFive{5.0F};
constexpr float kFloatTwenty{20.0F};
constexpr float kPer640{0.0015625F};
constexpr float kFloat420{420.0F};
constexpr float kPer480{0.0020833334F};
// NOT ORIGINAL grouping - loading_bar.cpp's state (the original's globals). Made on first use, never destroyed
// (loading_bar_state()).
struct LoadingBarState {
    uint32_t border_argb_{};
    Rgb gradient_end_{};
    Rgb gradient_start_{};
};

LoadingBarState& loading_bar_state()
{
    static core::NoDestroy<LoadingBarState> state;
    return state.get();
}

// The bar's settings (SetLoadingBarStartColor etc.): colours as r, g, b(, a) bytes, and its width (in
// 640-wide screen units).

Rgb& gradient_start() { return loading_bar_state().gradient_start_; }
Rgb& gradient_end() { return loading_bar_state().gradient_end_; }

uint32_t& border_argb() { return loading_bar_state().border_argb_; }

std::array<Rgb, kGradientBufferBytes / sizeof(Rgb)>& gradient_table()
{
    static core::NoDestroy<std::array<Rgb, kGradientBufferBytes / sizeof(Rgb)>> gradient_buffer;
    return gradient_buffer.get();
}

// The bar's timing (0x004B7400): seconds per tick, seconds so far, seconds for the whole bar.

struct BarVertex {
    float x, y, z, rhw;
    uint32_t color;
};
constexpr std::size_t kBarVertexBytes = 0x14;

static_assert(sizeof(BarVertex) == kBarVertexBytes, "the original's vertex");
constexpr uint32_t kBarVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE; // 0x44

void draw_quad(float x0, float y0, float x1, float y1, uint32_t left, uint32_t right)
{
    // (a strip: top left, bottom left, top right, bottom right)
    std::array<BarVertex, 4> quad = {{{.x = x0, .y = y0, .z = 1.0F, .rhw = 1.0F, .color = left},
                                      {.x = x0, .y = y1, .z = 1.0F, .rhw = 1.0F, .color = left},
                                      {.x = x1, .y = y0, .z = 1.0F, .rhw = 1.0F, .color = right},
                                      {.x = x1, .y = y1, .z = 1.0F, .rhw = 1.0F, .color = right}}};
    renderer().d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(BarVertex));
}
// The C runtime's _ftol2 (0x006236FC): truncated to a 64-bit integer, its low half (NaN and infinities: 0).
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
uint32_t opaque(const Rgb& colour)
{
    return kOpaqueAlphaBits | (colour.r & kByteMask) << kByte2Shift | (colour.g & kByteMask) << kByte1Shift |
           (colour.b & kByteMask);
}

} // namespace

// 0x004B6B80
void update_loading_bar_colors()
{
    gradient_start() = {.r = core::at(screen_2d().progress_from_color(), 0), .g = core::at(screen_2d().progress_from_color(), 1), .b = core::at(screen_2d().progress_from_color(), 2)};
    gradient_end() = {.r = core::at(screen_2d().progress_to_color(), 0), .g = core::at(screen_2d().progress_to_color(), 1), .b = core::at(screen_2d().progress_to_color(), 2)};
    screen_2d().gradient_width() = static_cast<int>(static_cast<double>(renderer().back_buffer_width()) *
                                                    screen_2d().progress_width() * (1.0F / kLayoutWidth));
    border_argb() = kBorderAlphaBits | core::at(screen_2d().progress_edge_color(), 0) << kByte2Shift | core::at(screen_2d().progress_edge_color(), 1) << kByte1Shift | core::at(screen_2d().progress_edge_color(), 2);

    // One colour per pixel column, from the start colour towards the end colour. The original's table is at
    // 0x00701688 and overflows at high resolutions; PartyMod pointed it at a bigger buffer instead, though 4
    // bytes too early (it replaced the instruction's base, which is the table's address + 4). Here the table is
    // at the start of the buffer. Nothing reads the table.
    const int steps = static_cast<int>(static_cast<double>(screen_2d().gradient_width()) * renderer().display_factor_x());
    const Rgb start = gradient_start();
    const Rgb delta = {.r = gradient_end().r - start.r, .g = gradient_end().g - start.g, .b = gradient_end().b - start.b};
    auto& table = gradient_table();
    for (int i = 0; i < steps; ++i) {
        Rgb& entry = core::at(table, i);
        entry.r = (delta.r * i / steps) + gradient_start().r;
        entry.g = (delta.g * i / steps) + gradient_start().g;
        entry.b = (delta.b * i / steps) + gradient_start().b;
    }
}

// 0x004B6FF0: the loading screen's frame - the timer callback 0x004B7400 starts (every 29 ms, on winmm's timer
// thread, while the game loads on its own): the picture, then the bar - its frame (the border colour, half
// transparent), the gradient over its whole width, and the part not reached yet covered with the border colour
// again. The bar fills with time (the seconds DisplayLoadingScreen was given), not with what has loaded; it stops
// full. Not original: the loading console's panel (engine/gfx/loading_console.h) is drawn before the scene ends.
void __stdcall draw_loading_screen_frame(unsigned int /*unused*/, unsigned int /*unused*/, unsigned long /*unused*/,
                                         unsigned long /*unused*/, unsigned long /*unused*/)
{
    if ((renderer().loading_bar_timer() == 0U) || !engine_parts().loading_screen_up()) {
        return;
    }
    WaitForSingleObject(screen_2d().screen_mutex(), INFINITE);
    renderer().d3d_device()->BeginScene();
    render::draw_loading_screen_picture(); // the picture, full screen

    // (x87 at double precision: the intermediate results unrounded, as in the original - only the ones it stores
    // are floats)
    const double x_scale = static_cast<double>(renderer().back_buffer_width()) * kPer640;               // 1 / 640
    auto y_scale = static_cast<float>(static_cast<double>(renderer().back_buffer_height()) * kPer480);  // 1 / 480
    const double elapsed = static_cast<double>(screen_2d().tick_seconds()) + screen_2d().elapsed_seconds();
    screen_2d().set_elapsed_seconds(static_cast<float>(elapsed));
    double fraction = elapsed / screen_2d().total_seconds();
    fraction = std::min(fraction, 1.0);
    const int left = (renderer().back_buffer_width() - screen_2d().gradient_width()) / 2;
    const int frame_left = ftol(static_cast<double>(static_cast<float>(left)) - (x_scale * kFloatFive)); // 5
    const int filled = ftol(screen_2d().gradient_width() * fraction) + left;
    const int frame_right = ftol((screen_2d().gradient_width() + frame_left) + (x_scale * kFloatTen)); // 10
    const int top = ftol(y_scale * static_cast<double>(kFloat420));                       // 420
    const int bottom = ftol(top + (y_scale * static_cast<double>(kFloatTwenty)));         // 20
    auto top_f = static_cast<float>(top);
    auto bottom_f = static_cast<float>(bottom);
    auto frame_left_f = static_cast<float>(frame_left);
    auto frame_right_f = static_cast<float>(frame_right);

    set_texture(0, nullptr);
    renderer().d3d_device()->SetRenderState(D3DRS_ZENABLE, FALSE);
    set_vertex_format(kBarVertexFormat);
    if (render::select_picture_up()) {
        // Not original - the level select pictures' plate: ink behind the bar (opaque, so it reads on any picture) with a
        // safety yellow rule on top, a little wider and taller than the bar's frame.
        const auto pad_x = static_cast<float>(x_scale * 18.0);
        const float pad_y = y_scale * 14.0F;
        const float rule = y_scale * 2.0F;
        draw_quad(frame_left_f - pad_x, top_f - pad_y, frame_right_f + pad_x, bottom_f + pad_y, kFrameShadowColour, kFrameShadowColour);
        draw_quad(frame_left_f - pad_x, top_f - pad_y, frame_right_f + pad_x, top_f - pad_y + rule, kFrameRuleColour,
                  kFrameRuleColour);
    }
    draw_quad(frame_left_f, top_f, frame_right_f, bottom_f, border_argb(), border_argb());
    // the gradient, inset 5/480 of the screen's height top and bottom
    const double inset = y_scale * static_cast<double>(kFloatFive); // 5
    auto gradient_top = static_cast<float>(top_f + inset);
    auto gradient_bottom = static_cast<float>(bottom_f - static_cast<double>(static_cast<float>(inset)));
    draw_quad(static_cast<float>(left), gradient_top, static_cast<float>(screen_2d().gradient_width() + left), gradient_bottom,
              opaque(gradient_start()), opaque(gradient_end()));
    // what isn't filled yet - from the fill's end to the frame's right edge, the frame's height: its colour again
    draw_quad(static_cast<float>(filled), top_f, frame_right_f, bottom_f, border_argb(), border_argb());
    renderer().d3d_device()->SetRenderState(D3DRS_ZENABLE, TRUE);
    // Not original - the loading console (nothing unless a loading screen is up with [features] loading_console on).
    gfx::loading_console::draw_panel();
    renderer().d3d_device()->EndScene();
    renderer().d3d_device()->Present(nullptr, nullptr, nullptr, nullptr);
    ReleaseMutex(screen_2d().screen_mutex());
}

} // namespace render

namespace render {

// 0x00640D10 (a static initialiser): the loading screen's lock, a mutex nobody owns.
void create_screen_mutex() { screen_2d().screen_mutex() = CreateMutexA(nullptr, FALSE, nullptr); }

} // namespace render
