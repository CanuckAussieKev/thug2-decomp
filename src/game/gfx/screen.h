#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The screen's shape and full-screen effects (Nx engine, PC): aspect ratio, field-of-view factor, letterbox and
// motion blur. The PC port (based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt) changes most
// of these: widescreen support, blur off by default.
namespace Nx {

// The career's current level (0 = the front end); 0 without the skate module (0x007CE478, its career +0x20,
// that's +0x630).
uint8_t CurrentLevel();
// The aspect the screen is fitted to: 4:3 in the front end, the window's shape in a level.
float DesiredAspectRatio();

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
