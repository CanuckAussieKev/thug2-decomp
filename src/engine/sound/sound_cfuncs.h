#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// The script commands (CFunctions) of the sound effects, 0x0044D7D0-0x0044E2BE: loading, playing, stopping and
// changing sounds through the sound effects manager (sound::AudioMixer at 0x006F85A8, sfx.h), its volume, pitch,
// dropoff and banks. All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

bool cfunc_load_sound(script::Struct* params, script::Context context);                  // 0x0044D7D0
bool cfunc_play_sound(script::Struct* params, script::Context context);                  // 0x0044DC10
bool cfunc_stop_sound(script::Struct* params, script::Context context);                  // 0x0044DD50
bool cfunc_stop_all_sounds(script::Struct* params, script::Context context);             // 0x0044DDA0
bool cfunc_set_sound_params(script::Struct* params, script::Context context);            // 0x0044DDB0
bool cfunc_is_sound_playing(script::Struct* params, script::Context context);            // 0x0044DED0
bool cfunc_add_sound_banks(script::Struct* params, script::Context context);             // 0x0044DF10
bool cfunc_clear_sound_bank(script::Struct* params, script::Context context);            // 0x0044DF70
bool cfunc_remove_all_sound_banks(script::Struct* params, script::Context context);      // 0x0044DFA0
bool cfunc_set_sfx_reverb(script::Struct* params, script::Context context);              // 0x0044DFB0
bool cfunc_set_sfx_dropoff(script::Struct* params, script::Context context);             // 0x0044E040
bool cfunc_set_sfx_volume(script::Struct* params, script::Context context);              // 0x0044E090
bool cfunc_set_global_pitch_modulation(script::Struct* params, script::Context context); // 0x0044E0C0
bool cfunc_get_value_from_volume(script::Struct* params, script::Context context);       // 0x0044E200
bool cfunc_set_positional_sound_silence_override_mode(script::Struct* params,
                                                      script::Context context); // 0x0044E290

} // namespace commands
