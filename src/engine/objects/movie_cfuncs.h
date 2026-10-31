#pragma once

#include <cstddef>
#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) of the movies, 0x005AD830-0x005AE36F: skater camera animations and cutscenes (the
// skate module's movie manager +0x368), object animations (its second movie manager +0x36C: GameObjects::VideoHub,
// movie_manager.h), and the cutscene destroy list's entry. All the commands are cdecl (params, script) and return
// whether the script's `if` should be taken (true where nothing is asked).
namespace commands {

// An entry of the cutscene destroy list (AddToCutsceneDestroyList, 0x005B5320; 0xC bytes): the object's name, the
// script that destroys it (destroy_func) and its parameters (destroy_params, a structure of the entry's own).
struct CutsceneDestroyEntry {
    uint32_t label_;          // +0x00
    uint32_t destroy_script_; // +0x04
    script::Struct* args_;    // +0x08
    // 0x005ADF60: the constructor: no name or script, a new empty structure (null if out of memory). Returns this.
    CutsceneDestroyEntry* make_new();
};
static_assert(sizeof(CutsceneDestroyEntry) == 0xC && offsetof(CutsceneDestroyEntry, args_) == 8,
              "layout must match the original");

bool __cdecl cfunc_play_skater_cam_anim(script::Struct* params, script::Script* script); // 0x005AD830
// 0x005AD960 / 0x005ADA30: also SetCutsceneSkippable / SetCutsceneShouldPause (the same functions).
bool __cdecl cfunc_set_skater_cam_anim_skippable(script::Struct* params, script::Script* script);    // 0x005AD960
bool __cdecl cfunc_set_skater_cam_anim_should_pause(script::Struct* params, script::Script* script); // 0x005ADA30
bool __cdecl cfunc_get_current_skater_cam_anim_name(script::Struct* params, script::Script* script); // 0x005ADB00
bool __cdecl cfunc_get_skater_cam_anim_params(script::Struct* params, script::Script* script);       // 0x005ADB50
// 0x005ADC10 / 0x005ADCC0 / 0x005ADD70: also CutsceneFinished / CutsceneHeld / KillCutscene.
bool __cdecl cfunc_skater_cam_anim_finished(script::Struct* params, script::Script* script);     // 0x005ADC10
bool __cdecl cfunc_skater_cam_anim_held(script::Struct* params, script::Script* script);         // 0x005ADCC0
bool __cdecl cfunc_kill_skater_cam_anim(script::Struct* params, script::Script* script);         // 0x005ADD70
bool __cdecl cfunc_reload_skater_cam_anim(script::Struct* params, script::Script* script);       // 0x005ADEC0
bool __cdecl cfunc_cutscene_destroy_list_active(script::Struct* params, script::Script* script); // 0x005AE000
bool __cdecl cfunc_play_cutscene(script::Struct* params, script::Script* script);                // 0x005AE010
bool __cdecl cfunc_has_movie_started(script::Struct* params, script::Script* script);            // 0x005AE090
bool __cdecl cfunc_is_movie_queued(script::Struct* params, script::Script* script);              // 0x005AE0A0
bool __cdecl cfunc_pause_cutscene(script::Struct* params, script::Script* script);               // 0x005AE0B0
bool __cdecl cfunc_cutscene_frame_advance(script::Struct* params, script::Script* script);       // 0x005AE120
bool __cdecl cfunc_play_object_anim(script::Struct* params, script::Script* script);             // 0x005AE140
bool __cdecl cfunc_set_object_anim_skippable(script::Struct* params, script::Script* script);    // 0x005AE190
bool __cdecl cfunc_set_object_anim_should_pause(script::Struct* params, script::Script* script); // 0x005AE200
bool __cdecl cfunc_object_anim_finished(script::Struct* params, script::Script* script);         // 0x005AE270
bool __cdecl cfunc_object_anim_held(script::Struct* params, script::Script* script);             // 0x005AE2B0
bool __cdecl cfunc_kill_object_anim(script::Struct* params, script::Script* script);             // 0x005AE2F0

} // namespace commands
