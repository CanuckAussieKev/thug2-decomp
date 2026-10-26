#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script functions (CFuncs) for the save files on the memory card: deleting one, whether one exists and the space
// they need, and the temporary script pools the save code works in. All are cdecl (params, script) and return
// whether the script's `if` should be taken.
namespace Mc {

// 0x005A6EA0: DeleteMemCardFile [CardFileName=] [UserFileName= Type=] [XBoxDirectoryName=]: the save file
// removed - on the PC (and platform 7) the card file name, which is needed; on the consoles also the save's folder
// with its icons. Music and streams are paused around it (except on the PC). False without a card or on a failure.
bool DeleteMemCardFile(Script::CStruct* params, Script::CScript* script);
// 0x005A8500: GetMemCardSpaceRequired <type>: SpaceRequired (card blocks) for a save of that type. True.
bool GetMemCardSpaceRequired(Script::CStruct* params, Script::CScript* script);
// 0x005A8550: MemCardFileExists Name= Type=: whether the save file can be opened; if so, total_file_size (its
// card blocks) in the script's parameters. On the PC a file that can't be opened is removed.
bool MemCardFileExists(Script::CStruct* params, Script::CScript* script);
// 0x005A8BF0: GetSaveInfo type=<type>: the save's contents (MainStructure) and summary (SummaryInfo) made now,
// with their sizes (MainStructureSize, SummaryInfoSize, MaxSummaryInfoSize 100, TotalSize with the header,
// MaxTotalSize the type's file size) in the script's parameters. True.
bool GetSaveInfo(Script::CStruct* params, Script::CScript* script);
// 0x005A8F70: GetMemCardDirectoryListing [FileType=<type>]: DirectoryListing - a structure per save of the game
// (75 at most; newest first): file_type, index, the date, actual_file_name, total_file_size and the summary, or
// corrupt with the damaged-file text - TotalTHPS4FilesOnCard and FilesLimitReached in the script's parameters. True.
bool GetMemCardDirectoryListing(Script::CStruct* params, Script::CScript* script);
// 0x005A97E0: SaveToMemoryCard Name= Type= [savevaultdata=n]: the save (or the kept contents) written - the header,
// the summary, the contents (NetworkSettings' stored plus 0x69) and the padding up to the type's size. Whether it
// was; SaveFailedDueToInsufficientSpace's flag set from the card.
bool SaveToMemoryCard(Script::CStruct* params, Script::CScript* script);
// 0x005A7420: the summary writer's callback on the PC: the music and sounds updated.
void __cdecl KeepSoundGoing();
// 0x005A8BB0: the kept loaded contents (and their type) dropped. True.
bool ClearMemCardDataForUpload(Script::CStruct* params, Script::CScript* script);
// 0x005A8DA0: unless they exist, the second (reserve) pools of components (50000), structures (50000) and
// vectors (10000), made on the top-down heap. True.
bool CreateTemporaryMemCardPools(Script::CStruct* params, Script::CScript* script);
// 0x005A9700: those reserve pools deleted. True.
bool RemoveTemporaryMemCardPools(Script::CStruct* params, Script::CScript* script);

} // namespace Mc
