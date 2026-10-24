#pragma once

#include <cstdint>

namespace Mem {
struct ListNode;
} // namespace Mem
namespace Script {
class CStruct;
} // namespace Script

// The save file types (by checksum) and what the memory card commands need to know about each: the file's
// name, the version scripts give it, its size and the blocks it takes, and the summary shown for a save - 0x005A5F70-
// 0x005A6610 and the helpers around the memory card commands.
namespace Mc {

// The save types' checksums (the type's name in the scripts).
constexpr uint32_t kSaveOptionsAndPros = 0xB010F357;  // "OptionsAndPros" (.SKA, letter a)
constexpr uint32_t kSaveCas = 0xFFC529F4;             // "Cas" (letter b)
constexpr uint32_t kSavePark = 0x3BF882CC;            // "Park" (.PRK, letter c)
constexpr uint32_t kSaveNetworkSettings = 0xCA41692D; // "NetworkSettings" (.NWS, letter e)
constexpr uint32_t kSaveCat = 0x61A1BC57;             // "Cat" (.CAT, letter f)
constexpr uint32_t kSaveCreatedGoals = 0x62896EDF;    // "CreatedGoals" (.GOA, letter g)
constexpr uint32_t kSaveCreatedGraphic = 0x79DAA2F9;  // "CreatedGraphic" (.GRF, letter h)
constexpr uint32_t kSaveReplay = 0x26C80B0D;          // "Replay" (.REP; only a file name)

// 0x005A5F70 (name in EAX, buffer in ESI, type in EDI; returns the buffer): 8 letters a-z from the name's
// checksum (base 26, lowest digit first), the last replaced by the type's letter (kept for an unknown type).
char* MakeFileName(const char* name, char* buffer, uint32_t type);
// 0x005A60C0 (the letter in AL): the save type of a file name's letter a-h (0 for d and any other).
uint32_t TypeFromLetter(char letter);
// 0x005A6180 (the type in EAX): the global integer <type>IconSpaceRequired (0 for an unknown type).
int IconSpaceRequired(uint32_t type);
// 0x005A6210 (the type in EAX): the global integer VERSION_<type> (-1 for an unknown type).
int TypeVersion(uint32_t type);
// 0x005A62A0 (the type in EAX, the name in ECX): "Save/<name>.<extension>" in a shared buffer (0x007CECE8; left as
// it was for an unknown type), which is returned.
const char* TypeFileName(uint32_t type, const char* name);
// 0x005A6390 (the type in EDI; cdecl name, buffer): "/<folder><file name>" (the console builds' save folder,
// 0x005B5F10). Returns sprintf's count.
int MakeFolderFileName(uint32_t type, const char* name, char* buffer);
// 0x005A63E0 (EDI, ESI): whether the first summary's date (year, month, day, hour, minutes, seconds) is later than
// the second's.
bool IsLater(Script::CStruct* first, Script::CStruct* second);
// 0x005A6CD0 (the size in ESI): the size rounded up to the platform's card block (0x007CEFA8: 1-3 and 7 1 KB,
// 4 16 KB, 5 8 KB, others 1).
int RoundUpToCardBlock(int size);
// 0x005A7340 (the type in ECX): the header (0x14) and the global integer SaveSize_<type> (0 for an unknown type),
// rounded up to the card block.
int TypeFileSize(uint32_t type);
// 0x005A8470 (cdecl type, size): the card blocks a save of `size` bytes takes - on platforms 1-3 plus the type's
// icon space and 4, on platform 4 plus 3.
int SaveBlocks(uint32_t type, int size);

// 0x005A6610 (the saved contents in EAX, the summary in ESI; cdecl the type): what a save of `type` shows - read
// from `saved`, or (null) from the game's state now:
//   CreatedGoals: num_edited_goals;
//   Park: num_edited_goals, MaxPlayers, num_gaps, num_pieces, theme, tod_script, width, length (the park map);
//   Cat: name; NetworkSettings: network_id;
//   OptionsAndPros: total_goals_complete / total_goals_possible / total_score / total_score_potential (the saved
//   career's - all four only if it has the first - or the skater's), and from a save also is_male and name.
void WriteSummary(Script::CStruct* saved, Script::CStruct* summary, uint32_t type);

// 0x005A6120 (the saved structure in ECX): the goal manager's parameters replaced by its GoalManagerParams, the
// career reset (mode 0) and the level's goals loaded again.
void __fastcall ApplyGoalManagerParams(Script::CStruct* saved);
// 0x005A6C80 (the loaded structure in EDI; cdecl the custom skater): the current skater's CAT info, list 0x44B66A9F
// and chapter info read from it, and the profile manager's custom skater from the argument (its result returned).
bool ApplyCatAndCustomSkater(Script::CStruct* loaded, Script::CStruct* customSkater);
// 0x005A8EB0: the loaded contents kept (0x007CECD4, the previous ones deleted) - read from `data` - with their type
// (0x007CECD8). Returns the end of what was read.
const uint8_t* KeepLoadedContents(const uint8_t* data, uint32_t type);

// The scalar deleting destructors of two list types (on the node; flag 1 frees it). Return the node.
struct ListDestructors {
    // 0x005A7520: Lst::Node<T>'s (vtable 0x0065797C): unlinked (its destructor body inlined), then freed.
    Mem::ListNode* NodeDestroy(unsigned int flags);
    // 0x005A8E80: the type with vtable 0x00658A60's - the same body (0x005BC990) called, then freed.
    Mem::ListNode* OtherDestroy(unsigned int flags);
};

// The naked entry points of the register-argument functions (also called by the self-tests).
void MakeFileNameStub();
void TypeFromLetterStub();
void IconSpaceRequiredStub();
void TypeVersionStub();
void TypeFileNameStub();
void MakeFolderFileNameStub();
void IsLaterStub();
void WriteSummaryStub();
void RoundUpToCardBlockStub();
void TypeFileSizeStub();
void ApplyCatAndCustomSkaterStub();

} // namespace Mc
