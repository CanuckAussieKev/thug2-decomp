// NOT ORIGINAL - the loading console (see loading_console.h).

#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/loading_bar.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/loading_screen.h"

#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <array>

#include "editor/level_editor_ui_fonts.h"
#include "engine/gfx/movie.h"
#include "engine/gfx/draw_frame.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/util/com_ptr.h"
#include "engine/core/bits_as.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace gfx::loading_console {

// --- pure helpers ---

const char* get_file_name(const char* path)
{
    const char* name = path;
    for (const char* c = path; *c; ++c)
        if (*c == '\\' || *c == '/')
            name = c + 1;
    return name;
}

std::string size_text(long long bytes)
{
    std::array<char, 32> text{};
    if (bytes < 1024)
        std::snprintf(text.data(), sizeof(text), "%lld B", bytes);
    else if (bytes < 1024LL * 1024)
        std::snprintf(text.data(), sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(text.data(), sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
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
        std::array<char, kLineWidth + 32> text{};
        if (line.repeat > 1)
            std::snprintf(text.data(), sizeof(text), "[%6.1f] %s (x%u)", line.time, line.text.data(), line.repeat);
        else
            std::snprintf(text.data(), sizeof(text), "[%6.1f] %s", line.time, line.text.data());
        out.emplace_back(text.data());
    }
    return out;
}

namespace {

// --- the console's state ---

SRWLOCK g_lock = SRWLOCK_INIT; // the ring and the activity time
Ring g_ring;
std::atomic<bool> g_active{false};
double g_start = 0.0;         // when the load began (Now())
double g_last_activity = 0.0; // the last line's time (Now())

// The loading screen's lock (0x007052AC: a mutex, held while it draws) and its state.
HANDLE screen_mutex() { return memory::bits_as<HANDLE>(render::g_screen_mutex); }
IDirect3DDevice9* input_port() { return render::g_d3d_device; }
bool load_screen_shown() { return render::g_loading_screen_up != 0; } // the main loop draws nothing then
uint32_t bar_timer() { return render::g_loading_bar_timer; }          // the loading bar's timer (0x004B7400)
void* screen_picture() { return render::g_loading_screen_picture; }   // the loading screen's picture
void* movie_shown() { return render::g_current_movie; }               // a Bink movie playing (gfx/movie.cpp)
int get_screen_width() { return render::g_back_buffer_width; }
int get_screen_height() { return render::g_back_buffer_height; }
int progress_bar_pixels() { return render::g_gradient_width; } // the loading bar's width in pixels

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
    AcquireSRWLockExclusive(&g_lock);
    g_ring.append_one(text, now - g_start);
    g_last_activity = now;
    ReleaseSRWLockExclusive(&g_lock);
}

// --- the font: JetBrains Mono, rasterised once by ImGui's font atlas (no ImGui context: only its atlas) ---

struct Glyph {
    float x0, y0, x1, y1, u0, v0, u1, v1, advance;
    bool visible;
};
std::array<Glyph, 128> g_glyphs;
float g_advance = 0.0f;     // the font's one advance (monospace)
float g_line_height = 0.0f; // pixels
float g_white_u = 0.0f, g_white_v = 0.0f;
IDirect3DTexture9* g_texture = nullptr;
bool g_font_tried = false;

void build_font()
{
    g_font_tried = true;
    IDirect3DDevice9* device = input_port();
    if (!device)
        return;
    const float pixels = std::clamp(std::round(static_cast<float>(get_screen_height()) * 15.0f / 720.0f), 12.0f, 30.0f);
    ImFontAtlas atlas;
    atlas.Flags |= ImFontAtlasFlags_NoMouseCursors;
    ImFontConfig config;
    config.PixelSnapH = true;
    config.OversampleH = 2;
    static const auto kRanges = std::to_array<ImWchar>({0x0020, 0x007E, 0});
    const frontend::level_editor::ui_fonts::CompressedFont mono = frontend::level_editor::ui_fonts::get_mono();
    ImFont* font = atlas.AddFontFromMemoryCompressedTTF(mono.data, mono.size, pixels, &config, kRanges.data());
    unsigned char* rgba = nullptr;
    int width = 0, height = 0;
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
    for (int y = 0; y < height; ++y) {
        auto* row = raw::field_at<uint32_t>(static_cast<uint8_t*>(locked.pBits), y * locked.Pitch);
        const unsigned char* in = rgba + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
            row[x] = static_cast<uint32_t>(in[x * 4 + 3]) << 24 | 0x00FFFFFFu; // white, the glyph's coverage as alpha
    }
    texture->UnlockRect(0);
    for (int c = 0x20; c < 0x7F; ++c) {
        const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(c));
        if (!glyph)
            continue;
        core::at(g_glyphs, c) = {glyph->X0, glyph->Y0, glyph->X1, glyph->Y1,       glyph->U0,
                       glyph->V0, glyph->U1, glyph->V1, glyph->AdvanceX, glyph->Visible != 0};
    }
    g_advance = core::at(g_glyphs, 'M').advance;
    g_line_height = std::round(pixels * 1.2f);
    g_white_u = atlas.TexUvWhitePixel.x;
    g_white_v = atlas.TexUvWhitePixel.y;
    g_texture = texture.detach();
    get_log("loading console: on ([features] loading_console=0 turns it off) - JetBrains Mono %.0f px", pixels);
}

// --- the panel's geometry: laid out at most 10 times a second, drawn with every frame of the loading screen ---

struct Vertex {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
};
constexpr DWORD kVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr size_t kShownLines = 12;
constexpr double kLayoutSeconds = 0.1;
constexpr double kStallSeconds = 10.0;
std::vector<Vertex> g_vertices; // (used with the loading screen's lock held)
double g_laid_out = -1.0;

void rect2(float x0, float y0, float x1, float y1, uint32_t color)
{
    const Vertex a{x0, y0, 0.0f, 1.0f, color, g_white_u, g_white_v}, b{x1, y0, 0.0f, 1.0f, color, g_white_u, g_white_v},
        c{x0, y1, 0.0f, 1.0f, color, g_white_u, g_white_v}, d{x1, y1, 0.0f, 1.0f, color, g_white_u, g_white_v};
    g_vertices.insert(g_vertices.end(), {a, b, c, b, d, c});
}

// Text from (x, y) (the line's top), at most `columns` characters.
void draw_text(float x, float y, const char* text, size_t columns, uint32_t color)
{
    for (size_t i = 0; text[i] && i < columns; ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        const Glyph& glyph = core::at(g_glyphs, c < 0x80 ? c : '?');
        if (glyph.visible) {
            const float x0 = x + glyph.x0 - 0.5f;
            const float y0 = y + glyph.y0 - 0.5f;
            const float x1 = x + glyph.x1 - 0.5f;
            const float y1 = y + glyph.y1 - 0.5f;
            const Vertex a{x0, y0, 0.0f, 1.0f, color, glyph.u0, glyph.v0},
                b{x1, y0, 0.0f, 1.0f, color, glyph.u1, glyph.v0}, cc{x0, y1, 0.0f, 1.0f, color, glyph.u0, glyph.v1},
                d{x1, y1, 0.0f, 1.0f, color, glyph.u1, glyph.v1};
            g_vertices.insert(g_vertices.end(), {a, b, cc, b, d, cc});
        }
        x += g_advance;
    }
}

void get_layout(double now)
{
    g_laid_out = now;
    g_vertices.clear();
    std::vector<std::string> lines;
    double started = 0.0, last = 0.0;
    AcquireSRWLockShared(&g_lock);
    lines = g_ring.final_one(kShownLines);
    started = g_start;
    last = g_last_activity;
    ReleaseSRWLockShared(&g_lock);

    auto width = static_cast<float>(get_screen_width()), height = static_cast<float>(get_screen_height());
    const float margin = std::max(8.0f, std::round(height * 0.02f));
    const float pad = std::round(g_line_height * 0.4f);
    // Left of the loading bar (centred, progress_bar_pixels wide, its frame 5/640 of the screen wider each side), at most 45%.
    const float bar_left = (width - static_cast<float>(progress_bar_pixels())) * 0.5f - width * 5.0f / 640.0f;
    float panel_width = std::min(width * 0.45f, bar_left - 2.0f * margin);
    if (panel_width < 20.0f * g_advance)
        panel_width = std::min(width - 2.0f * margin, 40.0f * g_advance); // (no room beside the bar: over it)
    const size_t columns = static_cast<size_t>(std::max(1.0f, (panel_width - 2.0f * pad) / g_advance));
    const float panel_height = 2.0f * pad + g_line_height * static_cast<float>(kShownLines + 1) + pad * 0.5f;
    const float x0 = margin;
    const float y1 = height - margin;
    const float y0 = y1 - panel_height;
    const float x1 = x0 + panel_width;

    const double idle = now - last;
    const bool stalled = idle >= kStallSeconds;
    rect2(x0, y0, x1, y1, stalled ? 0xC0301000u : 0xB0000000u);
    rect2(x0, y0, x1, y0 + 2.0f, stalled ? 0xFFFF8C1Au : 0xFF5A5A5Au); // (a rule along the top)

    char left[48], right[64];
    std::snprintf(left, sizeof(left), "LOADING %.1f s", now - started);
    if (stalled)
        std::snprintf(right, sizeof(right), "NO ACTIVITY - last %.1f s ago", idle);
    else
        std::snprintf(right, sizeof(right), "last activity %.1f s ago", idle);
    float y = y0 + pad;
    draw_text(x0 + pad, y, left, columns, 0xFFFFFFFFu);
    const size_t right_length = std::strlen(right);
    const size_t left_length = std::strlen(left);
    if (left_length + 2 + right_length <= columns)
        draw_text(x1 - pad - static_cast<float>(right_length) * g_advance, y, right, columns,
                  stalled ? 0xFFFF9A2Eu : 0xFFA0A0A0u);
    else
        draw_text(x0 + pad + static_cast<float>(left_length + 2) * g_advance, y, right,
                  columns - std::min(columns, left_length + 2), stalled ? 0xFFFF9A2Eu : 0xFFA0A0A0u);
    y += g_line_height + pad * 0.5f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool newest = i + 1 == lines.size();
        const uint32_t color = newest ? (stalled ? 0xFFFFC080u : 0xFFFFFFFFu) : 0xFFB8B8B8u;
        draw_text(x0 + pad, y, core::at(lines, i).c_str(), columns, color);
        y += g_line_height;
    }
}

// --- the console's own timer, for a loading screen without a loading bar ---

UINT g_timer = 0;
std::atomic<bool> g_timer_draws{false}; // the screen shown wasn't frozen
std::atomic<bool> g_blank{false};

void CALLBACK screen_tick(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    // Only while the loading bar's own timer isn't running (it draws the panel then), a loading screen is up, it
    // isn't a frozen frame, and no movie is shown.
    if (!g_active || !g_timer_draws || bar_timer() != 0 || !load_screen_shown() || movie_shown())
        return;
    const bool blank = g_blank;
    if (!blank && !screen_picture())
        return;
    WaitForSingleObject(screen_mutex(), INFINITE);
    IDirect3DDevice9* device = input_port();
    if (device && load_screen_shown() && bar_timer() == 0 && g_active) {
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
    if (g_active)
        note_log_line(line);
}

} // namespace

bool is_switched_on()
{
    static const bool kEnabled = config_int("features", "loading_console", 1) != 0;
    return kEnabled;
}

bool live_now() { return g_active; }

void screen_shown(bool freeze, bool blank)
{
    if (!is_switched_on())
        return;
    if (!g_font_tried)
        build_font();
    if (!g_active) {
        const double now = get_now();
        AcquireSRWLockExclusive(&g_lock);
        g_ring.count = 0;
        g_start = now;
        g_last_activity = now;
        ReleaseSRWLockExclusive(&g_lock);
        g_laid_out = -1.0;
        set_log_listener(&on_log_line);
        g_active = true;
    }
    get_note(freeze ? "loading screen (frozen frame)" : blank ? "loading screen (blank)" : "loading screen");
    g_blank = blank;
    g_timer_draws = !freeze;
    if (!freeze && !g_timer)
        g_timer = timeSetEvent(100, 10, &screen_tick, 0, TIME_PERIODIC);
}

void screen_hidden()
{
    if (!g_active)
        return;
    g_active = false;
    g_timer_draws = false;
    if (g_timer) {
        timeKillEvent(g_timer);
        g_timer = 0;
    }
    const double now = get_now();
    get_log("loading console: the loading screen was up %.1f s", now - g_start);
}

void note_text(const char* text)
{
    if (!g_active)
        return;
    add_line(text);
}

void note_file(const char* what, const char* path, long long size)
{
    if (!g_active || !path)
        return;
    if (size >= 0)
        get_note("%s %s  %s", what, get_file_name(path), size_text(size).c_str());
    else
        get_note("%s %s", what, get_file_name(path));
}

void note_script_call(uint32_t name, bool cfunction, script::Struct* params)
{
    if (!g_active)
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
    if (!g_active || !interesting_log_line(line))
        return;
    std::array<char, kLineWidth> text{};
    strncpy_s(text.data(), text.size(), line, _TRUNCATE);
    // (a log line can come from any thread, also while a crash is being logged: never wait for the lock)
    const double now = get_now();
    if (!TryAcquireSRWLockExclusive(&g_lock))
        return;
    g_ring.append_one(text.data(), now - g_start);
    g_last_activity = now;
    ReleaseSRWLockExclusive(&g_lock);
}

void draw_panel()
{
    if (!g_active || !g_texture)
        return;
    IDirect3DDevice9* device = input_port();
    if (!device)
        return;
    const double now = get_now();
    if (g_laid_out < 0.0 || now - g_laid_out >= kLayoutSeconds)
        get_layout(now);
    if (g_vertices.empty())
        return;
    // The device's state saved and put back after (the renderer's cached state stays true).
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
        return;
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetFVF(kVertexFormat);
    device->SetTexture(0, g_texture);
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
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
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
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(g_vertices.size() / 3), g_vertices.data(),
                            sizeof(Vertex));
    saved->Apply();
    saved->Release();
}

} // namespace gfx::loading_console
