#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The screen's shape and full-screen effects (Nx engine, PC): aspect ratio, field-of-view factor, letterbox and
// motion blur. PartyMod replaces most of these with its own (widescreen support, blur off by default); the
// source here is what the game runs with PartyMod.
namespace Nx {

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
