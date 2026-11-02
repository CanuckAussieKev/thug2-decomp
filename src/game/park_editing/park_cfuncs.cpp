// Park editor script commands (CFunctions) that answer with the park's player counts: GetParkEditorMaxPlayers and
// GetParkEditorMaxPlayersPossible. net.qb's created-park player counts and the level select's / memory card's "too
// many players for this park" checks read their <MaxPlayers> / <MaxPlayersPossible>.
//
// The parameter names are written here as string literals, and that matters: these two are not no-ops and not
// optional. The original passes the names as pointers to the exe's own strings ("MaxPlayers" at 0x0064F29C,
// "MaxPlayersPossible" at 0x0064F2A8, used by nothing else). THUG2.exe (the main install's) has
// 0x90 filler bytes over 0x0064F29C-0x0064F2B9 (damage in that build; the reference build still has the
// names - tools/game/exe_audit.ts lists every byte the two differ in), so there the original added the value under
// the checksum of thirty 0x90 bytes and the scripts' <MaxPlayers> / <MaxPlayersPossible> were never set. With the
// names in our source, both commands behave as the original did on an undamaged exe, whichever exe runs.

#include "game/park_editing/park.h"

#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"

namespace park_editing {

// 0x0058E120
bool cfunc_get_park_editor_max_players(script::Struct*, script::Script* script)
{
    ParkAssembler* generator = ParkLayout::singleton()->builder_;
    script->args()->put_int("MaxPlayers", generator->get_player_limit()); // 0xB7E39B53
    return true;
}

// 0x0058E140
bool cfunc_get_park_editor_max_players_possible(script::Struct*, script::Script* script)
{
    ParkAssembler* generator = ParkLayout::singleton()->builder_;
    script->args()->put_int("MaxPlayersPossible", generator->get_player_cap_limit()); // 0x1FD6359C
    return true;
}

} // namespace park_editing
