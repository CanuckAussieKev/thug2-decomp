// The loading screen's progress bar: its colour gradient, with PartyMod's bigger buffer built in, and its frame (the
// timer callback that draws the loading screen while the game loads).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/loading_bar.h"

#include <windows.h>
#include <d3d9.h>

#include "game/gfx/loading_console.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// PartyMod's gradient table: 100000 bytes, enough for high resolutions (PartyMod's resbuffer, which its
// patchWindow pointed the table at).
uint8_t s_gradientBuffer[100000];

struct Rgb {
    int r, g, b;
};

// The bar's settings (SetLoadingBarStartColor etc.): colours as r, g, b(, a) bytes, and its width (in
// 640-wide screen units).
const uint8_t* StartColor() { return &GameVariable<uint8_t>(0x0067DDCC); }
const uint8_t* EndColor() { return &GameVariable<uint8_t>(0x0067DDD0); }
const uint8_t* BorderColor() { return &GameVariable<uint8_t>(0x0067DDD4); }
int& BarWidth() { return GameVariable<int>(0x0067DDBC); }
int& ScreenWidth() { return GameVariable<int>(0x00786D6C); }
float& ScreenScaleX() { return GameVariable<float>(0x00786D80); }

Rgb& GradientStart() { return GameVariable<Rgb>(0x00705294); }
Rgb& GradientEnd() { return GameVariable<Rgb>(0x00705288); }
int& GradientWidth() { return GameVariable<int>(0x00701684); }
uint32_t& BorderArgb() { return GameVariable<uint32_t>(0x00701680); }

Rgb* GradientTable() { return reinterpret_cast<Rgb*>(s_gradientBuffer); }

// The bar's timing (0x004B7400): seconds per tick, seconds so far, seconds for the whole bar.
float& TickSeconds() { return GameVariable<float>(0x007052A0); }
float& ElapsedSeconds() { return GameVariable<float>(0x007052A4); }
float& TotalSeconds() { return GameVariable<float>(0x007052A8); }
int& ScreenHeight() { return GameVariable<int>(0x00786D70); }
uint32_t& BarTimer() { return GameVariable<uint32_t>(0x00786D08); }    // the timer's id (timeSetEvent)
bool& ScreenUp() { return GameVariable<bool>(0x00786D04); }           // shown (0x004B78B0): the main loop doesn't draw
HANDLE& ScreenMutex() { return GameVariable<HANDLE>(0x007052AC); }    // held while the loading screen is drawn
IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }

struct BarVertex {
    float x, y, z, rhw;
    uint32_t color;
};
static_assert(sizeof(BarVertex) == 0x14, "the original's vertex");
constexpr uint32_t kBarVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE; // 0x44

void DrawQuad(float x0, float y0, float x1, float y1, uint32_t left, uint32_t right)
{
    // (a strip: top left, bottom left, top right, bottom right)
    BarVertex quad[4] = {{x0, y0, 1.0f, 1.0f, left}, {x0, y1, 1.0f, 1.0f, left}, {x1, y0, 1.0f, 1.0f, right},
                         {x1, y1, 1.0f, 1.0f, right}};
    Device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(BarVertex));
}
// The C runtime's _ftol2 (0x006236FC): truncated to a 64-bit integer, its low half (NaN and infinities: 0).
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
uint32_t Opaque(const Rgb& c) { return 0xFF000000u | (c.r & 0xFF) << 16 | (c.g & 0xFF) << 8 | (c.b & 0xFF); }

} // namespace

uint8_t* LoadingBarGradientBuffer() { return s_gradientBuffer; }

// 0x004B6B80
void UpdateLoadingBarColors()
{
    GradientStart() = {StartColor()[0], StartColor()[1], StartColor()[2]};
    GradientEnd() = {EndColor()[0], EndColor()[1], EndColor()[2]};
    GradientWidth() = static_cast<int>(static_cast<double>(ScreenWidth()) * BarWidth() * (1.0f / 640.0f));
    BorderArgb() = 0x80000000u | BorderColor()[0] << 16 | BorderColor()[1] << 8 | BorderColor()[2];

    // One colour per pixel column, from the start colour towards the end colour. The original's table is at
    // 0x00701688 and overflows at high resolutions; PartyMod pointed it at a bigger buffer instead, though 4
    // bytes too early (it replaced the instruction's base, which is the table's address + 4). Here the table is
    // at the start of the buffer. Nothing reads the table.
    int steps = static_cast<int>(static_cast<double>(GradientWidth()) * ScreenScaleX());
    const Rgb start = GradientStart();
    const Rgb delta = {GradientEnd().r - start.r, GradientEnd().g - start.g, GradientEnd().b - start.b};
    Rgb* table = GradientTable();
    for (int i = 0; i < steps; ++i) {
        table[i].r = delta.r * i / steps + GradientStart().r;
        table[i].g = delta.g * i / steps + GradientStart().g;
        table[i].b = delta.b * i / steps + GradientStart().b;
    }
}

// 0x004B6FF0: the loading screen's frame - the timer callback 0x004B7400 starts (every 29 ms, on winmm's timer
// thread, while the game loads on its own): the picture, then the bar - its frame (the border colour, half
// transparent), the gradient over its whole width, and the part not reached yet covered with the border colour
// again. The bar fills with time (the seconds DisplayLoadingScreen was given), not with what has loaded; it stops
// full. Not original: the loading console's panel (game/gfx/loading_console.h) is drawn before the scene ends.
void __stdcall DrawLoadingScreenFrame(unsigned int, unsigned int, unsigned long, unsigned long, unsigned long)
{
    if (!BarTimer() || !ScreenUp())
        return;
    WaitForSingleObject(ScreenMutex(), INFINITE);
    Device()->BeginScene();
    GameFunction<void(__cdecl*)()>(0x004B6CD0)(); // the picture, full screen

    // (x87 at double precision: the intermediate results unrounded, as in the original - only the ones it stores
    // are floats)
    double xScale = static_cast<double>(ScreenWidth()) * GameVariable<float>(0x0064A474); // 1 / 640
    float yScale = static_cast<float>(static_cast<double>(ScreenHeight()) * GameVariable<float>(0x0064A47C)); // 1 / 480
    double elapsed = static_cast<double>(TickSeconds()) + ElapsedSeconds();
    ElapsedSeconds() = static_cast<float>(elapsed);
    double fraction = elapsed / TotalSeconds();
    if (fraction > 1.0)
        fraction = 1.0;
    int left = (ScreenWidth() - GradientWidth()) / 2;
    int frameLeft = Ftol(static_cast<double>(static_cast<float>(left)) - xScale * GameVariable<float>(0x006469AC)); // 5
    int filled = Ftol(GradientWidth() * fraction) + left;
    int frameRight = Ftol((GradientWidth() + frameLeft) + xScale * GameVariable<float>(0x00646710)); // 10
    int top = Ftol(yScale * static_cast<double>(GameVariable<float>(0x0064A478)));                  // 420
    int bottom = Ftol(top + yScale * static_cast<double>(GameVariable<float>(0x006486AC)));         // 20
    float topF = static_cast<float>(top), bottomF = static_cast<float>(bottom);
    float frameLeftF = static_cast<float>(frameLeft), frameRightF = static_cast<float>(frameRight);

    SetTexture(0, nullptr);
    Device()->SetRenderState(D3DRS_ZENABLE, FALSE);
    SetVertexFormat(kBarVertexFormat);
    DrawQuad(frameLeftF, topF, frameRightF, bottomF, BorderArgb(), BorderArgb());
    // the gradient, inset 5/480 of the screen's height top and bottom
    double inset = yScale * static_cast<double>(GameVariable<float>(0x006469AC)); // 5
    float gradientTop = static_cast<float>(topF + inset);
    float gradientBottom = static_cast<float>(bottomF - static_cast<double>(static_cast<float>(inset)));
    DrawQuad(static_cast<float>(left), gradientTop, static_cast<float>(GradientWidth() + left), gradientBottom,
             Opaque(GradientStart()), Opaque(GradientEnd()));
    // what isn't filled yet - from the fill's end to the frame's right edge, the frame's height: its colour again
    DrawQuad(static_cast<float>(filled), topF, frameRightF, bottomF, BorderArgb(), BorderArgb());
    Device()->SetRenderState(D3DRS_ZENABLE, TRUE);
    // Not original - the loading console (nothing unless a loading screen is up with [features] loading_console on).
    Gfx::LoadingConsole::DrawPanel();
    Device()->EndScene();
    Device()->Present(nullptr, nullptr, nullptr, nullptr);
    ReleaseMutex(ScreenMutex());
}

} // namespace Nx

REPLACE(0x004B6B80, Nx::UpdateLoadingBarColors);
REPLACE(0x004B6FF0, Nx::DrawLoadingScreenFrame);
