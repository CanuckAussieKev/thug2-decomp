#pragma once

#include <cstddef>
#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) of the movies, 0x005AD830-0x005AE36F: skater camera animations and cutscenes (the
// skate module's movie manager +0x368), object animations (its second movie manager +0x36C: GameObjects::CVideoHub,
// movie_manager.h), and the cutscene destroy list's entry. All the commands are cdecl (params, script) and return
// whether the script's `if` should be taken (true where nothing is asked).
namespace ScriptCommands {

// An entry of the cutscene destroy list (AddToCutsceneDestroyList, 0x005B5320; 0xC bytes): the object's name, the
// script that destroys it (destroy_func) and its parameters (destroy_params, a structure of the entry's own).
struct CutsceneDestroyEntry {
    uint32_t m_label;            // +0x00
    uint32_t m_destroyScript;   // +0x04
    Scripts::CStruct* m_args;  // +0x08
    // 0x005ADF60: the constructor: no name or script, a new empty structure (null if out of memory). Returns this.
    CutsceneDestroyEntry* MakeNew();
};
static_assert(sizeof(CutsceneDestroyEntry) == 0xC && offsetof(CutsceneDestroyEntry, m_args) == 8,
              "layout must match the original");

bool __cdecl CFunc_PlaySkaterCamAnim(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AD830
// 0x005AD960 / 0x005ADA30: also SetCutsceneSkippable / SetCutsceneShouldPause (the same functions).
bool __cdecl CFunc_SetSkaterCamAnimSkippable(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005AD960
bool __cdecl CFunc_SetSkaterCamAnimShouldPause(Scripts::CStruct* params, Scripts::CScript* script); // 0x005ADA30
bool __cdecl CFunc_GetCurrentSkaterCamAnimName(Scripts::CStruct* params, Scripts::CScript* script); // 0x005ADB00
bool __cdecl CFunc_GetSkaterCamAnimParams(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005ADB50
// 0x005ADC10 / 0x005ADCC0 / 0x005ADD70: also CutsceneFinished / CutsceneHeld / KillCutscene.
bool __cdecl CFunc_SkaterCamAnimFinished(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005ADC10
bool __cdecl CFunc_SkaterCamAnimHeld(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005ADCC0
bool __cdecl CFunc_KillSkaterCamAnim(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005ADD70
bool __cdecl CFunc_ReloadSkaterCamAnim(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005ADEC0
bool __cdecl CFunc_CutsceneDestroyListActive(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005AE000
bool __cdecl CFunc_PlayCutscene(Scripts::CStruct* params, Scripts::CScript* script);                // 0x005AE010
bool __cdecl CFunc_HasMovieStarted(Scripts::CStruct* params, Scripts::CScript* script);             // 0x005AE090
bool __cdecl CFunc_IsMovieQueued(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005AE0A0
bool __cdecl CFunc_PauseCutscene(Scripts::CStruct* params, Scripts::CScript* script);               // 0x005AE0B0
bool __cdecl CFunc_Cutscene_FrameAdvance(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005AE120
bool __cdecl CFunc_PlayObjectAnim(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AE140
bool __cdecl CFunc_SetObjectAnimSkippable(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005AE190
bool __cdecl CFunc_SetObjectAnimShouldPause(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005AE200
bool __cdecl CFunc_ObjectAnimFinished(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005AE270
bool __cdecl CFunc_ObjectAnimHeld(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AE2B0
bool __cdecl CFunc_KillObjectAnim(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005AE2F0

} // namespace ScriptCommands
