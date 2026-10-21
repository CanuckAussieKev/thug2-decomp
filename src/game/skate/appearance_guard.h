#pragma once

#include <cstdint>

// NOT ORIGINAL - received skater appearances made safe (register: tools/compat/thugpro/README.md "Run time", TP-R5;
// record: docs/pending/thugpro-guards.md). A skater's appearance is a structure of CAS slots - {skater_m_head =
// {desc_id = ...} board = {desc_id = ...} ...}; the model is built by looking each desc_id up in the global array named
// after its slot (0x00485CF0 / 0x00489050), and THUG2 never checks the answer. A THUGPRO player's appearance can name
// parts THUG2 hasn't (THUGPRO's extra heads, pros, boards: ~110 of them) or slots it hasn't. Every appearance read from
// the network (CSkaterProfile::ReadFromBuffer: the player create, join request and player info messages) goes through
// Sanitise first, so a part we can't build falls back to one we can, slot by slot:
//   - a slot whose global part list doesn't exist (a slot THUG2 hasn't): taken out;
//   - a part not in its slot's list: the slot's `None` if the list has one (an empty slot), else the THUG2 default
//     custom skater's part for the slot (appearance_custom_skater_male), else the list's first;
//   - a `body` we haven't (THUGPRO's full-body pros): the whole appearance becomes the THUG2 default custom skater's
//     (a body names the rest of the skater: the other slots don't fit another body).
// THUGPRO's own parts are kept when they were loaded from the user's THUGPRO install (thugpro_cas.h): then they are in
// the lists. Every change is logged.
namespace Script {
class CArray;
class CStruct;
} // namespace Script

namespace Obj::AppearanceGuard {

// Fix `appearance` in place; returns how many slots were changed (0: it was fine).
int Sanitise(Script::CStruct* appearance);

// Where the part lists and the default skater come from (the self-tests swap them): the global array named after a
// slot; appearance_custom_skater_male.
struct Catalog {
    Script::CArray* (*partList)(uint32_t slot);
    Script::CStruct* (*defaults)();
};
const Catalog& GameCatalog();
void SetCatalog(const Catalog& catalog);

} // namespace Obj::AppearanceGuard
