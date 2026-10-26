// Park editor script commands (CFunctions) that answer with the park's player counts: GetParkEditorMaxPlayers and
// GetParkEditorMaxPlayersPossible. net.qb's created-park player counts and the level select's / memory card's "too
// many players for this park" checks read their <MaxPlayers> / <MaxPlayersPossible>.
//
// The parameter names are written here as string literals, and that matters: these two are not no-ops and not
// optional. The original passes the names as pointers to the exe's own strings ("MaxPlayers" at 0x0064F29C,
// "MaxPlayersPossible" at 0x0064F2A8, used by nothing else). The alternate THUG2.exe build the main install runs has
// 0x90 filler bytes over 0x0064F29C-0x0064F2B9 (damage in that build; the reference build still has the
// names - tools/pipeline/game/exe_audit.ts lists every byte the two differ in), so there the original added the value under
// the checksum of thirty 0x90 bytes and the scripts' <MaxPlayers> / <MaxPlayersPossible> were never set. With the
// names in our source, both commands behave as the original did on an undamaged exe, whichever exe runs.

#include "game/ed/park.h"

#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

// 0x0058E120
bool __cdecl CFunc_GetParkEditorMaxPlayers(Script::CStruct*, Script::CScript* script)
{
    CParkGenerator* generator = CParkManager::sInstance()->mp_generator;
    script->m_params->AddInteger("MaxPlayers", generator->GetMaxPlayers()); // 0xB7E39B53
    return true;
}

// 0x0058E140
bool __cdecl CFunc_GetParkEditorMaxPlayersPossible(Script::CStruct*, Script::CScript* script)
{
    CParkGenerator* generator = CParkManager::sInstance()->mp_generator;
    script->m_params->AddInteger("MaxPlayersPossible", generator->GetMaxPlayersPossible()); // 0x1FD6359C
    return true;
}

} // namespace Ed

REPLACE(0x0058E120, Ed::CFunc_GetParkEditorMaxPlayers);
REPLACE(0x0058E140, Ed::CFunc_GetParkEditorMaxPlayersPossible);
