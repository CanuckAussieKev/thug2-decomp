#pragma once

#include <cstdint>

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The graphics options on PC, from [Graphics] in thug2-decomp.ini in place of the original's settings loader
// (0x005F34D0, the registry): AntiAliasing, HQShadows, DistanceClipping, ClippingDistance (1-100), Fog, and
// DisableBlur (on by default: SetScreenBlur keeps the motion blur off), and (NOT ORIGINAL) OcclusionCulling (on by
// default, as the original: game/gfx/occlusion.h).
namespace Nx {

// Read them (at start-up, before Platform::InitInput writes them).
void LoadGraphicsSettings();
// Put them where the engine reads its options (0x007D6434...): Platform::InitInput.
void WriteGraphicsSettings();

// The DisableBlur setting (see SetScreenBlur).
bool& BlurDisabled();

extern bool g_fogSetting;
extern bool g_distanceClipping;

extern uint8_t g_hqShadows;
} // namespace Nx
