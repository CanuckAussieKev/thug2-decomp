#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// What a save holds and how it is put back - 0x005A7560-0x005A8470: the global info (options, sound, controllers,
// taunts, career, records, pros), each save type's contents written into a structure, and a loaded save's contents
// applied (the sections SetSectionsToApplyWhenLoading picked, for OptionsAndPros).
namespace save_cards {

// 0x005A7560: GlobalInfo's contents written into `global`: Options (SplitScreenPreferences, SoundOptions -
// master_loudness, MusicVolume, the forbidden track flags, current_soundtrack on the PC, RandomMode -,
// ControllerPreferences - VibrationOn / AutokickOn / SpinTapsOn for each of 8 controllers -, Taunts), the career,
// the game records and Pros.
void write_global_info(script::Struct* global);
// 0x005A7CF0: the same read back from `global` and applied (the music volume through the Change script, the
// soundtrack through set_loaded_soundtrack); LastLevelLoadScript / LastGameMode left in the script's parameters.
void read_save_header(script::Struct* global, script::Script* script);
// 0x005A79A0 (the structure in EBX; cdecl the type): the save's contents for `type`: the goal editor's goals, the
// CAT (other_params, rotation_info, animation_info), the park, the network settings, OptionsAndPros (GlobalInfo,
// Story, StorySkater, CustomSkater - then also what CreatedGraphic has), CreatedGraphic (layer_infos).
void write_save_contents(script::Struct* contents, uint32_t type);
// 0x005A81B0 (the type in EAX, the contents in ESI; cdecl script, params): the loaded contents put in place.
void apply_loaded_contents(uint32_t type, script::Struct* contents, script::Script* script, script::Struct* params);

// The naked entry points (also called by the self-tests).
void write_save_contents_stub();
void apply_loaded_contents_stub();

} // namespace save_cards
