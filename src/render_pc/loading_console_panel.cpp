// NOT ORIGINAL - the loading console's panel, drawn over the loading screen (see loading_console.h and
// loading_console_panel.h).

#include "render_pc/loading_console_panel.h"
#include "engine/gfx/loading_console.h"
#include "render_pc/loading_dialog_panel.h"
#include "render_pc/loading_screen.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>
#include <array>

#include "engine/ui/ui_fonts.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "base/core/cursor.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "render_pc/device.h"
#include "engine/gfx/screen_2d.h"
#include "engine/gfx/engine_parts.h"
#include "base/core/packed_colour.h"
#include "base/maths/vec2.h"
#include "render_pc/render_state.h"


namespace gfx::loading_console {

constexpr std::size_t kGlyphCount = 128;
constexpr int kFirstPrintable = 0x20;
constexpr int kPrintableEnd = 0x7F;
constexpr float kLineHeightFactor = 1.2F;
constexpr int kAsciiLimit = 0x80;
constexpr float kPanelWidthShare = 0.45F;
constexpr float kBothSides = 2.0F;
constexpr float kMinPanelColumns = 20.0F;
constexpr float kFallbackPanelColumns = 40.0F;
constexpr uint32_t kPanelStalledColour = 0xC0301000U; // a stalled load: the panel is reddish
constexpr uint32_t kPanelColour = 0xB0000000U;
constexpr float kRuleHeight = 2.0F;
constexpr uint32_t kRuleStalledColour = 0xFFFF8C1AU;
constexpr uint32_t kRuleColour = 0xFF5A5A5AU;
constexpr std::size_t kLeftTextChars = 48;
constexpr std::size_t kRightTextChars = 64;
constexpr uint32_t kTextColour = 0xFFFFFFFFU;
constexpr uint32_t kRightStalledColour = 0xFFFF9A2EU;
constexpr uint32_t kRightColour = 0xFFA0A0A0U;
constexpr float kHalf = 0.5F;
constexpr int kTimerPeriodMilliseconds = 100;
constexpr int kTimerResolutionMilliseconds = 10;
constexpr int kColourWriteAll = 0xF;

namespace {

// The loading screen's lock (0x007052AC: a mutex, held while it draws) and its state.
HANDLE screen_mutex() { return render::screen_2d().screen_mutex(); }

bool load_screen_shown() { return render::engine_parts().loading_screen_up(); } // the main loop draws nothing then

bool screen_picture() { return render::screen_2d().loading_screen_picture() != nullptr; } // the loading screen's picture is up

int progress_bar_pixels() { return render::screen_2d().gradient_width(); } // the loading bar's width in pixels

// --- the font: JetBrains Mono, rasterised once by ImGui's font atlas (no ImGui context: only its atlas) ---

struct Glyph {
    float x0, y0, x1, y1, u0, v0, u1, v1, advance;
    bool visible;
};
// NOT ORIGINAL grouping - the console's font: its glyphs' advances, the line height, the white texel, the texture and
// whether it was tried (were globals). Made on first use, never destroyed (console_font()).
struct ConsoleFont {
    std::array<Glyph, kGlyphCount> glyphs_{};
    float advance_ = 0.0F;     // the font's one advance (monospace)
    float line_height_ = 0.0F; // pixels
    float white_u_ = 0.0F;
    float white_v_ = 0.0F;
    render::GpuTexture* texture_ = nullptr;
    bool font_tried_ = false;
};

ConsoleFont& console_font()
{
    static core::NoDestroy<ConsoleFont> state;
    return state.get();
}

void build_font()
{
    console_font().font_tried_ = true;
    if (!render::device_made()) {
        return;
    }
    const float pixels = std::clamp(
        std::round(static_cast<float>(render::renderer().back_buffer_height()) * 15.0F / 720.0F), 12.0F, 30.0F);
    ImFontAtlas atlas;
    atlas.Flags |= ImFontAtlasFlags_NoMouseCursors;
    ImFontConfig config;
    config.PixelSnapH = true;
    config.OversampleH = 2;
    static const auto kRanges = std::to_array<ImWchar>({0x0020, 0x007E, 0});
    const frontend::ui_fonts::CompressedFont mono = frontend::ui_fonts::get_mono();
    ImFont* font = atlas.AddFontFromMemoryCompressedTTF(mono.data, mono.size, pixels, &config, kRanges.data());
    if (!font || !atlas.Build()) {
        get_log("loading console: the font couldn't be made - no panel");
        return;
    }
    render::GpuTexture* texture = make_coverage_texture(atlas, "loading console");
    if (texture == nullptr) {
        return;
    }
    for (int character = kFirstPrintable; character < kPrintableEnd; ++character) {
        const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(character));
        if (!glyph) {
            continue;
        }
        core::at(console_font().glyphs_, character) = {.x0 = glyph->X0,
                                                       .y0 = glyph->Y0,
                                                       .x1 = glyph->X1,
                                                       .y1 = glyph->Y1,
                                                       .u0 = glyph->U0,
                                                       .v0 = glyph->V0,
                                                       .u1 = glyph->U1,
                                                       .v1 = glyph->V1,
                                                       .advance = glyph->AdvanceX,
                                                       .visible = glyph->Visible != 0};
    }
    console_font().advance_ = core::at(console_font().glyphs_, 'M').advance;
    console_font().line_height_ = std::round(pixels * kLineHeightFactor);
    console_font().white_u_ = atlas.TexUvWhitePixel.x;
    console_font().white_v_ = atlas.TexUvWhitePixel.y;
    console_font().texture_ = texture;
    get_log("loading console: on ([features] loading_console=0 turns it off) - JetBrains Mono %.0f px", pixels);
}

// --- the panel's geometry: laid out at most 10 times a second, drawn with every frame of the loading screen ---

using Vertex = ScreenVertex;
constexpr size_t kShownLines = 12;
constexpr double kLayoutSeconds = 0.1;
constexpr double kStallSeconds = 10.0;
// NOT ORIGINAL grouping - the console's vertices and how many are laid out (were globals). Made on first use, never
// destroyed (console_vertices()).
struct ConsoleVertices {
    std::vector<Vertex> vertices_; // (used with the loading screen's lock held)
    double laid_out_ = -1.0;
};

ConsoleVertices& console_vertices()
{
    static core::NoDestroy<ConsoleVertices> state;
    return state.get();
}

void rect2(float x0, float y0, float x1, float y1, uint32_t color)
{
    const Vertex top_left{.x = x0,
                          .y = y0,
                          .z = 0.0F,
                          .rhw = 1.0F,
                          .color = color,
                          .u = console_font().white_u_,
                          .v = console_font().white_v_};
    const Vertex top_right{.x = x1,
                           .y = y0,
                           .z = 0.0F,
                           .rhw = 1.0F,
                           .color = color,
                           .u = console_font().white_u_,
                           .v = console_font().white_v_};
    const Vertex bottom_left{.x = x0,
                             .y = y1,
                             .z = 0.0F,
                             .rhw = 1.0F,
                             .color = color,
                             .u = console_font().white_u_,
                             .v = console_font().white_v_};
    const Vertex bottom_right{.x = x1,
                              .y = y1,
                              .z = 0.0F,
                              .rhw = 1.0F,
                              .color = color,
                              .u = console_font().white_u_,
                              .v = console_font().white_v_};
    console_vertices().vertices_.insert(console_vertices().vertices_.end(),
                                        {top_left, top_right, bottom_left, top_right, bottom_right, bottom_left});
}

// Text from (x, y) (the line's top), at most `columns` characters.
void draw_text(maths::Vec2 pen, const char* text, size_t columns, colour::Packed colour_word)
{
    float x = pen.x;
    const float y = pen.y;
    const uint32_t color = colour_word.value();
    size_t column = 0;
    for (core::CStringCursor at(text); (*at != '\0') && column < columns; ++at, ++column) {
        auto glyph_code = static_cast<unsigned char>(*at);
        const Glyph& glyph = core::at(console_font().glyphs_, glyph_code < kAsciiLimit ? glyph_code : '?');
        if (glyph.visible) {
            const float x0 = x + glyph.x0 - 0.5F;
            const float y0 = y + glyph.y0 - 0.5F;
            const float x1 = x + glyph.x1 - 0.5F;
            const float y1 = y + glyph.y1 - 0.5F;
            const Vertex top_left{
                .x = x0, .y = y0, .z = 0.0F, .rhw = 1.0F, .color = color, .u = glyph.u0, .v = glyph.v0};
            const Vertex top_right{
                .x = x1, .y = y0, .z = 0.0F, .rhw = 1.0F, .color = color, .u = glyph.u1, .v = glyph.v0};
            const Vertex bottom_left{
                .x = x0, .y = y1, .z = 0.0F, .rhw = 1.0F, .color = color, .u = glyph.u0, .v = glyph.v1};
            const Vertex bottom_right{
                .x = x1, .y = y1, .z = 0.0F, .rhw = 1.0F, .color = color, .u = glyph.u1, .v = glyph.v1};
            console_vertices().vertices_.insert(
                console_vertices().vertices_.end(),
                {top_left, top_right, bottom_left, top_right, bottom_right, bottom_left});
        }
        x += console_font().advance_;
    }
}

void get_layout(double now)
{
    console_vertices().laid_out_ = now;
    console_vertices().vertices_.clear();
    const Snapshot shot = take_snapshot(kShownLines);
    const std::vector<std::string>& lines = shot.lines;
    const double started = shot.started;
    const double last = shot.last_activity;

    auto width = static_cast<float>(render::renderer().back_buffer_width());
    auto height = static_cast<float>(render::renderer().back_buffer_height());
    const float margin = std::max(8.0F, std::round(height * 0.02F));
    const float pad = std::round(console_font().line_height_ * 0.4F);
    // Left of the loading bar (centred, progress_bar_pixels wide, its frame 5/640 of the screen wider each side), at most 45%.
    const float bar_left = ((width - static_cast<float>(progress_bar_pixels())) * 0.5F) - (width * 5.0F / 640.0F);
    float panel_width = std::min(width * kPanelWidthShare, bar_left - (kBothSides * margin));
    if (panel_width < kMinPanelColumns * console_font().advance_) {
        panel_width = std::min(width - (kBothSides * margin),
                               kFallbackPanelColumns * console_font().advance_); // (no room beside the bar: over it)
    }
    const size_t columns = static_cast<size_t>(std::max(1.0F, (panel_width - (2.0F * pad)) / console_font().advance_));
    const float panel_height =
        (2.0F * pad) + (console_font().line_height_ * static_cast<float>(kShownLines + 1)) + (pad * 0.5F);
    const float x0 = margin;
    const float y1 = height - margin;
    const float y0 = y1 - panel_height;
    const float x1 = x0 + panel_width;

    const double idle = now - last;
    const bool stalled = idle >= kStallSeconds;
    rect2(x0, y0, x1, y1, stalled ? kPanelStalledColour : kPanelColour);
    rect2(x0, y0, x1, y0 + kRuleHeight, stalled ? kRuleStalledColour : kRuleColour); // (a rule along the top)

    std::array<char, kLeftTextChars> left{};
    std::array<char, kRightTextChars> right{};
    text::format_to(left, "LOADING %.1f s", now - started);
    if (stalled) {
        text::format_to(right, "NO ACTIVITY - last %.1f s ago", idle);
    } else {
        text::format_to(right, "last activity %.1f s ago", idle);
    }
    float y = y0 + pad;
    draw_text({.x = x0 + pad, .y = y}, left.data(), columns, colour::Packed{kTextColour});
    const size_t right_length = std::strlen(right.data());
    const size_t left_length = std::strlen(left.data());
    if (left_length + 2 + right_length <= columns) {
        draw_text({.x = x1 - pad - (static_cast<float>(right_length) * console_font().advance_), .y = y}, right.data(),
                  columns, colour::Packed{stalled ? kRightStalledColour : kRightColour});
    } else {
        draw_text({.x = x0 + pad + (static_cast<float>(left_length + 2) * console_font().advance_), .y = y},
                  right.data(), columns - std::min(columns, left_length + 2),
                  colour::Packed{stalled ? kRightStalledColour : kRightColour});
    }
    y += console_font().line_height_ + (pad * kHalf);
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool newest = i + 1 == lines.size();
        const uint32_t newest_color = stalled ? 0xFFFFC080U : 0xFFFFFFFFU;
        const uint32_t color = newest ? newest_color : 0xFFB8B8B8U;
        draw_text({.x = x0 + pad, .y = y}, core::at(lines, i).c_str(), columns, colour::Packed{color});
        y += console_font().line_height_;
    }
}

// --- the console's own timer, for a loading screen without a loading bar ---

// NOT ORIGINAL grouping - the console's timer, the draws since it started and whether the screen is blank (the
// original's globals). Made on first use, never destroyed (console_timer()).
struct ConsoleTimer {
    UINT timer_ = 0;
    std::atomic<bool> timer_draws_{false}; // the screen shown wasn't frozen
    std::atomic<bool> blank_{false};
};

ConsoleTimer& console_timer()
{
    static core::NoDestroy<ConsoleTimer> state;
    return state.get();
}

void CALLBACK screen_tick(UINT /*unused*/, UINT /*unused*/, DWORD_PTR /*unused*/, DWORD_PTR /*unused*/,
                          DWORD_PTR /*unused*/)
{
    // Only while the loading bar's own timer isn't running (it draws the panel then), a loading screen is up, it
    // isn't a frozen frame, and no movie is shown. (The editor mode's loading dialog draws in its place:
    // loading_dialog_panel.h.)
    if (!live_now() || !console_timer().timer_draws_ || render::loading_dialog_panel::up() ||
        render::renderer().loading_bar_timer() != 0 || !load_screen_shown() || render::engine_parts().current_movie()) {
        return;
    }
    const bool blank = console_timer().blank_;
    if (!blank && !screen_picture()) {
        return;
    }
    WaitForSingleObject(screen_mutex(), INFINITE);
    render::Gpu& device = render::gpu();
    if (render::device_made() && load_screen_shown() && render::renderer().loading_bar_timer() == 0 && live_now()) {
        device.begin_scene();
        // as the loading screen draws it (0x004B78B0): its picture, or black
        if (blank) {
            device.clear(0, nullptr, render::gpu_value::kClearTarget, 0, 1.0F, 0);
        } else {
            render::draw_loading_screen_picture(); // the picture, full screen
        }
        draw_panel();
        device.end_scene();
        device.present();
    }
    ReleaseMutex(screen_mutex());
}

// The hooks' ends (loading_console.cpp calls them where the console's own code did this).
void prepare_panel()
{
    if (!console_font().font_tried_) {
        build_font();
    }
}

void restart_panel_layout() { console_vertices().laid_out_ = -1.0; }

void start_panel(bool freeze, bool blank)
{
    console_timer().blank_ = blank;
    console_timer().timer_draws_ = !freeze;
    if (!freeze && (console_timer().timer_ == 0U)) {
        console_timer().timer_ = timeSetEvent(kTimerPeriodMilliseconds, kTimerResolutionMilliseconds, &screen_tick, 0, TIME_PERIODIC);
    }
}

void stop_panel()
{
    console_timer().timer_draws_ = false;
    if (console_timer().timer_ != 0U) {
        timeKillEvent(console_timer().timer_);
        console_timer().timer_ = 0;
    }
}

} // namespace

PanelHooks panel_hooks()
{
    return {.prepare = &prepare_panel, .restart_layout = &restart_panel_layout, .start = &start_panel, .stop = &stop_panel};
}

void draw_panel()
{
    if (!live_now() || !console_font().texture_) {
        return;
    }
    if (!render::device_made()) {
        return;
    }
    const double now = get_now();
    if (console_vertices().laid_out_ < 0.0 || now - console_vertices().laid_out_ >= kLayoutSeconds) {
        get_layout(now);
    }
    draw_screen_triangles(console_font().texture_, console_vertices().vertices_, false);
}

void draw_screen_triangles(render::GpuTexture* texture, std::span<const ScreenVertex> vertices, bool smooth)
{
    if (vertices.empty() || !render::device_made()) {
        return;
    }
    render::Gpu& device = render::gpu();
    // The device's state saved and put back after (the renderer's cached state stays true).
    render::GpuSavedState saved;
    if (device.save_state(&saved) < 0) {
        return;
    }
    device.set_pixel_shader(nullptr);
    device.set_vertex_shader(nullptr);
    device.set_vertex_format(render::kScreenVertexFormat);
    device.set_texture(0, texture);
    device.set_texture(1, nullptr);
    device.set_render_state(render::GpuRenderState::kFillMode, render::gpu_value::kFillSolid);
    device.set_render_state(render::GpuRenderState::kZEnable, 0);
    device.set_render_state(render::GpuRenderState::kZWriteEnable, 0);
    device.set_render_state(render::GpuRenderState::kCullMode, render::gpu_value::kCullNone);
    device.set_render_state(render::GpuRenderState::kLighting, 0);
    device.set_render_state(render::GpuRenderState::kFogEnable, 0);
    device.set_render_state(render::GpuRenderState::kStencilEnable, 0);
    device.set_render_state(render::GpuRenderState::kScissorTestEnable, 0);
    device.set_render_state(render::GpuRenderState::kAlphaTestEnable, 0);
    device.set_render_state(render::GpuRenderState::kAlphaBlendEnable, 1);
    device.set_render_state(render::GpuRenderState::kSeparateAlphaBlendEnable, 0);
    device.set_render_state(render::GpuRenderState::kBlendOp, render::gpu_value::kBlendOpAdd);
    device.set_render_state(render::GpuRenderState::kSrcBlend, render::gpu_value::kBlendSrcAlpha);
    device.set_render_state(render::GpuRenderState::kDestBlend, render::gpu_value::kBlendInvSrcAlpha);
    device.set_render_state(render::GpuRenderState::kColourWriteEnable, kColourWriteAll);
    device.set_stage_state(0, render::GpuStageState::kColourOp, render::gpu_value::kOpModulate);
    device.set_stage_state(0, render::GpuStageState::kColourArg1, render::gpu_value::kArgTexture);
    device.set_stage_state(0, render::GpuStageState::kColourArg2, render::gpu_value::kArgDiffuse);
    device.set_stage_state(0, render::GpuStageState::kAlphaOp, render::gpu_value::kOpModulate);
    device.set_stage_state(0, render::GpuStageState::kAlphaArg1, render::gpu_value::kArgTexture);
    device.set_stage_state(0, render::GpuStageState::kAlphaArg2, render::gpu_value::kArgDiffuse);
    device.set_stage_state(0, render::GpuStageState::kTexCoordIndex, 0);
    device.set_stage_state(0, render::GpuStageState::kTextureTransformFlags, render::gpu_value::kTextureTransformDisable);
    device.set_stage_state(1, render::GpuStageState::kColourOp, render::gpu_value::kOpDisable);
    device.set_stage_state(1, render::GpuStageState::kAlphaOp, render::gpu_value::kOpDisable);
    const uint32_t filter = smooth ? render::gpu_value::kFilterLinear : render::gpu_value::kFilterPoint;
    device.set_sampler_state(0, render::GpuSamplerState::kMinFilter, filter);
    device.set_sampler_state(0, render::GpuSamplerState::kMagFilter, filter);
    device.set_sampler_state(0, render::GpuSamplerState::kMipFilter, render::gpu_value::kFilterNone);
    device.set_sampler_state(0, render::GpuSamplerState::kAddressU, render::gpu_value::kAddressClamp);
    device.set_sampler_state(0, render::GpuSamplerState::kAddressV, render::gpu_value::kAddressClamp);
    device.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(vertices.size() / 3),
                            vertices.data(), sizeof(ScreenVertex));
    device.restore_state(saved);
    device.release_saved_state(&saved);
}

render::GpuTexture* make_coverage_texture(ImFontAtlas& atlas, const char* who)
{
    unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
    atlas.GetTexDataAsRGBA32(&rgba, &width, &height);
    render::GpuTexture* texture = nullptr;
    if (rgba == nullptr || render::gpu().create_texture({.width = static_cast<uint32_t>(width),
                                                         .height = static_cast<uint32_t>(height),
                                                         .levels = 1,
                                                         .usage = 0,
                                                         .format = render::gpu_value::kFormatA8R8G8B8,
                                                         .pool = render::gpu_value::kPoolManaged},
                                                        &texture) < 0) {
        get_log("%s: no texture for the font", who);
        return nullptr;
    }
    render::GpuLockedRect locked;
    if (render::gpu().lock_texture(texture, 0, &locked, nullptr, 0) < 0) {
        render::gpu().release_texture(texture);
        get_log("%s: the font's texture couldn't be filled", who);
        return nullptr;
    }
    const std::span<uint8_t> bits(static_cast<uint8_t*>(locked.bits),
                                  static_cast<size_t>(locked.pitch) * static_cast<size_t>(height));
    const std::span<const unsigned char> rgba_pixels(rgba, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int pixel_y = 0; pixel_y < height; ++pixel_y) {
        const std::span<uint8_t> row = bits.subspan(static_cast<size_t>(pixel_y) * static_cast<size_t>(locked.pitch));
        const std::span<const unsigned char> in =
            rgba_pixels.subspan(static_cast<size_t>(pixel_y) * static_cast<size_t>(width) * 4);
        for (int pixel_x = 0; pixel_x < width; ++pixel_x) {
            const uint32_t texel =
                static_cast<uint32_t>(core::at(in, (pixel_x * 4) + 3)) << 24 | 0x00FFFFFFU; // white, coverage as alpha
            std::memcpy(row.subspan(static_cast<size_t>(pixel_x) * 4, sizeof(texel)).data(), &texel, sizeof(texel));
        }
    }
    render::gpu().unlock_texture(texture, 0);
    return texture;
}

} // namespace gfx::loading_console
