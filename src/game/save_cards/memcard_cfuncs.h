#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

namespace save_cards {

class SaveCard;

// PartyMod's free space count for GetMemCardSpaceAvailable (fastcall: the card in ECX, as PartyMod's call at
// 0x005A6E7E has it): the disk's free 16 KB blocks if the card is there (its +0x0C), clamped to 0x7FFE0001.
int32_t free_disk_blocks(SaveCard* card);

// 0x005A6E40: script function GetMemCardSpaceAvailable.
bool get_mem_card_space_available(script::Struct* params, script::Context context);

// 0x005A6550 / 0x005A6600: the flags of the last save (not enough space, 0x007CECD0) and load (a custom skater
// was loaded, 0x007CECE0).
bool save_failed_due_to_insufficient_space(script::Struct* params, script::Context context);
bool get_loaded_custom_skater(script::Struct* params, script::Context context);
// 0x005A6560: SetSectionsToApplyWhenLoading ApplyCustomSkater (8) / ApplyStorySkater (4) / ApplyStory (2) /
// 0xB39D7CC4 (1) / all (every bit): which sections of a loaded save are applied. True.
bool set_sections_to_apply_when_loading(script::Struct* params, script::Context context);
// The sections (0x007CECDC).
uint32_t& sections_to_apply_when_loading();

// 0x005A6D30: GetMostRecentSave <array of summaries> <type>: MostRecentSave in the script's parameters (removed
// first) - the latest summary of that type (or of none), bad ones (BadVersion / Corrupt) only on platforms 4 and 5.
// False if there is none.
bool get_most_recent_save(script::Struct* params, script::Context context);
// 0x005A7210: FormatCard (music and streams paused around it): the card's always-false format.
bool format_card(script::Struct* params, script::Context context);
// 0x005A7260 / 0x005A7280: whether there is a card / whether it's formatted (always, with a card).
bool card_is_in_slot(script::Struct* params, script::Context context);
bool card_is_formatted(script::Struct* params, script::Context context);
// 0x005A72A0: GetSummaryInfo <type> [VaultData=n]: the summary (write_summary) of the game's state now, or with
// VaultData of the kept contents (their own type). True.
bool get_summary_info(script::Struct* params, script::Context context);
// 0x005A7430: type = the kept contents' type. 0x005A7450: MaxTHPS4FilesAllowed = 75. True.
bool get_mem_card_data_for_upload(script::Struct* params, script::Context context);
bool get_max_thps4_files_allowed(script::Struct* params, script::Context context);
// 0x005A7470: the component, structure and vector pools' second (temporary) pools made current - if all three
// exist (true); else nothing changes (false). 0x005A74F0: back to the first ones (if any isn't). True.
bool switch_to_temp_pools_if_they_exist(script::Struct* params, script::Context context);
bool switch_to_regular_pools(script::Struct* params, script::Context context);

// 0x005A8730: script function LoadFromMemoryCard (Originally from PartyMod Fixes: without the save checksum test).
bool load_from_memory_card(script::Struct* params, script::Context context);

} // namespace save_cards
