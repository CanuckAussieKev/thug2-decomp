// NOT ORIGINAL - the loading console (see loading_console.h).

#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/loading_bar.h"
#include "game/gfx/loading_console.h"
#include "game/gfx/loading_screen.h"

#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "game/front/level_editor_ui_fonts.h"
#include "game/gfx/movie.h"
#include "game/gfx/render.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"
#include "imgui.h"

#pragma comment(lib, "winmm.lib") // timeSetEvent / timeKillEvent

namespace Gfx::LoadingConsole {

// --- pure helpers ---

const char* FileName(const char* path)
{
    const char* name = path;
    for (const char* c = path; *c; ++c)
        if (*c == '\\' || *c == '/')
            name = c + 1;
    return name;
}

std::string SizeText(long long bytes)
{
    char text[32];
    if (bytes < 1024)
        std::snprintf(text, sizeof(text), "%lld B", bytes);
    else if (bytes < 1024 * 1024)
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

bool InterestingCommand(const char* name)
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

bool InterestingLogLine(const char* line)
{
    if (!line || !*line)
        return false;
    // (the data overlay's files are already there as opened; the rest is the self-tests' and hooks' bookkeeping)
    for (const char* prefix : {"data overlay:", "replaced 0x", "shadow checks:", "self-test", "  "})
        if (std::strncmp(line, prefix, std::strlen(prefix)) == 0)
            return false;
    return true;
}

void Ring::Add(const char* text, double time)
{
    if (count != 0) {
        Line& newest = lines[(count - 1) % kRingLines];
        if (std::strncmp(newest.text, text, kLineWidth - 1) == 0) {
            ++newest.repeat;
            newest.time = time;
            return;
        }
    }
    Line& line = lines[count % kRingLines];
    strncpy_s(line.text, text, _TRUNCATE);
    line.time = time;
    line.repeat = 1;
    ++count;
}

std::vector<std::string> Ring::Last(size_t n) const
{
    size_t shown = std::min<size_t>({n, count, kRingLines});
    std::vector<std::string> out;
    out.reserve(shown);
    for (size_t i = shown; i > 0; --i) {
        const Line& line = lines[(count - i) % kRingLines];
        char text[kLineWidth + 32];
        if (line.repeat > 1)
            std::snprintf(text, sizeof(text), "[%6.1f] %s (x%u)", line.time, line.text, line.repeat);
        else
            std::snprintf(text, sizeof(text), "[%6.1f] %s", line.time, line.text);
        out.emplace_back(text);
    }
    return out;
}

namespace {

// --- the console's state ---

SRWLOCK g_lock = SRWLOCK_INIT; // the ring and the activity time
Ring g_ring;
std::atomic<bool> g_active{false};
double g_start = 0.0;        // when the load began (Now())
double g_lastActivity = 0.0; // the last line's time (Now())

// The loading screen's lock (0x007052AC: a mutex, held while it draws) and its state.
HANDLE ScreenMutex() { return GameVariable<HANDLE>(0x007052AC); }
IDirect3DDevice9* Device() { return Nx::g_d3dDevice; }
bool ScreenUp() { return Nx::g_loadingScreenUp != 0; } // the main loop draws nothing then
uint32_t BarTimer() { return Nx::g_loadingBarTimer; }   // the loading bar's timer (0x004B7400)
void* ScreenPicture() { return Nx::g_loadingScreenPicture; }    // the loading screen's picture
void* MovieShown() { return Nx::g_currentMovie; }       // a Bink movie playing (gfx/movie.cpp)
int ScreenWidth() { return Nx::g_backBufferWidth; }
int ScreenHeight() { return Nx::g_backBufferHeight; }
int BarWidth() { return Nx::g_gradientWidth; } // the loading bar's width in pixels

double Now()
{
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency;
}

void AddLine(const char* text)
{
    double now = Now();
    AcquireSRWLockExclusive(&g_lock);
    g_ring.Add(text, now - g_start);
    g_lastActivity = now;
    ReleaseSRWLockExclusive(&g_lock);
}

// --- the font: JetBrains Mono, rasterised once by ImGui's font atlas (no ImGui context: only its atlas) ---

struct Glyph {
    float x0, y0, x1, y1, u0, v0, u1, v1, advance;
    bool visible;
};
Glyph g_glyphs[128];
float g_advance = 0.0f;    // the font's one advance (monospace)
float g_lineHeight = 0.0f; // pixels
float g_whiteU = 0.0f, g_whiteV = 0.0f;
IDirect3DTexture9* g_texture = nullptr;
bool g_fontTried = false;

void BuildFont()
{
    g_fontTried = true;
    IDirect3DDevice9* device = Device();
    if (!device)
        return;
    float pixels = std::clamp(std::round(static_cast<float>(ScreenHeight()) * 15.0f / 720.0f), 12.0f, 30.0f);
    ImFontAtlas atlas;
    atlas.Flags |= ImFontAtlasFlags_NoMouseCursors;
    ImFontConfig config;
    config.PixelSnapH = true;
    config.OversampleH = 2;
    static const ImWchar kRanges[] = {0x0020, 0x007E, 0};
    Front::LevelEditor::UiFonts::CompressedFont mono = Front::LevelEditor::UiFonts::Mono();
    ImFont* font = atlas.AddFontFromMemoryCompressedTTF(mono.data, mono.size, pixels, &config, kRanges);
    unsigned char* rgba = nullptr;
    int width = 0, height = 0;
    if (!font || !atlas.Build()) {
        Log("loading console: the font couldn't be made - no panel");
        return;
    }
    atlas.GetTexDataAsRGBA32(&rgba, &width, &height);
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(static_cast<UINT>(width), static_cast<UINT>(height), 1, 0, D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED, &texture, nullptr))) {
        Log("loading console: no texture for the font - no panel");
        return;
    }
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        texture->Release();
        return;
    }
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch);
        const unsigned char* in = rgba + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
            row[x] = static_cast<uint32_t>(in[x * 4 + 3]) << 24 | 0x00FFFFFFu; // white, the glyph's coverage as alpha
    }
    texture->UnlockRect(0);
    for (int c = 0x20; c < 0x7F; ++c) {
        const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(c));
        if (!glyph)
            continue;
        g_glyphs[c] = {glyph->X0, glyph->Y0, glyph->X1, glyph->Y1, glyph->U0, glyph->V0, glyph->U1, glyph->V1,
                       glyph->AdvanceX, glyph->Visible != 0};
    }
    g_advance = g_glyphs['M'].advance;
    g_lineHeight = std::round(pixels * 1.2f);
    g_whiteU = atlas.TexUvWhitePixel.x;
    g_whiteV = atlas.TexUvWhitePixel.y;
    g_texture = texture;
    Log("loading console: on ([features] loading_console=0 turns it off) - JetBrains Mono %.0f px", pixels);
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
double g_laidOut = -1.0;

void Rect(float x0, float y0, float x1, float y1, uint32_t color)
{
    const Vertex a{x0, y0, 0.0f, 1.0f, color, g_whiteU, g_whiteV}, b{x1, y0, 0.0f, 1.0f, color, g_whiteU, g_whiteV},
        c{x0, y1, 0.0f, 1.0f, color, g_whiteU, g_whiteV}, d{x1, y1, 0.0f, 1.0f, color, g_whiteU, g_whiteV};
    g_vertices.insert(g_vertices.end(), {a, b, c, b, d, c});
}

// Text from (x, y) (the line's top), at most `columns` characters.
void Text(float x, float y, const char* text, size_t columns, uint32_t color)
{
    for (size_t i = 0; text[i] && i < columns; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        const Glyph& glyph = g_glyphs[c < 0x80 ? c : '?'];
        if (glyph.visible) {
            float x0 = x + glyph.x0 - 0.5f, y0 = y + glyph.y0 - 0.5f, x1 = x + glyph.x1 - 0.5f, y1 = y + glyph.y1 - 0.5f;
            const Vertex a{x0, y0, 0.0f, 1.0f, color, glyph.u0, glyph.v0}, b{x1, y0, 0.0f, 1.0f, color, glyph.u1, glyph.v0},
                cc{x0, y1, 0.0f, 1.0f, color, glyph.u0, glyph.v1}, d{x1, y1, 0.0f, 1.0f, color, glyph.u1, glyph.v1};
            g_vertices.insert(g_vertices.end(), {a, b, cc, b, d, cc});
        }
        x += g_advance;
    }
}

void Layout(double now)
{
    g_laidOut = now;
    g_vertices.clear();
    std::vector<std::string> lines;
    double started, last;
    AcquireSRWLockShared(&g_lock);
    lines = g_ring.Last(kShownLines);
    started = g_start;
    last = g_lastActivity;
    ReleaseSRWLockShared(&g_lock);

    float width = static_cast<float>(ScreenWidth()), height = static_cast<float>(ScreenHeight());
    float margin = std::max(8.0f, std::round(height * 0.02f));
    float pad = std::round(g_lineHeight * 0.4f);
    // Left of the loading bar (centred, BarWidth wide, its frame 5/640 of the screen wider each side), at most 45%.
    float barLeft = (width - static_cast<float>(BarWidth())) * 0.5f - width * 5.0f / 640.0f;
    float panelWidth = std::min(width * 0.45f, barLeft - 2.0f * margin);
    if (panelWidth < 20.0f * g_advance)
        panelWidth = std::min(width - 2.0f * margin, 40.0f * g_advance); // (no room beside the bar: over it)
    size_t columns = static_cast<size_t>(std::max(1.0f, (panelWidth - 2.0f * pad) / g_advance));
    float panelHeight = 2.0f * pad + g_lineHeight * static_cast<float>(kShownLines + 1) + pad * 0.5f;
    float x0 = margin, y1 = height - margin, y0 = y1 - panelHeight, x1 = x0 + panelWidth;

    double idle = now - last;
    bool stalled = idle >= kStallSeconds;
    Rect(x0, y0, x1, y1, stalled ? 0xC0301000u : 0xB0000000u);
    Rect(x0, y0, x1, y0 + 2.0f, stalled ? 0xFFFF8C1Au : 0xFF5A5A5Au); // (a rule along the top)

    char left[48], right[64];
    std::snprintf(left, sizeof(left), "LOADING %.1f s", now - started);
    if (stalled)
        std::snprintf(right, sizeof(right), "NO ACTIVITY - last %.1f s ago", idle);
    else
        std::snprintf(right, sizeof(right), "last activity %.1f s ago", idle);
    float y = y0 + pad;
    Text(x0 + pad, y, left, columns, 0xFFFFFFFFu);
    size_t rightLength = std::strlen(right);
    size_t leftLength = std::strlen(left);
    if (leftLength + 2 + rightLength <= columns)
        Text(x1 - pad - static_cast<float>(rightLength) * g_advance, y, right, columns, stalled ? 0xFFFF9A2Eu : 0xFFA0A0A0u);
    else
        Text(x0 + pad + static_cast<float>(leftLength + 2) * g_advance, y, right, columns - std::min(columns, leftLength + 2),
             stalled ? 0xFFFF9A2Eu : 0xFFA0A0A0u);
    y += g_lineHeight + pad * 0.5f;
    for (size_t i = 0; i < lines.size(); ++i) {
        bool newest = i + 1 == lines.size();
        uint32_t color = newest ? (stalled ? 0xFFFFC080u : 0xFFFFFFFFu) : 0xFFB8B8B8u;
        Text(x0 + pad, y, lines[i].c_str(), columns, color);
        y += g_lineHeight;
    }
}

// --- the console's own timer, for a loading screen without a loading bar ---

UINT g_timer = 0;
std::atomic<bool> g_timerDraws{false}; // the screen shown wasn't frozen
std::atomic<bool> g_blank{false};

void CALLBACK ScreenTick(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    // Only while the loading bar's own timer isn't running (it draws the panel then), a loading screen is up, it
    // isn't a frozen frame, and no movie is shown.
    if (!g_active || !g_timerDraws || BarTimer() != 0 || !ScreenUp() || MovieShown())
        return;
    bool blank = g_blank;
    if (!blank && !ScreenPicture())
        return;
    WaitForSingleObject(ScreenMutex(), INFINITE);
    IDirect3DDevice9* device = Device();
    if (device && ScreenUp() && BarTimer() == 0 && g_active) {
        device->BeginScene();
        // as the loading screen draws it (0x004B78B0): its picture, or black
        if (blank)
            device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        else
            Nx::DrawLoadingScreenPicture(); // the picture, full screen
        DrawPanel();
        device->EndScene();
        device->Present(nullptr, nullptr, nullptr, nullptr);
    }
    ReleaseMutex(ScreenMutex());
}

void OnLogLine(const char* line)
{
    if (g_active)
        NoteLogLine(line);
}

} // namespace

bool Enabled()
{
    static bool on = ConfigInt("features", "loading_console", 1) != 0;
    return on;
}

bool Active() { return g_active; }

bool& SuspendedForSelfTests()
{
    static bool suspended = false;
    return suspended;
}

void ScreenShown(bool freeze, bool blank)
{
    if (SuspendedForSelfTests() || !Enabled())
        return;
    if (!g_fontTried)
        BuildFont();
    if (!g_active) {
        double now = Now();
        AcquireSRWLockExclusive(&g_lock);
        g_ring.count = 0;
        g_start = now;
        g_lastActivity = now;
        ReleaseSRWLockExclusive(&g_lock);
        g_laidOut = -1.0;
        SetLogListener(&OnLogLine);
        g_active = true;
    }
    Note(freeze ? "loading screen (frozen frame)" : blank ? "loading screen (blank)" : "loading screen");
    g_blank = blank;
    g_timerDraws = !freeze;
    if (!freeze && !g_timer)
        g_timer = timeSetEvent(100, 10, &ScreenTick, 0, TIME_PERIODIC);
}

void ScreenHidden()
{
    if (SuspendedForSelfTests() || !g_active)
        return;
    g_active = false;
    g_timerDraws = false;
    if (g_timer) {
        timeKillEvent(g_timer);
        g_timer = 0;
    }
    double now = Now();
    Log("loading console: the loading screen was up %.1f s", now - g_start);
}

void Note(const char* format, ...)
{
    if (!g_active)
        return;
    char text[kLineWidth];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    AddLine(text);
}

void NoteFile(const char* what, const char* path, long long size)
{
    if (!g_active || !path)
        return;
    if (size >= 0)
        Note("%s %s  %s", what, FileName(path), SizeText(size).c_str());
    else
        Note("%s %s", what, FileName(path));
}

void NoteScriptCall(uint32_t name, bool cfunction, Script::CStruct* params)
{
    if (!g_active)
        return;
    const char* text = Script::FindChecksumName(name);
    if (cfunction) {
        if (!InterestingCommand(text))
            return;
        // what it's given: a file or scene name, else a name checksum
        const char* what = nullptr;
        if (params) {
            if (!params->GetString(0u, &what, 0))
                for (const char* key : {"scene", "file", "name", "filename", "level", "type"})
                    if (params->GetString(key, &what, 0))
                        break;
            uint32_t checksum = 0;
            if (!what && (params->GetChecksum(0u, &checksum, 0) || params->GetChecksum("name", &checksum, 0) ||
                          params->GetChecksum("type", &checksum, 0)))
                what = Script::FindChecksumName(checksum);
        }
        if (what && *what)
            Note("%s %s", text, FileName(what));
        else
            Note("%s", text);
    } else if (text) {
        Note("script %s", text);
    }
    // (a script whose name the game doesn't know - most of them, the retail scripts carry few names - isn't shown:
    // a checksum says nothing)
}

void NoteLogLine(const char* line)
{
    if (!g_active || !InterestingLogLine(line))
        return;
    char text[kLineWidth];
    strncpy_s(text, line, _TRUNCATE);
    // (a log line can come from any thread, also while a crash is being logged: never wait for the lock)
    double now = Now();
    if (!TryAcquireSRWLockExclusive(&g_lock))
        return;
    g_ring.Add(text, now - g_start);
    g_lastActivity = now;
    ReleaseSRWLockExclusive(&g_lock);
}

void DrawPanel()
{
    if (!g_active || !g_texture)
        return;
    IDirect3DDevice9* device = Device();
    if (!device)
        return;
    double now = Now();
    if (g_laidOut < 0.0 || now - g_laidOut >= kLayoutSeconds)
        Layout(now);
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

} // namespace Gfx::LoadingConsole
