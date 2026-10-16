// The screen's shape and full-screen effects (Nx engine, PC) - with PartyMod's replacements built in.

#include "game/gfx/screen.h"

#include "game/script/params.h"
#include "hook/game.h"
#include "hook/hook.h"

// PartyMod's replacements (src/partymod/gfx.c); it jumps from the starts of the originals to these.
extern "C" void __cdecl setAspectRatio(float aspect);
extern "C" float __cdecl getScreenAngleFactor();
extern "C" void __cdecl setBlurWrapper(uint32_t blur);
extern "C" void __cdecl setLetterbox(int isLetterboxed);
extern "C" int blur_disabled; // PartyMod's DisableBlur setting ([Graphics] in partymod.ini, on by default)

namespace Nx {

namespace {

// Whether the device can do the blur (its render-target textures were made).
auto& BlurAvailable() { return GameVariable<uint32_t>(0x00786D40); }
auto& ScreenBlur() { return GameVariable<uint32_t>(0x00787328); }
// The last amount the script asked for (kept even when the blur is off).
auto& ScriptScreenBlur() { return GameVariable<int>(0x006FA72C); }

} // namespace

// 0x004A0780. The original stores `aspect` as the screen's aspect (0x00701340). PartyMod stores the aspect it wants instead
// (4:3 in the front end, the window's shape in a level) and ignores `aspect`.
void SetScreenAspectRatio(float aspect)
{
    setAspectRatio(aspect);
}

// 0x004A07C0. The original returns the constant at 0x0067E544 (1.0). PartyMod widens the view for screens wider
// than 4:3 (the aspect / (4/3), unless that constant isn't 1) and also updates the screen's aspect (0x00701340).
float GetScreenAngleFactor()
{
    return getScreenAngleFactor();
}

// 0x004B2330. The original: if (BlurAvailable()) ScreenBlur() = amount;
// PartyMod's DisableBlur setting (on by default) turns the blur off for good instead.
void SetScreenBlur(uint32_t amount)
{
    if (blur_disabled) {
        setBlurWrapper(amount); // ScreenBlur() = 0
        return;
    }
    if (BlurAvailable())
        ScreenBlur() = amount;
}

// 0x004B2440. The original squashes the 3D view vertically to 3/4 of the screen, centred, and back:
//     int quarter = backbufferHeight / 4;
//     if (letterbox) { if (!active) { offsetY += quarter / 2; scaleY = 0.75f; active = true; } }
//     else if (active) { offsetY += -quarter / 2; scaleY = 1.0f; active = false; }
// (active 0x00786CBE, scaleY 0x00786D84, offsetY 0x00786D8C, backbufferHeight 0x00786D70). PartyMod replaces it:
// it fits the 640x480 screen layout to the aspect it wants (pillarbox or letterbox) and sets the flag, which it
// then uses for its own display region.
void SetLetterbox(bool letterbox)
{
    setLetterbox(letterbox);
}

// 0x0048C170: SetScreenBlur <amount>. (PartyMod rewrites this function's first bytes with what they already are
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

// PartyMod jumps from the starts of the first four to its own, and may write over the fifth's first bytes, so
// none can carry our hook.
REPLACE_SOURCE_ONLY(0x004A0780, Nx::SetScreenAspectRatio);
REPLACE_SOURCE_ONLY(0x004A07C0, Nx::GetScreenAngleFactor);
REPLACE_SOURCE_ONLY(0x004B2330, Nx::SetScreenBlur);
REPLACE_SOURCE_ONLY(0x004B2440, Nx::SetLetterbox);
REPLACE_SOURCE_ONLY(0x0048C170, Nx::CFunc_SetScreenBlur);
