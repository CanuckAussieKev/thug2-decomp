#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The screen's shape and full-screen effects (Nx engine, PC): aspect ratio, field-of-view factor, letterbox and
// motion blur. The PC port (contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md) changes most
// of these: widescreen support, blur off by default.
namespace Nx {

// The career's current level (0 = the front end); 0 without the skate module (0x007CE478, its career +0x20,
// that's +0x630).
uint8_t CurrentLevel();
// The aspect the screen is fitted to: 4:3 in the front end, the window's shape in a level. In the front end
// this is the 2D menu layout's shape (see FrontEndWidescreen).
float DesiredAspectRatio();

// thug2-decomp.ini [features] widescreen_menus (default 1): in the front end the 3D scene behind the menus fills
// the window (widescreen field of view, no pillarbox bars) while the 2D menu layout stays 4:3 and centred. With 0,
// the front end is pillarboxed 4:3 as in the PC port. Read from the ini once; self-tests set it for their run.
bool& WidescreenMenus();
// Whether that applies right now: the setting is on and we're in the front end.
bool FrontEndWidescreen();
// The aspect the 3D view is fitted to: the window's shape when FrontEndWidescreen(), else DesiredAspectRatio().
float Desired3DAspectRatio();

// 0x004A0780
void SetScreenAspectRatio(float aspect);
// 0x004A07C0: the factor the horizontal field of view is scaled by.
float GetScreenAngleFactor();
// 0x004B2330: the motion blur amount (0 = off, up to 255).
void SetScreenBlur(uint32_t amount);
// 0x004B2440
void SetLetterbox(bool letterbox);

// 0x0048C170: SetScreenBlur <amount>
bool __cdecl CFunc_SetScreenBlur(Script::CStruct* params, Script::CScript* script);

} // namespace Nx
