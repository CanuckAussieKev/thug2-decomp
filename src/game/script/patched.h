#pragma once

#include <cstdint>

// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

// Patched scripts on PC: .qb files loaded with a binary patch (BPS) applied - one: scripts\engine\menu\keyboard.qb
// (the on-screen keyboard, for typing with a real keyboard). The patched script is kept and reused when the file
// is loaded again.
namespace Script {

// The list of patches (at start-up; before it, nothing is patched).
void InitScriptPatches();

// Pip::Load(filename) - the file's data, or the patched copy of it if there's a patch for the file (Script::LoadQB
// loads .qb files through this). The patched copy isn't freed (nor are the original's file data: Pip::Unload
// does that).
const uint8_t* LoadPatchedScript(const char* filename);

} // namespace Script
