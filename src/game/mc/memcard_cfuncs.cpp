// Script functions (CFuncs) for the memory card.

#include "game/mc/memcard_cfuncs.h"

#include "game/mc/memcard.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

// PartyMod's free-space count (src/partymod/main.c): the disk's free 16 KB blocks, clamped so that scripts can
// add to it without overflowing (the original's count is unclamped).
extern "C" int32_t __fastcall calcAvailableSpaceFudged(uint8_t* card);

namespace Mc {

namespace {

constexpr uint32_t kSpaceAvailable = 0x0C37C363; // "SpaceAvailable"
constexpr uint32_t kFilesLeft = 0x0855B2FC;      // "FilesLeft"

// The calling script's own parameters (Script::CScript +0x14): where CFuncs leave results for it.
Script::CStruct* ResultParams(Script::CScript* script)
{
    return *reinterpret_cast<Script::CStruct**>(reinterpret_cast<char*>(script) + 0x14);
}

} // namespace

// 0x005A6E40. Results for the script: SpaceAvailable (16 KB blocks; 0 until a card is found) and FilesLeft
// (always 1000000). False without a card.
bool GetMemCardSpaceAvailable(Script::CStruct*, Script::CScript* script)
{
    ResultParams(script)->AddInteger(kSpaceAvailable, 0);
    ResultParams(script)->AddInteger(kFilesLeft, 1000000);
    Card* card = GameVariable<Manager*>(0x007D1890)->GetCard(0, 0);
    if (!card)
        return false;
    // PartyMod: its clamped count in place of the original's card->GetFreeBlocks().
    ResultParams(script)->AddInteger(kSpaceAvailable, calcAvailableSpaceFudged(reinterpret_cast<uint8_t*>(card)));
    return true;
}

} // namespace Mc

REPLACE(0x005A6E40, Mc::GetMemCardSpaceAvailable);
