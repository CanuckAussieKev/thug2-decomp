// The script commands of the sound effects, 0x0044D7D0-0x0044E2BE (see sound_cfuncs.h).

#include "engine/sound/sound_cfuncs.h"

#include <bit>
#include <cstdint>

#include "engine/audio/music_player.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/core/checked_index.h"

namespace commands {

namespace {

using script::Array;
using script::Script;
using script::Struct;

constexpr uint32_t kPitch = 0xD8604126;                // "pitch"
constexpr uint32_t kDropoff = 0xFF2020EC;              // "dropoff"
constexpr uint32_t kVol = 0xF6A36814;                  // "vol"
constexpr uint32_t kVolL = 0x5E285A66;                 // "volL"
constexpr uint32_t kVolR = 0xA4276705;                 // "volR"
constexpr uint32_t kId = 0x40C698AF;                   // "id"
constexpr uint32_t kBank = 0x279F4085;                 // "bank"
constexpr uint32_t kLevelSound = 0x389129E4;           // (name unknown) ingest_sound: a level sound (load type 0)
constexpr uint32_t kPosUpdateWithDoppler = 0xCF15C120; // "PosUpdateWithDoppler"
constexpr uint32_t kNoReverb = 0x8B87176F;             // "NoReverb"
constexpr uint32_t kDebugSoundFx = 0xD7BB618D;         // "DebugSoundFX"
constexpr uint32_t kMode = 0x6835B854;                 // "mode"
constexpr uint32_t kInstant = 0xF16FB852;              // "instant"
constexpr uint32_t kPitchFlagA = 0x6A3BD54B;           // (name unknown) SetGlobalPitchModulation: its third argument
constexpr uint32_t kPitchFlagB = 0xC406CE6F;           // (name unknown) SetGlobalPitchModulation: its second argument
constexpr uint32_t kSfxVol = 0x8D6F6554;               // "sfxvol"
constexpr uint32_t kMusicVolume = 0xA5162BD2;          // (name unknown) GetValueFromVolume: the music volume
constexpr uint32_t kSfxVolume = 0xE32F3525;            // (name unknown) GetValueFromVolume: the sound effects volume
constexpr uint32_t kValue = 0xE288A7CB;                // "value"

constexpr float kTwelveInchScale = 12.0f; // (0x0064546C)

// The sound effects manager (0x006F85A8).
sound::AudioMixer* audio_mixer_handle() { return sound::g_audio_mixer.get(); }

// A stereo volume of the manager's default type: left and right.
void set_stereo(sound::VolumeRef& volume, float left, float right)
{
    new (&volume) sound::VolumeRef;
    volume.change_muted();
    core::at(volume.lanes_, 0) = left;
    core::at(volume.lanes_, 1) = right;
}

} // namespace

// 0x0044D7D0: ingest_sound <file> [pitch=100] [dropoff=feet] [vol=100] [0x389129E4 | bank=n] [PosUpdateWithDoppler |
// NoReverb]: the sound loaded - into the level's table with the flag 0x389129E4 (load type 0), as a bank's sound
// (2) with bank=, else permanently (1); the flags 1 / 2. Always true (nothing without a file).
bool cfunc_load_sound(Struct* params, Script*)
{
    const char* file = nullptr;
    if (!params->fetch_text(0u, &file, 1))
        return true;
    float pitch = 100.0f;
    float volume = 100.0f;
    float dropoff = 0.0f;
    int bank = 0;
    int load_type = 1;
    params->fetch_float(kPitch, &pitch, 0);
    if (params->fetch_float(kDropoff, &dropoff, 0))
        dropoff *= kTwelveInchScale;
    params->fetch_float(kVol, &volume, 0);
    if (params->has_bare_flag(kLevelSound))
        load_type = 0;
    else if (params->fetch_int_value(kBank, &bank, 0))
        load_type = 2;
    uint16_t flags = 0;
    if (params->has_bare_flag(kPosUpdateWithDoppler))
        flags = 1;
    else if (params->has_bare_flag(kNoReverb))
        flags = 2;
    audio_mixer_handle()->ingest_sound(file, flags, load_type, bank, dropoff, pitch, volume);
    return true;
}

// 0x0044DC10: emit_sound <sound> [volL= volR= | vol= (100)] [pitch=100] [id=<unique id>]: played (stereo). Always
// true.
bool cfunc_play_sound(Struct* params, Script*)
{
    uint32_t sound = 0;
    uint32_t id = 0;
    float vol = 100.0f;
    float left = 100.0f;
    float right = 100.0f;
    float pitch = 100.0f;
    params->read_hash_value(0u, &sound, 0);
    params->fetch_float(kVolL, &left, 0);
    params->fetch_float(kVolR, &right, 0);
    if (params->fetch_float(kVol, &vol, 0)) {
        left = vol;
        right = vol;
    }
    params->fetch_float(kPitch, &pitch, 0);
    params->read_hash_value(kId, &id, 0);
    sound::VolumeRef volume;
    set_stereo(volume, left, right);
    audio_mixer_handle()->emit_sound(sound, &volume, pitch, id, nullptr, 0);
    return true;
}

// 0x0044DD50: StopSound <id or name>: whether it was playing (and is now stopped). False without one.
bool cfunc_stop_sound(Struct* params, Script*)
{
    uint32_t sound = 0; // (uninitialised in the original: only read when found)
    if (!params->read_hash_value(0u, &sound, 0))
        return false;
    const bool stopped = audio_mixer_handle()->stop_sound(sound);
    if (!stopped)
        script::fetch_int_value(kDebugSoundFx); // (a compiled-out report: the global is read, not used)
    return stopped;
}

// 0x0044DDA0: StopAllSounds. Always true.
bool cfunc_stop_all_sounds(Struct*, Script*)
{
    audio_mixer_handle()->stop_all_sounds();
    return true;
}

// 0x0044DDB0: SetSoundParams <id or name> [volL= volR= | vol=] [pitch=100]: the playing sound's volume (default
// 100 each) and pitch. Always true (nothing without a sound).
bool cfunc_set_sound_params(Struct* params, Script*)
{
    uint32_t sound = 0;
    if (!params->read_hash_value(0u, &sound, 0))
        return true;
    float left = 100.0f;
    float right = 100.0f;
    float pitch = 100.0f;
    float vol = 0.0f; // (uninitialised in the original: only read when found)
    params->fetch_float(kVolL, &left, 0);
    params->fetch_float(kVolR, &right, 0);
    if (params->fetch_float(kVol, &vol, 0)) {
        left = vol;
        right = vol;
    }
    params->fetch_float(kPitch, &pitch, 0);
    sound::VolumeRef volume;
    set_stereo(volume, left, right);
    audio_mixer_handle()->set_sound_params(sound, &volume, pitch);
    return true;
}

// 0x0044DED0: IsSoundPlaying <unique id>. False without one.
bool cfunc_is_sound_playing(Struct* params, Script*)
{
    uint32_t sound = 0;
    if (!params->read_hash_value(0u, &sound, 0))
        return false;
    return audio_mixer_handle()->is_sound_playing(sound, nullptr);
}

// 0x0044DF10: AddSoundBanks [<bank numbers>]: each bank's group (number << 10) given to the manager's stop_group
// (empty in this build). Always true.
bool cfunc_add_sound_banks(Struct* params, Script*)
{
    Array* banks = nullptr; // (uninitialised in the original: only read when found)
    if (params->fetch_list(0u, &banks, 0)) {
        sound::AudioMixer* manager = audio_mixer_handle();
        for (uint32_t i = 0; i < banks->extent_; ++i)
            manager->stop_group(static_cast<int>(banks->fetch_part(i) << 10));
    }
    return true;
}

// 0x0044DF70: ClearSoundBank <group>: the group's permanent sounds unloaded. Always true.
bool cfunc_clear_sound_bank(Struct* params, Script*)
{
    int group = 0; // (uninitialised in the original: only read when found)
    if (params->fetch_int_value(0u, &group, 0))
        audio_mixer_handle()->unload_group(group);
    return true;
}

// 0x0044DFA0: RemoveAllSoundBanks. Always true.
bool cfunc_remove_all_sound_banks(Struct*, Script*)
{
    audio_mixer_handle()->unload_all_groups();
    return true;
}

// 0x0044DFB0: SetSfxReverb [level (0; over 100: 100)] [mode=n] [instant]: the reverb - compiled out of the PC
// build (0x0047B6B0 does nothing; its first argument is the level's float bits). Always true.
bool cfunc_set_sfx_reverb(Struct* params, Script*)
{
    float level = 0.0f;
    int mode = 0;
    params->fetch_float(0u, &level, 0);
    if (level > 100.0f) // (0x006463D4; a NaN is kept)
        level = 100.0f;
    params->fetch_int_value(kMode, &mode, 0);
    const bool instant = params->has_bare_flag(kInstant);
    audio_mixer_handle()->print_sounds(std::bit_cast<int>(level), mode, instant);
    return true;
}

// 0x0044E040: SetSfxDropoff [feet (1020)]: the default dropoff distance. False (and nothing set) for 0 or less.
bool cfunc_set_sfx_dropoff(Struct* params, Script*)
{
    float dropoff = 1020.0f;
    params->fetch_float(0u, &dropoff, 0);
    if (dropoff <= 0.0f) // (0x0066D520; a NaN goes on)
        return false;
    audio_mixer_handle()->set_default_dropoff(dropoff * kTwelveInchScale);
    return true;
}

// 0x0044E090: SetSfxVolume [percent (100)]: the main volume. Always true.
bool cfunc_set_sfx_volume(Struct* params, Script*)
{
    float volume = 100.0f;
    params->fetch_float(0u, &volume, 0);
    audio_mixer_handle()->change_master_loudness(volume);
    return true;
}

// 0x0044E0C0: SetGlobalPitchModulation [n (1)] [0xC406CE6F] [0x6A3BD54B]: the sound effects' pitch. Always true.
bool cfunc_set_global_pitch_modulation(Struct* params, Script*)
{
    float pitch = 1.0f;
    params->fetch_float(0u, &pitch, 0);
    sound::AudioMixer* manager = audio_mixer_handle();
    const bool a = params->has_bare_flag(kPitchFlagA);
    const bool b = params->has_bare_flag(kPitchFlagB);
    manager->set_pitch(pitch, b, a);
    return true;
}

// 0x0044E200: GetValueFromVolume sfxvol | 0xA5162BD2 (the music's) | 0xE32F3525 (the sound effects'): the volume
// (percent) / 10, truncated, as the script's value. Always true (nothing for another name).
bool cfunc_get_value_from_volume(Struct* params, Script* script)
{
    uint32_t which = 0; // (uninitialised in the original without a name)
    params->read_hash_value(0u, &which, 1);
    float volume = 0.0f;
    switch (which) {
    case kSfxVol:
        volume = sound::fetch_master_loudness(); // (called on the manager in the original; it ignores it)
        break;
    case kMusicVolume:
        volume = audio::get_music_volume();
        break;
    case kSfxVolume:
        volume = audio::get_sfx_volume();
        break;
    default:
        return true;
    }
    script->args_->add_component_of_type(kValue, script::kQbvalInteger,
                                         static_cast<uint32_t>(static_cast<int>(volume) / 10));
    return true;
}

// 0x0044E290: SetPositionalSoundSilenceOverrideMode [n (1)]: positional sounds keep playing while paused (the
// manager's +0) for a non-zero n. Always true.
bool cfunc_set_positional_sound_silence_override_mode(Struct* params, Script*)
{
    int mode = 1;
    params->fetch_int_value(0u, &mode, 1);
    audio_mixer_handle()->mute_override_ = mode != 0;
    return true;
}

} // namespace commands
