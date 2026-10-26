#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

namespace Sys {

// IsPS2: false on PC - the game's CFunction table (0x0067F3A0) points it at the shared "return false" stub
// 0x005B9E50 (also IsNGC's and others', so it can't be hooked on its own). With PartyMod's PS2 controls on
// (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md) it's true for the vehicle grind script
// (0x47F15883), so a vehicle grinds on the PS2's buttons.
bool __cdecl CFunc_IsPS2(Script::CStruct* params, Script::CScript* script);

// Point the CFunction table's IsPS2 entry (0x0068146C) at CFunc_IsPS2. Until that table is the game's own source,
// this stands in for its entry (done by Sio::InitSdlInput with PS2 controls on, before the script functions are
// registered).
void InstallIsPS2();

} // namespace Sys
