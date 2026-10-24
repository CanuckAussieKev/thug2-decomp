#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// What a save holds and how it is put back - 0x005A7560-0x005A8470: the global info (options, sound, controllers,
// taunts, career, records, pros), each save type's contents written into a structure, and a loaded save's contents
// applied (the sections SetSectionsToApplyWhenLoading picked, for OptionsAndPros).
namespace Mc {

// 0x005A7560: GlobalInfo's contents written into `global`: Options (SplitScreenPreferences, SoundOptions -
// MainVolume, MusicVolume, the forbidden track flags, current_soundtrack on the PC, RandomMode -,
// ControllerPreferences - VibrationOn / AutokickOn / SpinTapsOn for each of 8 controllers -, Taunts), the career,
// the game records and Pros.
void WriteGlobalInfo(Script::CStruct* global);
// 0x005A7CF0: the same read back from `global` and applied (the music volume through the Change script, the
// soundtrack through set_loaded_soundtrack); LastLevelLoadScript / LastGameMode left in the script's parameters.
void ReadGlobalInfo(Script::CStruct* global, Script::CScript* script);
// 0x005A79A0 (the structure in EBX; cdecl the type): the save's contents for `type`: the goal editor's goals, the
// CAT (other_params, rotation_info, animation_info), the park, the network settings, OptionsAndPros (GlobalInfo,
// Story, StorySkater, CustomSkater - then also what CreatedGraphic has), CreatedGraphic (layer_infos).
void WriteSaveContents(Script::CStruct* contents, uint32_t type);
// 0x005A81B0 (the type in EAX, the contents in ESI; cdecl script, params): the loaded contents put in place.
void ApplyLoadedContents(uint32_t type, Script::CStruct* contents, Script::CScript* script, Script::CStruct* params);

// The naked entry points (also called by the self-tests).
void WriteSaveContentsStub();
void ApplyLoadedContentsStub();

} // namespace Mc
