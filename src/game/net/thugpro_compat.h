#pragma once

#include <cstdint>

// NOT ORIGINAL - agreeing with THUGPRO (Morten1337's THUG2 mod) in network games, at run time (register:
// tools/compat/thugpro/README.md, "Run time"; record: docs/pending/thugpro-guards.md). Ideas from Morten1337's
// thugpro_proxy, used with his permission and written here in this project's own way.
//
// TP-R2 - big levels: a skater's position travels as three 16-bit integers (the skater updates, message 0x23: the
// client's own, 0x00512EA0 / 0x005131A0; the server's relay, 0x005696C0 / 0x005698F0; their readers 0x0056BE60 and
// HandleObjectUpdates 0x0056E470). THUG2 sends x and z in half units and y in quarter units: +-16384 / +-8192 units.
// THUGPRO's THUGPRO_SetBigLevel (run from its load_level in network games) sends whole units instead in a level with
// IsBigLevel (x and z: its The Mall, z_ma) or IsTallLevel (y). Ours: the same, decided by the level select's entry for
// the current level (tools/menu_level/level_select.ts marks the levels named in tools/compat/thugpro/thaw_levels.json
// "net_position_scale"); off outside network games.
namespace GameNet::Thugpro {

struct PositionScale {
    bool big = false;  // x and z in whole units (else halves)
    bool tall = false; // y in whole units (else quarters)
};

// What the network code uses now.
const PositionScale& CurrentPositionScale();
// Set it (the self-tests; RefreshPositionScale sets it in the game).
void SetPositionScale(const PositionScale& scale);
// Once a frame (the main loop): the current level's scale while a network game is on, none otherwise. Logs a change.
void RefreshPositionScale();
// The level select entry flags for a level (its load script): IsBigLevel / IsTallLevel.
PositionScale LevelPositionScale(uint32_t level);

// A position coordinate as sent (the original's _ftol: truncated toward zero, kept to 16 bits) and as read.
int16_t PackHorizontal(float value); // x, z
int16_t PackHeight(float value);     // y
float UnpackHorizontal(int value);
float UnpackHeight(int value);

} // namespace GameNet::Thugpro
