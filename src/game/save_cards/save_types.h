#pragma once

#include <cstdint>

namespace memory {
struct ListNode;
} // namespace memory
namespace script {
class Struct;
} // namespace script

// The save file types (by checksum) and what the memory card commands need to know about each: the file's
// name, the version scripts give it, its size and the blocks it takes, and the summary shown for a save - 0x005A5F70-
// 0x005A6610 and the helpers around the memory card commands.
namespace save_cards {

// The save types' checksums (the type's name in the scripts).
constexpr uint32_t kSaveOptionsAndPros = 0xB010F357;  // "OptionsAndPros" (.SKA, letter a)
constexpr uint32_t kSaveCas = 0xFFC529F4;             // "Cas" (letter b)
constexpr uint32_t kSavePark = 0x3BF882CC;            // "Park" (.PRK, letter c)
constexpr uint32_t kSaveNetworkSettings = 0xCA41692D; // "NetworkSettings" (.NWS, letter e)
constexpr uint32_t kSaveCat = 0x61A1BC57;             // "Cat" (.CAT, letter f)
constexpr uint32_t kSaveCreatedGoals = 0x62896EDF;    // "CreatedGoals" (.GOA, letter g)
constexpr uint32_t kSaveCreatedGraphic = 0x79DAA2F9;  // "CreatedGraphic" (.GRF, letter h)
constexpr uint32_t kSaveReplay = 0x26C80B0D;          // "Replay" (.REP; only a file name)

// 0x005A5F70 (returns the buffer): 8 letters a-z from the name's
// checksum (base 26, lowest digit first), the last replaced by the type's letter (kept for an unknown type).
char* make_file_name(const char* name, char* buffer, uint32_t type);
// 0x005A60C0: the save type of a file name's letter a-h (0 for d and any other).
uint32_t type_from_letter(char letter);
// 0x005A6180: the global integer <type>icon_space_required (0 for an unknown type).
int icon_space_required(uint32_t type);
// 0x005A6210: the global integer VERSION_<type> (-1 for an unknown type).
int type_version(uint32_t type);
// 0x005A62A0: "Save/<name>.<extension>" in a shared buffer (0x007CECE8; left as
// it was for an unknown type), which is returned.
const char* get_type_file_name(uint32_t type, const char* name);
// 0x005A6390: "/<folder><file name>" (the console builds' save folder,
// 0x005B5F10). Returns sprintf's count.
int make_folder_file_name(uint32_t type, const char* name, char* buffer);
// 0x005A63E0: whether the first summary's date (year, month, day, hour, minutes, seconds) is later than
// the second's.
bool is_later(script::Struct* first, script::Struct* second);
// 0x005A6CD0: the size rounded up to the platform's card block (0x007CEFA8: 1-3 and 7 1 KB,
// 4 16 KB, 5 8 KB, others 1).
int round_up_to_card_block(int size);
// 0x005A7340: the header (0x14) and the global integer SaveSize_<type> (0 for an unknown type),
// rounded up to the card block.
int type_file_size(uint32_t type);
// 0x005A8470 (cdecl type, size): the card blocks a save of `size` bytes takes - on platforms 1-3 plus the type's
// icon space and 4, on platform 4 plus 3.
int save_blocks(uint32_t type, int size);

// 0x005A6610: what a save of `type` shows - read
// from `saved`, or (null) from the game's state now:
//   CreatedGoals: num_edited_goals;
//   Park: num_edited_goals, MaxPlayers, num_gaps, num_pieces, theme, tod_script, width, length (the park map);
//   Cat: name; NetworkSettings: network_id;
//   OptionsAndPros: total_goals_complete / total_goals_possible / total_score / total_score_potential (the saved
//   career's - all four only if it has the first - or the skater's), and from a save also is_male and name.
void write_summary(script::Struct* saved, script::Struct* summary, uint32_t type);

// 0x005A6120 (the saved structure in ECX): the goal manager's parameters replaced by its GoalManagerParams, the
// career reset (mode 0) and the level's goals loaded again.
void __fastcall apply_goal_manager_params(script::Struct* saved);
// 0x005A6C80: the current skater's CAT info, list 0x44B66A9F
// and chapter info read from it, and the profile manager's custom skater from the argument (its result returned).
bool apply_cat_and_custom_skater(script::Struct* loaded, script::Struct* custom_skater);
// 0x005A8EB0: the loaded contents kept (0x007CECD4, the previous ones deleted) - read from `data` - with their type
// (0x007CECD8). Returns the end of what was read.
const uint8_t* keep_loaded_contents(const uint8_t* data, uint32_t type);

// The scalar deleting destructors of two list types (on the node; flag 1 frees it). Return the node.
struct ListDestructors {
    // 0x005A7520: lists::Elem<T>'s (vtable 0x0065797C): unlinked (its destructor body inlined), then freed.
    memory::ListNode* node_destroy(unsigned int flags);
    // 0x005A8E80: the type with vtable 0x00658A60's - the same body (0x005BC990) called, then freed.
    memory::ListNode* other_destroy(unsigned int flags);
};

} // namespace save_cards
