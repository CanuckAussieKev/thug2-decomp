// The loading screen's progress bar: its colour gradient, with PartyMod's bigger buffer built in, and its frame (the
// timer callback that draws the loading screen while the game loads).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include <cstdint>

#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/loading_bar.h"

#include <windows.h>
#include <d3d9.h>

#include "game/gfx/loading_console.h"
#include "game/gfx/loading_screen.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/sys/startup_init.h"

namespace Nx {

int g_gradientWidth;
void* g_screenMutex; // the loading screen's lock (a HANDLE), held while it is drawn
float g_tickSeconds;
float g_elapsedSeconds;
float g_totalSeconds;

float g_floatTen{10.0f};
int g_barPosition[2]{258, 400};
int g_barWidth{140};
int g_barHeight{8};
int g_barBorder[2]{5, 5};
uint8_t g_barStartColor[4]{0x00, 0x4C, 0x81, 0x80};
uint8_t g_barEndColor[4]{0xAC, 0xD3, 0x73, 0x80};
uint8_t g_barBorderColor[4]{0x28, 0x28, 0x28, 0x80};
namespace {

// PartyMod's gradient table: 100000 bytes, enough for high resolutions (PartyMod's resbuffer, which its
// patchWindow pointed the table at).
uint8_t s_gradientBuffer[100000];

struct Rgb {
    int r, g, b;
};

float g_floatFive{5.0f};
float g_floatTwenty{20.0f};
float g_per640{0.0015625f};
float g_float420{420.0f};
float g_per480{0.0020833334f};
uint32_t g_borderArgb;
Rgb g_gradientEnd;
Rgb g_gradientStart;

// The bar's settings (SetLoadingBarStartColor etc.): colours as r, g, b(, a) bytes, and its width (in
// 640-wide screen units).
const uint8_t* StartColor() { return g_barStartColor; }
const uint8_t* EndColor() { return g_barEndColor; }
const uint8_t* BorderColor() { return g_barBorderColor; }
int& BarWidth() { return g_barWidth; }
int& ScreenWidth() { return g_backBufferWidth; }
float& ScreenScaleX() { return g_screenScaleX; }

Rgb& GradientStart() { return g_gradientStart; }
Rgb& GradientEnd() { return g_gradientEnd; }
int& GradientWidth() { return g_gradientWidth; }
uint32_t& BorderArgb() { return g_borderArgb; }

Rgb* GradientTable() { return reinterpret_cast<Rgb*>(s_gradientBuffer); }

// The bar's timing (0x004B7400): seconds per tick, seconds so far, seconds for the whole bar.
float& TickSeconds() { return g_tickSeconds; }
float& ElapsedSeconds() { return g_elapsedSeconds; }
float& TotalSeconds() { return g_totalSeconds; }
int& ScreenHeight() { return g_backBufferHeight; }
uint32_t& BarTimer() { return g_loadingBarTimer; }    // the timer's id (timeSetEvent)
bool& ScreenUp() { return g_loadingScreenUp; }           // shown (0x004B78B0): the main loop doesn't draw
HANDLE& ScreenMutex() { return reinterpret_cast<HANDLE&>(g_screenMutex); }    // held while the loading screen is drawn
IDirect3DDevice9* Device() { return g_d3dDevice; }

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
    Nx::DrawLoadingScreenPicture(); // the picture, full screen

    // (x87 at double precision: the intermediate results unrounded, as in the original - only the ones it stores
    // are floats)
    double xScale = static_cast<double>(ScreenWidth()) * g_per640; // 1 / 640
    float yScale = static_cast<float>(static_cast<double>(ScreenHeight()) * g_per480); // 1 / 480
    double elapsed = static_cast<double>(TickSeconds()) + ElapsedSeconds();
    ElapsedSeconds() = static_cast<float>(elapsed);
    double fraction = elapsed / TotalSeconds();
    if (fraction > 1.0)
        fraction = 1.0;
    int left = (ScreenWidth() - GradientWidth()) / 2;
    int frameLeft = Ftol(static_cast<double>(static_cast<float>(left)) - xScale * g_floatFive); // 5
    int filled = Ftol(GradientWidth() * fraction) + left;
    int frameRight = Ftol((GradientWidth() + frameLeft) + xScale * g_floatTen); // 10
    int top = Ftol(yScale * static_cast<double>(g_float420));                  // 420
    int bottom = Ftol(top + yScale * static_cast<double>(g_floatTwenty));         // 20
    float topF = static_cast<float>(top), bottomF = static_cast<float>(bottom);
    float frameLeftF = static_cast<float>(frameLeft), frameRightF = static_cast<float>(frameRight);

    SetTexture(0, nullptr);
    Device()->SetRenderState(D3DRS_ZENABLE, FALSE);
    SetVertexFormat(kBarVertexFormat);
    if (Nx::SelectPictureUp()) {
        // Not original - the level select pictures' plate: ink behind the bar (opaque, so it reads on any picture) with a
        // safety yellow rule on top, a little wider and taller than the bar's frame.
        float padX = static_cast<float>(xScale * 18.0), padY = yScale * 14.0f;
        float rule = yScale * 2.0f;
        DrawQuad(frameLeftF - padX, topF - padY, frameRightF + padX, bottomF + padY, 0xFF0A0A0A, 0xFF0A0A0A);
        DrawQuad(frameLeftF - padX, topF - padY, frameRightF + padX, topF - padY + rule, 0xFFF2E205, 0xFFF2E205);
    }
    DrawQuad(frameLeftF, topF, frameRightF, bottomF, BorderArgb(), BorderArgb());
    // the gradient, inset 5/480 of the screen's height top and bottom
    double inset = yScale * static_cast<double>(g_floatFive); // 5
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

namespace Nx {

// 0x00640D10 (a static initialiser): the loading screen's lock, a mutex nobody owns.
void CreateScreenMutex() { g_screenMutex = CreateMutexA(nullptr, FALSE, nullptr); }

} // namespace Nx
