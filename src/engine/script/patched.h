#pragma once

#include <cstdint>

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// Patched scripts on PC: .qb files loaded with a binary patch (BPS) applied - one: scripts\engine\menu\keyboard.qb
// (the on-screen keyboard, for typing with a real keyboard). The patched script is kept and reused when the file
// is loaded again.
namespace script {

// The list of patches (at start-up; before it, nothing is patched).
void init_script_patches();

// packs::ReadIn(filename) - the file's data, or the patched copy of it if there's a patch for the file (Scripts::LoadQB
// loads .qb files through this). The patched copy isn't freed (nor are the original's file data: packs::offload
// does that).
const uint8_t* load_patched_script(const char* filename);

} // namespace Script
