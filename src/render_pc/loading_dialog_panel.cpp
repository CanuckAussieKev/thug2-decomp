// NOT ORIGINAL - the editor mode's loading dialog, drawn in place of the loading screen (loading_dialog_panel.h).

#include "render_pc/loading_dialog_panel.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/loading_dialog.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/screen_2d.h"
#include "engine/ui/ui_fonts.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "platform/run_log.h"
#include "render_pc/device.h"
#include "render_pc/loading_console_panel.h"

namespace render::loading_dialog_panel {

namespace {

constexpr int kTimerPeriodMilliseconds = 33;
constexpr int kTimerResolutionMilliseconds = 5;
constexpr double kStallSeconds = 10.0;       // (as the loading console's "no activity")
constexpr float kCircleError = 0.3F;         // (ImGui's style defaults: CircleTessellationMaxError)
constexpr float kCurveTolerance = 1.25F;     // (CurveTessellationTol)
constexpr ImTextureID kFontTexture = 1;      // (the list's one texture: the fonts' - any id but 0)
constexpr std::size_t kReservedVertices = 16384; // (the list grown once, before the load: no allocation after)
constexpr std::size_t kTimeChars = 32;
constexpr float kPixelCentre = 0.5F;
constexpr unsigned kRedShift = 16;
constexpr uint32_t kGreenAlphaMask = 0xFF00FF00U;
constexpr uint32_t kByteMask = 0xFFU;

// The characters a level's title may have (Latin-1).
constexpr auto kRanges = std::to_array<ImWchar>({0x0020, 0x00FF, 0});

// NOT ORIGINAL grouping - the dialog's fonts, its draw list and its timer (the shared data before the list: the list
// points at it). Made on first use, never destroyed (dialog_panel()).
struct DialogPanel {
    std::unique_ptr<ImFontAtlas> atlas;
    gfx::loading_dialog::BoxFonts fonts;
    float ui_pixels = 0.0F;
    float heading_pixels = 0.0F;
    render::GpuTexture* texture = nullptr;
    ImDrawListSharedData shared;
    std::unique_ptr<ImDrawList> list;
    std::vector<gfx::loading_console::ScreenVertex> vertices;
    UINT timer = 0;
    double shown_at = 0.0;
    std::atomic<bool> up{false};
};

DialogPanel& dialog_panel()
{
    static core::NoDestroy<DialogPanel> state;
    return state.get();
}

ImFont* add_font(ImFontAtlas& atlas, frontend::ui_fonts::CompressedFont font, float pixels)
{
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    return atlas.AddFontFromMemoryCompressedTTF(font.data, font.size, pixels, &config, kRanges.data());
}

// The fonts at the editor's sizes (made again when they change), and the list that draws with them.
bool prepare_fonts(const gfx::loading_dialog::Look& look)
{
    DialogPanel& panel = dialog_panel();
    if (panel.texture != nullptr && panel.ui_pixels == look.ui_pixels && panel.heading_pixels == look.heading_pixels) {
        return true;
    }
    if (panel.texture != nullptr) {
        render::gpu().release_texture(panel.texture);
        panel.texture = nullptr;
    }
    panel.atlas = std::make_unique<ImFontAtlas>();
    panel.atlas->Flags |= ImFontAtlasFlags_NoMouseCursors;
    panel.fonts.ui = add_font(*panel.atlas, frontend::ui_fonts::inter(), look.ui_pixels);
    panel.fonts.heading = add_font(*panel.atlas, frontend::ui_fonts::inter_semi_bold(), look.heading_pixels);
    if (panel.fonts.ui == nullptr || panel.fonts.heading == nullptr || !panel.atlas->Build()) {
        get_log("loading dialog: the fonts couldn't be made - an empty page while levels load");
        return false;
    }
    panel.texture = gfx::loading_console::make_coverage_texture(*panel.atlas, "loading dialog");
    if (panel.texture == nullptr) {
        return false;
    }
    panel.atlas->SetTexID(kFontTexture);
    panel.ui_pixels = look.ui_pixels;
    panel.heading_pixels = look.heading_pixels;
    ImDrawListSharedData& shared = panel.shared;
    shared.TexUvWhitePixel = panel.atlas->TexUvWhitePixel;
    shared.TexUvLines = std::data(panel.atlas->TexUvLines);
    shared.Font = panel.fonts.ui;
    shared.FontSize = look.ui_pixels;
    shared.FontScale = 1.0F;
    shared.CurveTessellationTol = kCurveTolerance;
    shared.SetCircleTessellationMaxError(kCircleError);
    shared.InitialFlags = ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
    shared.InitialFringeScale = 1.0F;
    if (!panel.list) {
        panel.list = std::make_unique<ImDrawList>(&panel.shared);
        panel.list->VtxBuffer.reserve(static_cast<int>(kReservedVertices));
        panel.list->IdxBuffer.reserve(static_cast<int>(kReservedVertices * 2));
        panel.vertices.reserve(kReservedVertices * 2);
    }
    return true;
}

// ImGui's colour (red in the low byte) as the device's (blue in the low byte).
uint32_t device_colour(ImU32 colour)
{
    return (colour & kGreenAlphaMask) | ((colour & kByteMask) << kRedShift) | ((colour >> kRedShift) & kByteMask);
}

// What the box says now: the level, the loading bar's time (or a sweep), the loading console's newest line.
gfx::loading_dialog::BoxText box_text(double now)
{
    gfx::loading_dialog::BoxText text;
    const std::string title = gfx::loading_dialog::title();
    // (no level yet: the editor starting - the front end's own load underneath)
    text.caption = title.empty() ? "THUG2 Editor" : "Open Level";
    text.heading = title.empty() ? std::string("Starting...") : "Loading " + title + "...";
    const double seconds = now - dialog_panel().shown_at;
    const float total = screen_2d().total_seconds();
    if (renderer().loading_bar_timer() != 0U && total > 0.0F) {
        text.fraction = std::clamp(screen_2d().elapsed_seconds() / total, 0.0F, 1.0F);
    } else {
        text.fraction = -static_cast<float>(seconds);
    }
    text.status = "Loading";
    if (gfx::loading_console::live_now()) {
        const gfx::loading_console::Snapshot shot = gfx::loading_console::take_snapshot(1);
        if (!shot.lines.empty()) {
            // ("[  1.2] load bo.pre" - the time in brackets left out: the box has its own)
            const std::string& line = shot.lines.back();
            const std::size_t after = line.find("] ");
            text.status = after == std::string::npos ? line : line.substr(after + 2);
        }
        text.stalled = now - shot.last_activity >= kStallSeconds;
    }
    std::array<char, kTimeChars> time{};
    text::format_to(time, "%.1f s", seconds);
    text.time = time.data();
    return text;
}

// The page and the box, inside a scene (the loading screen's lock held).
void draw_now()
{
    DialogPanel& panel = dialog_panel();
    const gfx::loading_dialog::Look look = gfx::loading_dialog::look();
    const auto width = static_cast<float>(renderer().back_buffer_width());
    const auto height = static_cast<float>(renderer().back_buffer_height());
    ImDrawList& list = *panel.list;
    panel.shared.ClipRectFullscreen = ImVec4(0.0F, 0.0F, width, height);
    list._ResetForNewFrame();
    list.PushClipRectFullScreen();
    list.PushTextureID(kFontTexture);
    list.AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(width, height), look.page);
    gfx::loading_dialog::draw_box(&list, look, panel.fonts, box_text(gfx::loading_console::get_now()),
                                  {.origin_x = 0.0F, .origin_y = 0.0F, .width = width, .height = height});
    panel.vertices.clear();
    const std::span<const ImDrawVert> source(list.VtxBuffer.Data, static_cast<std::size_t>(list.VtxBuffer.Size));
    const std::span<const ImDrawIdx> indices(list.IdxBuffer.Data, static_cast<std::size_t>(list.IdxBuffer.Size));
    for (const ImDrawIdx index : indices) {
        const ImDrawVert& vertex = core::at(source, index);
        panel.vertices.push_back({.x = vertex.pos.x - kPixelCentre,
                                  .y = vertex.pos.y - kPixelCentre,
                                  .z = 0.0F,
                                  .rhw = 1.0F,
                                  .color = device_colour(vertex.col),
                                  .u = vertex.uv.x,
                                  .v = vertex.uv.y});
    }
    Gpu& device = gpu();
    device.begin_scene();
    gfx::loading_console::draw_screen_triangles(panel.texture, panel.vertices, true);
    device.end_scene();
    device.present();
}

void CALLBACK dialog_tick(UINT /*unused*/, UINT /*unused*/, DWORD_PTR /*unused*/, DWORD_PTR /*unused*/,
                          DWORD_PTR /*unused*/)
{
    if (!dialog_panel().up) {
        return;
    }
    WaitForSingleObject(screen_2d().screen_mutex(), INFINITE);
    if (dialog_panel().up && device_made() && engine_parts().loading_screen_up() && !engine_parts().current_movie()) {
        draw_now();
    }
    ReleaseMutex(screen_2d().screen_mutex());
}

} // namespace

void show()
{
    DialogPanel& panel = dialog_panel();
    if (!device_made() || !prepare_fonts(gfx::loading_dialog::look())) {
        return;
    }
    if (!panel.up) {
        panel.shown_at = gfx::loading_console::get_now();
        get_log("loading dialog: %s", gfx::loading_dialog::title().c_str());
    }
    panel.up = true;
    draw_now();
    if (panel.timer == 0U) {
        panel.timer = timeSetEvent(kTimerPeriodMilliseconds, kTimerResolutionMilliseconds, &dialog_tick, 0, TIME_PERIODIC);
    }
}

void hide()
{
    DialogPanel& panel = dialog_panel();
    panel.up = false;
    if (panel.timer != 0U) {
        timeKillEvent(panel.timer);
        panel.timer = 0;
    }
}

bool up() { return dialog_panel().up; }

} // namespace render::loading_dialog_panel
