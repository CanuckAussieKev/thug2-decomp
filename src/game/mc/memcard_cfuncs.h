#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

namespace MemCard {

class Card;

// PartyMod's free space count for GetMemCardSpaceAvailable (fastcall: the card in ECX, as PartyMod's call at
// 0x005A6E7E has it): the disk's free 16 KB blocks if the card is there (its +0x0C), clamped to 0x7FFE0001.
int32_t __fastcall FreeDiskBlocks(Card* card);

// 0x005A6E40: script function GetMemCardSpaceAvailable.
bool GetMemCardSpaceAvailable(Scripts::CStruct* params, Scripts::CScript* script);

// 0x005A6550 / 0x005A6600: the flags of the last save (not enough space, 0x007CECD0) and load (a custom skater
// was loaded, 0x007CECE0).
bool SaveFailedDueToInsufficientSpace(Scripts::CStruct* params, Scripts::CScript* script);
bool LoadedCustomSkater(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A6560: SetSectionsToApplyWhenLoading ApplyCustomSkater (8) / ApplyStorySkater (4) / ApplyStory (2) /
// 0xB39D7CC4 (1) / all (every bit): which sections of a loaded save are applied. True.
bool SetSectionsToApplyWhenLoading(Scripts::CStruct* params, Scripts::CScript* script);
// The sections (0x007CECDC).
uint32_t& SectionsToApplyWhenLoading();

// 0x005A6D30: GetMostRecentSave <array of summaries> <type>: MostRecentSave in the script's parameters (removed
// first) - the latest summary of that type (or of none), bad ones (BadVersion / Corrupt) only on platforms 4 and 5.
// False if there is none.
bool GetMostRecentSave(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A7210: FormatCard (music and streams paused around it): the card's always-false format.
bool FormatCard(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A7260 / 0x005A7280: whether there is a card / whether it's formatted (always, with a card).
bool CardIsInSlot(Scripts::CStruct* params, Scripts::CScript* script);
bool CardIsFormatted(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A72A0: GetSummaryInfo <type> [VaultData=n]: the summary (WriteSummary) of the game's state now, or with
// VaultData of the kept contents (their own type). True.
bool GetSummaryInfo(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A7430: type = the kept contents' type. 0x005A7450: MaxTHPS4FilesAllowed = 75. True.
bool GetMemCardDataForUpload(Scripts::CStruct* params, Scripts::CScript* script);
bool GetMaxTHPS4FilesAllowed(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A7470: the component, structure and vector pools' second (temporary) pools made current - if all three
// exist (true); else nothing changes (false). 0x005A74F0: back to the first ones (if any isn't). True.
bool SwitchToTempPoolsIfTheyExist(Scripts::CStruct* params, Scripts::CScript* script);
bool SwitchToRegularPools(Scripts::CStruct* params, Scripts::CScript* script);

// 0x005A8730: script function LoadFromMemoryCard (Originally from PartyMod Fixes: without the save checksum test).
bool LoadFromMemoryCard(Scripts::CStruct* params, Scripts::CScript* script);

extern bool g_outOfSpace;
extern Scripts::CStruct* g_keptParams;
extern uint32_t g_keptType;
extern bool g_loadedCustomSkater;

} // namespace Mc
