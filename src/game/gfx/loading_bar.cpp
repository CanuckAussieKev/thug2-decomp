// The loading screen's progress bar: its colour gradient, with PartyMod's bigger buffer built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/loading_bar.h"

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

} // namespace Nx

REPLACE(0x004B6B80, Nx::UpdateLoadingBarColors);
