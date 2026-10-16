// The screen's shape and full-screen effects (Nx engine, PC) - with the PC port's widescreen support
// (pillar- / letterboxing to the aspect wanted) and its DisableBlur setting.
// Contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md

#include "game/gfx/screen.h"

#include "game/gfx/settings.h"
#include "game/script/params.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// Whether the device can do the blur (its render-target textures were made).
auto& BlurAvailable() { return GameVariable<uint32_t>(0x00786D40); }
auto& ScreenBlur() { return GameVariable<uint32_t>(0x00787328); }
// The last amount the script asked for (kept even when the blur is off).
auto& ScriptScreenBlur() { return GameVariable<int>(0x006FA72C); }

// The screen's aspect (width / height), and the original's angle factor constant (1.0).
float& ScreenAspect() { return GameVariable<float>(0x00701340); }
float AngleFactorConstant() { return GameVariable<float>(0x0067E544); }

// The 640x480 -> back buffer conversion (InitDirect3D) and the letterbox flag.
uint8_t& LetterboxActive() { return GameVariable<uint8_t>(0x00786CBE); }
float& ScreenScaleX() { return GameVariable<float>(0x00786D80); }
float& ScreenScaleY() { return GameVariable<float>(0x00786D84); }
uint32_t& ScreenOffsetX() { return GameVariable<uint32_t>(0x00786D88); }
uint32_t& ScreenOffsetY() { return GameVariable<uint32_t>(0x00786D8C); }
uint32_t BackBufferWidth() { return GameVariable<uint32_t>(0x00786D6C); }
uint32_t BackBufferHeight() { return GameVariable<uint32_t>(0x00786D70); }

} // namespace

uint8_t CurrentLevel()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    if (!skate)
        return 0;
    return (*reinterpret_cast<uint8_t**>(skate + 0x20))[0x630];
}

float DesiredAspectRatio()
{
    // (the window's size as unsigned numbers)
    auto width = static_cast<uint32_t>(Sys::WindowWidth());
    auto height = static_cast<uint32_t>(Sys::WindowHeight());
    if (CurrentLevel() == 0)
        return 4.0f / 3.0f;
    return static_cast<float>(width) / static_cast<float>(height);
}

// 0x004A0780. The original stores `aspect` as the screen's aspect (0x00701340). PC port: the aspect
// wanted instead (4:3 in the front end, the window's shape in a level); `aspect` is ignored.
void SetScreenAspectRatio(float /*aspect*/)
{
    ScreenAspect() = DesiredAspectRatio();
}

// 0x004A07C0. The original returns the constant at 0x0067E544 (1.0). PC port: the view is widened for
// screens wider than 4:3 (the aspect / (4/3)) - unless that constant isn't 1 - and the screen's aspect
// (0x00701340) is updated too.
float GetScreenAngleFactor()
{
    float aspect = DesiredAspectRatio();
    // (narrower than 4:3: the horizontal view isn't narrowed - the vertical one grows instead)
    float factor = aspect > 4.0f / 3.0f ? aspect / (4.0f / 3.0f) : 1.0f;
    ScreenAspect() = aspect;
    if (AngleFactorConstant() != 1.0f)
        return AngleFactorConstant();
    return factor;
}

// 0x004B2330. The original: if (BlurAvailable()) ScreenBlur() = amount;
// PC port: the DisableBlur setting (on by default) turns the blur off for good instead.
void SetScreenBlur(uint32_t amount)
{
    if (BlurDisabled()) {
        ScreenBlur() = 0;
        return;
    }
    if (BlurAvailable())
        ScreenBlur() = amount;
}

// 0x004B2440. The original squashes the 3D view vertically to 3/4 of the screen, centred, and back:
//     int quarter = backbufferHeight / 4;
//     if (letterbox) { if (!active) { offsetY += quarter / 2; scaleY = 0.75f; active = true; } }
//     else if (active) { offsetY += -quarter / 2; scaleY = 1.0f; active = false; }
// (active 0x00786CBE, scaleY 0x00786D84, offsetY 0x00786D8C, backbufferHeight 0x00786D70). PC port:
// the 640x480 screen layout is fitted to the aspect wanted (pillarbox or letterbox) and the flag set, which then
// selects the fitted display region (SetDisplayRegion).
void SetLetterbox(bool letterbox)
{
    float backBufferAspect = static_cast<float>(BackBufferWidth()) / static_cast<float>(BackBufferHeight());
    float desired = DesiredAspectRatio();
    if (backBufferAspect > desired) {
        // wider: bars left and right
        ScreenScaleX() = desired / backBufferAspect * (static_cast<float>(BackBufferWidth()) / 640.0f);
        ScreenScaleY() = static_cast<float>(BackBufferHeight()) / 480.0f;
        auto width = static_cast<uint32_t>(ScreenScaleX() * 640.0f);
        ScreenOffsetX() = (BackBufferWidth() - width) / 2;
        ScreenOffsetY() = static_cast<uint32_t>(16.0f * ScreenScaleY());
    } else {
        ScreenScaleX() = static_cast<float>(BackBufferWidth()) / 640.0f;
        ScreenScaleY() = backBufferAspect / desired * (static_cast<float>(BackBufferHeight()) / 480.0f);
        auto height = static_cast<uint32_t>(ScreenScaleY() * 480.0f);
        ScreenOffsetX() = 0;
        ScreenOffsetY() =
            static_cast<uint32_t>(16.0f * ScreenScaleY() + static_cast<float>((BackBufferHeight() - height) / 2));
    }
    LetterboxActive() = letterbox;
}

// 0x0048C170: SetScreenBlur <amount>. (the PC port rewrote this function's first bytes with what they already are
// when DisableBlur is off - some patched executables "in the wild" remove the blur here.)
bool __cdecl CFunc_SetScreenBlur(Script::CStruct* params, Script::CScript* /*script*/)
{
    int amount = 0;
    params->GetInteger(0u, &amount, false); // the unnamed integer
    ScriptScreenBlur() = amount;
    SetScreenBlur(static_cast<uint32_t>(amount));
    return true;
}

} // namespace Nx

REPLACE(0x004A0780, Nx::SetScreenAspectRatio);
REPLACE(0x004A07C0, Nx::GetScreenAngleFactor);
REPLACE(0x004B2330, Nx::SetScreenBlur);
REPLACE(0x004B2440, Nx::SetLetterbox);
REPLACE_SOURCE_ONLY(0x0048C1A0, Nx::SetLetterbox); // a jump to 0x004B2440
REPLACE(0x0048C170, Nx::CFunc_SetScreenBlur);
