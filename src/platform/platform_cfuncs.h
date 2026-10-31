#pragma once

namespace script {
class Struct;
class Script;
} // namespace Script

namespace sys {

// IsPS2: false on PC - the game's Function table (0x0067F3A0) points it at the shared "return false" stub
// 0x005B9E50 (also IsNGC's and others', so it can't be hooked on its own). With PartyMod's PS2 controls on
// (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md) it's true for the vehicle grind script
// (0x47F15883), so a vehicle grinds on the PS2's buttons.
bool __cdecl cfunc_is_ps2(script::Struct* params, script::Script* script);

// Point the Function table's IsPS2 entry (0x0068146C) at cfunc_is_ps2. Until that table is the game's own source,
// this stands in for its entry (done by sio::init_sdl_input with PS2 controls on, before the script functions are
// registered).
void install_is_ps2();

} // namespace Sys
