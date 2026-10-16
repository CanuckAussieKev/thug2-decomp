#pragma once

// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

// The graphics options on PC, from [Graphics] in partymod.ini in place of the original's settings loader
// (0x005F34D0, the registry): AntiAliasing, HQShadows, DistanceClipping, ClippingDistance (1-100), Fog, and
// DisableBlur (on by default: SetScreenBlur keeps the motion blur off).
namespace Nx {

// Read them (at start-up, before Platform::InitInput writes them).
void LoadGraphicsSettings();
// Put them where the engine reads its options (0x007D6434...): Platform::InitInput.
void WriteGraphicsSettings();

// The DisableBlur setting (see SetScreenBlur).
bool& BlurDisabled();

} // namespace Nx
