#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// The script commands (CFunctions) of the music and streams, 0x0044D100-0x0044D7C7 and (between the sound effects'
// commands, sfx/sound_cfuncs.h) 0x0044D8F0-0x0044E399: thin wrappers reading their parameters and calling the Pcm
// functions (music_player.h, music_api.h, stream_commands.h). All are cdecl (params, script) and return whether the
// script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_stream_exists(script::Struct* params, script::Script* script);          // 0x0044D100
bool __cdecl cfunc_add_stream(script::Struct* params, script::Script* script);             // 0x0044D140
bool __cdecl cfunc_remove_stream(script::Struct* params, script::Script* script);          // 0x0044D1B0
bool __cdecl cfunc_stop_stream(script::Struct* params, script::Script* script);            // 0x0044D220
bool __cdecl cfunc_is_stream_playing(script::Struct* params, script::Script* script);       // 0x0044D260
bool __cdecl cfunc_play_track(script::Struct* params, script::Script* script);             // 0x0044D2A0
bool __cdecl cfunc_play_music_stream(script::Struct* params, script::Script* script);       // 0x0044D380
bool __cdecl cfunc_load_stream_frame_amp(script::Struct* params, script::Script* script);    // 0x0044D3E0
bool __cdecl cfunc_free_stream_frame_amp(script::Struct* params, script::Script* script);    // 0x0044D420
bool __cdecl cfunc_set_music_volume(script::Struct* params, script::Script* script);        // 0x0044D460
bool __cdecl cfunc_set_music_stream_volume(script::Struct* params, script::Script* script);  // 0x0044D490
bool __cdecl cfunc_stop_music(script::Struct* params, script::Script* script);             // 0x0044D4C0
bool __cdecl cfunc_pause_music(script::Struct* params, script::Script* script);            // 0x0044D4D0
bool __cdecl cfunc_pause_stream(script::Struct* params, script::Script* script);           // 0x0044D500
bool __cdecl cfunc_add_music_track(script::Struct* params, script::Script* script);         // 0x0044D530
bool __cdecl cfunc_change_track_state(script::Struct* params, script::Script* script);      // 0x0044D5B0
bool __cdecl cfunc_track_enabled(script::Struct* params, script::Script* script);          // 0x0044D5F0
bool __cdecl cfunc_clear_music_track_list(script::Struct* params, script::Script* script);   // 0x0044D690
bool __cdecl cfunc_skip_music_track(script::Struct* params, script::Script* script);        // 0x0044D6C0
bool __cdecl cfunc_set_music_mode(script::Struct* params, script::Script* script);          // 0x0044D6D0
bool __cdecl cfunc_set_random_mode(script::Struct* params, script::Script* script);         // 0x0044D710
bool __cdecl cfunc_set_music_looping(script::Struct* params, script::Script* script);       // 0x0044D740
bool __cdecl cfunc_pre_load_stream_done(script::Struct* params, script::Script* script);     // 0x0044D770
bool __cdecl cfunc_use_user_soundtrack(script::Struct* params, script::Script* script);     // 0x0044D790
bool __cdecl cfunc_use_standard_soundtrack(script::Struct* params, script::Script* script); // 0x0044D7C0
bool __cdecl cfunc_play_stream(script::Struct* params, script::Script* script);            // 0x0044D8F0
bool __cdecl cfunc_set_stream_params(script::Struct* params, script::Script* script);       // 0x0044DAA0
bool __cdecl cfunc_load_music_header(script::Struct* params, script::Script* script);       // 0x0044E120
bool __cdecl cfunc_load_stream_header(script::Struct* params, script::Script* script);      // 0x0044E180
bool __cdecl cfunc_get_current_track(script::Struct* params, script::Script* script);       // 0x0044E1E0
bool __cdecl cfunc_start_pre_loaded_stream(script::Struct* params, script::Script* script);  // 0x0044E2C0

} // namespace commands
