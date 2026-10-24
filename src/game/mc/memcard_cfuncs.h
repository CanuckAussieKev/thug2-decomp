#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

namespace Mc {

class Card;

// PartyMod's free space count for GetMemCardSpaceAvailable (fastcall: the card in ECX, as PartyMod's call at
// 0x005A6E7E has it): the disk's free 16 KB blocks if the card is there (its +0x0C), clamped to 0x7FFE0001.
int32_t __fastcall FreeDiskBlocks(Card* card);

// 0x005A6E40: script function GetMemCardSpaceAvailable.
bool GetMemCardSpaceAvailable(Script::CStruct* params, Script::CScript* script);

// 0x005A6550 / 0x005A6600: the flags of the last save (not enough space, 0x007CECD0) and load (a custom skater
// was loaded, 0x007CECE0).
bool SaveFailedDueToInsufficientSpace(Script::CStruct* params, Script::CScript* script);
bool LoadedCustomSkater(Script::CStruct* params, Script::CScript* script);
// 0x005A6560: SetSectionsToApplyWhenLoading ApplyCustomSkater (8) / ApplyStorySkater (4) / ApplyStory (2) /
// 0xB39D7CC4 (1) / all (every bit): which sections of a loaded save are applied. True.
bool SetSectionsToApplyWhenLoading(Script::CStruct* params, Script::CScript* script);
// The sections (0x007CECDC).
uint32_t& SectionsToApplyWhenLoading();

// 0x005A6D30: GetMostRecentSave <array of summaries> <type>: MostRecentSave in the script's parameters (removed
// first) - the latest summary of that type (or of none), bad ones (BadVersion / Corrupt) only on platforms 4 and 5.
// False if there is none.
bool GetMostRecentSave(Script::CStruct* params, Script::CScript* script);
// 0x005A7210: FormatCard (music and streams paused around it): the card's always-false format.
bool FormatCard(Script::CStruct* params, Script::CScript* script);
// 0x005A7260 / 0x005A7280: whether there is a card / whether it's formatted (always, with a card).
bool CardIsInSlot(Script::CStruct* params, Script::CScript* script);
bool CardIsFormatted(Script::CStruct* params, Script::CScript* script);
// 0x005A72A0: GetSummaryInfo <type> [VaultData=n]: the summary (WriteSummary) of the game's state now, or with
// VaultData of the kept contents (their own type). True.
bool GetSummaryInfo(Script::CStruct* params, Script::CScript* script);
// 0x005A7430: type = the kept contents' type. 0x005A7450: MaxTHPS4FilesAllowed = 75. True.
bool GetMemCardDataForUpload(Script::CStruct* params, Script::CScript* script);
bool GetMaxTHPS4FilesAllowed(Script::CStruct* params, Script::CScript* script);
// 0x005A7470: the component, structure and vector pools' second (temporary) pools made current - if all three
// exist (true); else nothing changes (false). 0x005A74F0: back to the first ones (if any isn't). True.
bool SwitchToTempPoolsIfTheyExist(Script::CStruct* params, Script::CScript* script);
bool SwitchToRegularPools(Script::CStruct* params, Script::CScript* script);

// 0x005A8730: script function LoadFromMemoryCard (Originally from PartyMod Fixes: without the save checksum test).
bool LoadFromMemoryCard(Script::CStruct* params, Script::CScript* script);

} // namespace Mc
