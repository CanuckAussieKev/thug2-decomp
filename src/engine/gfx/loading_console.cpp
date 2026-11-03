// NOT ORIGINAL - the loading console (see loading_console.h).

#include "engine/gfx/loading_console.h"
#include "engine/gfx/loading_screen.h"

#include <windows.h>
#include <d3d9.h>
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
#include <string_view>
#include <vector>
#include <array>

#include "editor/level_editor_ui_fonts.h"
#include "engine/gfx/movie.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/util/com_ptr.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/screen_2d.h"
#include "engine/gfx/engine_parts.h"
#include "engine/core/packed_colour.h"
#include "engine/maths/vec2.h"
#include "engine/gfx/render_state.h"

namespace gfx::loading_console {

constexpr std::size_t kSizeTextChars = 32;
constexpr int kKilobyteBytes = 1024;
constexpr long long kKilobyteBytesLL = 1024LL;
constexpr double kKilobyteBytesD = 1024.0;
constexpr std::size_t kLineSlack = 32;
constexpr std::size_t kGlyphCount = 128;
constexpr int kFirstPrintable = 0x20;
constexpr int kPrintableEnd = 0x7F;
constexpr float kLineHeightFactor = 1.2f;
constexpr int kAsciiLimit = 0x80;
constexpr float kPanelWidthShare = 0.45f;
constexpr float kBothSides = 2.0f;
constexpr float kMinPanelColumns = 20.0f;
constexpr float kFallbackPanelColumns = 40.0f;
constexpr uint32_t kPanelStalledColour = 0xC0301000u; // a stalled load: the panel is reddish
constexpr uint32_t kPanelColour = 0xB0000000u;
constexpr float kRuleHeight = 2.0f;
constexpr uint32_t kRuleStalledColour = 0xFFFF8C1Au;
constexpr uint32_t kRuleColour = 0xFF5A5A5Au;
constexpr std::size_t kLeftTextChars = 48;
constexpr std::size_t kRightTextChars = 64;
constexpr uint32_t kTextColour = 0xFFFFFFFFu;
constexpr uint32_t kRightStalledColour = 0xFFFF9A2Eu;
constexpr uint32_t kRightColour = 0xFFA0A0A0u;
constexpr float kHalf = 0.5f;
constexpr int kTimerPeriodMilliseconds = 100;
constexpr int kTimerResolutionMilliseconds = 10;
constexpr int kColourWriteAll = 0xF;

// --- pure helpers ---

const char* get_file_name(const char* path)
{
    const std::string_view full(path);
    const std::size_t slash = full.find_last_of("\\/");
    return slash == std::string_view::npos ? path : full.substr(slash + 1).data();
}

std::string size_text(long long bytes)
{
    std::array<char, kSizeTextChars> text{};
    if (bytes < kKilobyteBytes)
        text::format_to(text, "%lld B", bytes);
    else if (bytes < kKilobyteBytesLL * kKilobyteBytes)
        text::format_to(text, "%.1f KB", static_cast<double>(bytes) / kKilobyteBytesD);
    else
        text::format_to(text, "%.1f MB", static_cast<double>(bytes) / (kKilobyteBytesD * kKilobyteBytesD));
    return text.data();
}

bool interesting_command(const char* name)
{
    if (!name)
        return false;
    for (const char* prefix : {"Load", "Create", "Parse", "Init", "Preload", "Spawn"})
        if (_strnicmp(name, prefix, std::strlen(prefix)) == 0)
            return true;
    // (Add* is mostly AddParams / AddFlag-style bookkeeping: only the loading ones)
    for (const char* exact : {"AddScene", "AddCollision", "AddSoundBanks", "AddMusicTrack", "AddStream"})
        if (_stricmp(name, exact) == 0)
            return true;
    return false;
}

bool interesting_log_line(const char* line)
{
    if (!line || !*line)
        return false;
    // (the data overlay's files are already there as opened; the rest is bookkeeping)
    for (const char* prefix : {"data overlay:", "replaced 0x", "shadow checks:", "self-test", "  "})
        if (std::strncmp(line, prefix, std::strlen(prefix)) == 0)
            return false;
    return true;
}

void Ring::append_one(const char* text, double time)
{
    if (count != 0) {
        Seg& newest = core::at(lines, (count - 1) % kRingLines);
        if (std::strncmp(newest.text.data(), text, kLineWidth - 1) == 0) {
            ++newest.repeat;
            newest.time = time;
            return;
        }
    }
    Seg& line = core::at(lines, count % kRingLines);
    strncpy_s(line.text.data(), line.text.size(), text, _TRUNCATE);
    line.time = time;
    line.repeat = 1;
    ++count;
}

std::vector<std::string> Ring::final_one(size_t n) const
{
    auto shown = std::min<size_t>({n, count, kRingLines});
    std::vector<std::string> out;
    out.reserve(shown);
    for (size_t i = shown; i > 0; --i) {
        const Seg& line = core::at(lines, (count - i) % kRingLines);
        std::array<char, kLineWidth + kLineSlack> text{};
        if (line.repeat > 1)
            text::format_to(text, "[%6.1f] %s (x%u)", line.time, line.text.data(), line.repeat);
        else
            text::format_to(text, "[%6.1f] %s", line.time, line.text.data());
        out.emplace_back(text.data());
    }
    return out;
}

namespace {

// --- the console's state ---

// NOT ORIGINAL grouping - loading_console.cpp's state (were globals). Made on first use, never destroyed
// (loading_console_state()).
struct LoadingConsoleState {
    SRWLOCK lock_ = SRWLOCK_INIT; // the ring and the activity time
    Ring ring_{};
    std::atomic<bool> active_{false};
    double start_ = 0.0; // when the load began (Now())
    double last_activity_ = 0.0; // the last line's time (Now())
};

LoadingConsoleState& loading_console_state()
{
    static core::NoDestroy<LoadingConsoleState> state;
    return state.get();
}

// The loading screen's lock (0x007052AC: a mutex, held while it draws) and its state.
HANDLE screen_mutex() { return render::screen_2d().screen_mutex(); }

bool load_screen_shown() { return render::engine_parts().loading_screen_up() != 0; } // the main loop draws nothing then

bool screen_picture() { return render::screen_2d().loading_screen_picture() != nullptr; } // the loading screen's picture is up

int progress_bar_pixels() { return render::screen_2d().gradient_width(); } // the loading bar's width in pixels

double get_now()
{
    static const double kFrequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / kFrequency;
}

void add_line(const char* text)
{
    const double now = get_now();
    AcquireSRWLockExclusive(&loading_console_state().lock_);
    loading_console_state().ring_.append_one(text, now - loading_console_state().start_);
    loading_console_state().last_activity_ = now;
    ReleaseSRWLockExclusive(&loading_console_state().lock_);
}

// --- the font: JetBrains Mono, rasterised once by ImGui's font atlas (no ImGui context: only its atlas) ---

struct Glyph {
    float x0, y0, x1, y1, u0, v0, u1, v1, advance;
    bool visible;
};
// NOT ORIGINAL grouping - the console's font: its glyphs' advances, the line height, the white texel, the texture and
// whether it was tried (were globals). Made on first use, never destroyed (console_font()).
struct ConsoleFont {
    std::array<Glyph, kGlyphCount> glyphs_{};
    float advance_ = 0.0f; // the font's one advance (monospace)
    float line_height_ = 0.0f; // pixels
    float white_u_ = 0.0f;
    float white_v_ = 0.0f;
    IDirect3DTexture9* texture_ = nullptr;
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
    IDirect3DDevice9* device = render::renderer().d3d_device();
    if (!device)
        return;
    const float pixels = std::clamp(std::round(static_cast<float>(render::renderer().back_buffer_height()) * 15.0f / 720.0f), 12.0f, 30.0f);
    ImFontAtlas atlas;
    atlas.Flags |= ImFontAtlasFlags_NoMouseCursors;
    ImFontConfig config;
    config.PixelSnapH = true;
    config.OversampleH = 2;
    static const auto kRanges = std::to_array<ImWchar>({0x0020, 0x007E, 0});
    const frontend::level_editor::ui_fonts::CompressedFont mono = frontend::level_editor::ui_fonts::get_mono();
    ImFont* font = atlas.AddFontFromMemoryCompressedTTF(mono.data, mono.size, pixels, &config, kRanges.data());
    unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
    if (!font || !atlas.Build()) {
        get_log("loading console: the font couldn't be made - no panel");
        return;
    }
    atlas.GetTexDataAsRGBA32(&rgba, &width, &height);
    util::ComPtr<IDirect3DTexture9> texture;
    if (FAILED(device->CreateTexture(static_cast<UINT>(width), static_cast<UINT>(height), 1, 0, D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED, texture.put(), nullptr))) {
        get_log("loading console: no texture for the font - no panel");
        return;
    }
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        return;
    const std::span<uint8_t> bits(static_cast<uint8_t*>(locked.pBits),
                                  static_cast<size_t>(locked.Pitch) * static_cast<size_t>(height));
    const std::span<const unsigned char> rgba_pixels(rgba, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const std::span<uint8_t> row = bits.subspan(static_cast<size_t>(y) * static_cast<size_t>(locked.Pitch));
        const std::span<const unsigned char> in = rgba_pixels.subspan(static_cast<size_t>(y) * static_cast<size_t>(width) * 4);
        for (int x = 0; x < width; ++x) {
            const uint32_t texel =
                static_cast<uint32_t>(core::at(in, (x * 4) + 3)) << 24 | 0x00FFFFFFu; // white, coverage as alpha
            std::memcpy(row.subspan(static_cast<size_t>(x) * 4, sizeof(texel)).data(), &texel, sizeof(texel));
        }
    }
    texture->UnlockRect(0);
    for (int c = kFirstPrintable; c < kPrintableEnd; ++c) {
        const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(c));
        if (!glyph)
            continue;
        core::at(console_font().glyphs_, c) = {.x0 = glyph->X0, .y0 = glyph->Y0, .x1 = glyph->X1, .y1 = glyph->Y1,       .u0 = glyph->U0,
                       .v0 = glyph->V0, .u1 = glyph->U1, .v1 = glyph->V1, .advance = glyph->AdvanceX, .visible = glyph->Visible != 0};
    }
    console_font().advance_ = core::at(console_font().glyphs_, 'M').advance;
    console_font().line_height_ = std::round(pixels * kLineHeightFactor);
    console_font().white_u_ = atlas.TexUvWhitePixel.x;
    console_font().white_v_ = atlas.TexUvWhitePixel.y;
    console_font().texture_ = texture.detach();
    get_log("loading console: on ([features] loading_console=0 turns it off) - JetBrains Mono %.0f px", pixels);
}

// --- the panel's geometry: laid out at most 10 times a second, drawn with every frame of the loading screen ---

struct Vertex {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
};
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
    const Vertex a{.x = x0,
                   .y = y0,
                   .z = 0.0f,
                   .rhw = 1.0f,
                   .color = color,
                   .u = console_font().white_u_,
                   .v = console_font().white_v_};
    const Vertex b{.x = x1,
                   .y = y0,
                   .z = 0.0f,
                   .rhw = 1.0f,
                   .color = color,
                   .u = console_font().white_u_,
                   .v = console_font().white_v_};
    const Vertex c{.x = x0,
                   .y = y1,
                   .z = 0.0f,
                   .rhw = 1.0f,
                   .color = color,
                   .u = console_font().white_u_,
                   .v = console_font().white_v_};
    const Vertex d{.x = x1,
                   .y = y1,
                   .z = 0.0f,
                   .rhw = 1.0f,
                   .color = color,
                   .u = console_font().white_u_,
                   .v = console_font().white_v_};
    console_vertices().vertices_.insert(console_vertices().vertices_.end(), {a, b, c, b, d, c});
}

// Text from (x, y) (the line's top), at most `columns` characters.
void draw_text(maths::Vec2 pen, const char* text, size_t columns, colour::Packed colour_word)
{
    float x = pen.x;
    const float y = pen.y;
    const uint32_t color = colour_word.value();
    size_t i = 0;
    for (core::CStringCursor at(text); *at && i < columns; ++at, ++i) {
        auto c = static_cast<unsigned char>(*at);
        const Glyph& glyph = core::at(console_font().glyphs_, c < kAsciiLimit ? c : '?');
        if (glyph.visible) {
            const float x0 = x + glyph.x0 - 0.5f;
            const float y0 = y + glyph.y0 - 0.5f;
            const float x1 = x + glyph.x1 - 0.5f;
            const float y1 = y + glyph.y1 - 0.5f;
            const Vertex a{.x = x0, .y = y0, .z = 0.0f, .rhw = 1.0f, .color = color, .u = glyph.u0, .v = glyph.v0};
            const Vertex b{.x = x1, .y = y0, .z = 0.0f, .rhw = 1.0f, .color = color, .u = glyph.u1, .v = glyph.v0};
            const Vertex cc{.x = x0, .y = y1, .z = 0.0f, .rhw = 1.0f, .color = color, .u = glyph.u0, .v = glyph.v1};
            const Vertex d{.x = x1, .y = y1, .z = 0.0f, .rhw = 1.0f, .color = color, .u = glyph.u1, .v = glyph.v1};
            console_vertices().vertices_.insert(console_vertices().vertices_.end(), {a, b, cc, b, d, cc});
        }
        x += console_font().advance_;
    }
}

void get_layout(double now)
{
    console_vertices().laid_out_ = now;
    console_vertices().vertices_.clear();
    std::vector<std::string> lines;
    double started = 0.0;
    double last = 0.0;
    AcquireSRWLockShared(&loading_console_state().lock_);
    lines = loading_console_state().ring_.final_one(kShownLines);
    started = loading_console_state().start_;
    last = loading_console_state().last_activity_;
    ReleaseSRWLockShared(&loading_console_state().lock_);

    auto width = static_cast<float>(render::renderer().back_buffer_width());
    auto height = static_cast<float>(render::renderer().back_buffer_height());
    const float margin = std::max(8.0f, std::round(height * 0.02f));
    const float pad = std::round(console_font().line_height_ * 0.4f);
    // Left of the loading bar (centred, progress_bar_pixels wide, its frame 5/640 of the screen wider each side), at most 45%.
    const float bar_left = ((width - static_cast<float>(progress_bar_pixels())) * 0.5f) - (width * 5.0f / 640.0f);
    float panel_width = std::min(width * kPanelWidthShare, bar_left - (kBothSides * margin));
    if (panel_width < kMinPanelColumns * console_font().advance_)
        panel_width = std::min(width - (kBothSides * margin),
                               kFallbackPanelColumns * console_font().advance_); // (no room beside the bar: over it)
    const size_t columns = static_cast<size_t>(std::max(1.0f, (panel_width - (2.0f * pad)) / console_font().advance_));
    const float panel_height =
        (2.0f * pad) + (console_font().line_height_ * static_cast<float>(kShownLines + 1)) + (pad * 0.5f);
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
    if (stalled)
        text::format_to(right, "NO ACTIVITY - last %.1f s ago", idle);
    else
        text::format_to(right, "last activity %.1f s ago", idle);
    float y = y0 + pad;
    draw_text({.x = x0 + pad, .y = y}, left.data(), columns, colour::Packed{kTextColour});
    const size_t right_length = std::strlen(right.data());
    const size_t left_length = std::strlen(left.data());
    if (left_length + 2 + right_length <= columns)
        draw_text({.x = x1 - pad - (static_cast<float>(right_length) * console_font().advance_), .y = y}, right.data(),
                  columns, colour::Packed{stalled ? kRightStalledColour : kRightColour});
    else
        draw_text({.x = x0 + pad + (static_cast<float>(left_length + 2) * console_font().advance_), .y = y},
                  right.data(), columns - std::min(columns, left_length + 2),
                  colour::Packed{stalled ? kRightStalledColour : kRightColour});
    y += console_font().line_height_ + (pad * kHalf);
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool newest = i + 1 == lines.size();
        const uint32_t color = newest ? (stalled ? 0xFFFFC080u : 0xFFFFFFFFu) : 0xFFB8B8B8u;
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
    // isn't a frozen frame, and no movie is shown.
    if (!loading_console_state().active_ || !console_timer().timer_draws_ || render::renderer().loading_bar_timer() != 0 || !load_screen_shown() || render::engine_parts().current_movie())
        return;
    const bool blank = console_timer().blank_;
    if (!blank && !screen_picture())
        return;
    WaitForSingleObject(screen_mutex(), INFINITE);
    IDirect3DDevice9* device = render::renderer().d3d_device();
    if (device && load_screen_shown() && render::renderer().loading_bar_timer() == 0 && loading_console_state().active_) {
        device->BeginScene();
        // as the loading screen draws it (0x004B78B0): its picture, or black
        if (blank)
            device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        else
            render::draw_loading_screen_picture(); // the picture, full screen
        draw_panel();
        device->EndScene();
        device->Present(nullptr, nullptr, nullptr, nullptr);
    }
    ReleaseMutex(screen_mutex());
}

void on_log_line(const char* line)
{
    if (loading_console_state().active_)
        note_log_line(line);
}

} // namespace

bool is_switched_on()
{
    static const bool kEnabled = config_int("features", "loading_console", 1) != 0;
    return kEnabled;
}

bool live_now() { return loading_console_state().active_; }

void screen_shown(bool freeze, bool blank)
{
    if (!is_switched_on())
        return;
    if (!console_font().font_tried_)
        build_font();
    if (!loading_console_state().active_) {
        const double now = get_now();
        AcquireSRWLockExclusive(&loading_console_state().lock_);
        loading_console_state().ring_.set_count(0);
        loading_console_state().start_ = now;
        loading_console_state().last_activity_ = now;
        ReleaseSRWLockExclusive(&loading_console_state().lock_);
        console_vertices().laid_out_ = -1.0;
        set_log_listener(&on_log_line);
        loading_console_state().active_ = true;
    }
    if (freeze)
        get_note("loading screen (frozen frame)");
    else if (blank)
        get_note("loading screen (blank)");
    else
        get_note("loading screen");
    console_timer().blank_ = blank;
    console_timer().timer_draws_ = !freeze;
    if (!freeze && !console_timer().timer_)
        console_timer().timer_ = timeSetEvent(kTimerPeriodMilliseconds, kTimerResolutionMilliseconds, &screen_tick, 0, TIME_PERIODIC);
}

void screen_hidden()
{
    if (!loading_console_state().active_)
        return;
    loading_console_state().active_ = false;
    console_timer().timer_draws_ = false;
    if (console_timer().timer_) {
        timeKillEvent(console_timer().timer_);
        console_timer().timer_ = 0;
    }
    const double now = get_now();
    get_log("loading console: the loading screen was up %.1f s", now - loading_console_state().start_);
}

void note_text(const char* text)
{
    if (!loading_console_state().active_)
        return;
    add_line(text);
}

void note_file(const char* what, const char* path, long long size)
{
    if (!loading_console_state().active_ || !path)
        return;
    if (size >= 0)
        get_note("%s %s  %s", what, get_file_name(path), size_text(size).c_str());
    else
        get_note("%s %s", what, get_file_name(path));
}

void note_script_call(uint32_t name, bool cfunction, script::Struct* params)
{
    if (!loading_console_state().active_)
        return;
    const char* text = script::look_up_hash_name(name);
    if (cfunction) {
        if (!interesting_command(text))
            return;
        // what it's given: a file or scene name, else a name checksum
        const char* what = nullptr;
        if (params) {
            if (!params->fetch_text(0u, &what, 0))
                for (const char* key : {"scene", "file", "name", "filename", "level", "type"})
                    if (params->fetch_text(key, &what, 0))
                        break;
            uint32_t checksum = 0;
            if (!what && (params->read_hash_value(0u, &checksum, 0) || params->read_hash_value("name", &checksum, 0) ||
                          params->read_hash_value("type", &checksum, 0)))
                what = script::look_up_hash_name(checksum);
        }
        if (what && *what)
            get_note("%s %s", text, get_file_name(what));
        else
            get_note("%s", text);
    } else if (text) {
        get_note("script %s", text);
    }
    // (a script whose name the game doesn't know - most of them, the retail scripts carry few names - isn't shown:
    // a checksum says nothing)
}

void note_log_line(const char* line)
{
    if (!loading_console_state().active_ || !interesting_log_line(line))
        return;
    std::array<char, kLineWidth> text{};
    strncpy_s(text.data(), text.size(), line, _TRUNCATE);
    // (a log line can come from any thread, also while a crash is being logged: never wait for the lock)
    const double now = get_now();
    if (!TryAcquireSRWLockExclusive(&loading_console_state().lock_))
        return;
    loading_console_state().ring_.append_one(text.data(), now - loading_console_state().start_);
    loading_console_state().last_activity_ = now;
    ReleaseSRWLockExclusive(&loading_console_state().lock_);
}

void draw_panel()
{
    if (!loading_console_state().active_ || !console_font().texture_)
        return;
    IDirect3DDevice9* device = render::renderer().d3d_device();
    if (!device)
        return;
    const double now = get_now();
    if (console_vertices().laid_out_ < 0.0 || now - console_vertices().laid_out_ >= kLayoutSeconds)
        get_layout(now);
    if (console_vertices().vertices_.empty())
        return;
    // The device's state saved and put back after (the renderer's cached state stays true).
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
        return;
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetFVF(render::kScreenVertexFormat);
    device->SetTexture(0, console_font().texture_);
    device->SetTexture(1, nullptr);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, kColourWriteAll);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(console_vertices().vertices_.size() / 3), console_vertices().vertices_.data(),
                            sizeof(Vertex));
    saved->Apply();
    saved->Release();
}

} // namespace gfx::loading_console
