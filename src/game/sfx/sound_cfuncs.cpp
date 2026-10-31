// The script commands of the sound effects, 0x0044D7D0-0x0044E2BE (see sound_cfuncs.h).

#include "game/sfx/sound_cfuncs.h"

#include <bit>
#include <cstdint>

#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"

namespace CFuncs {

namespace {

using Script::CArray;
using Script::CScript;
using Script::CStruct;

constexpr uint32_t kPitch = 0xD8604126;         // "pitch"
constexpr uint32_t kDropoff = 0xFF2020EC;       // "dropoff"
constexpr uint32_t kVol = 0xF6A36814;           // "vol"
constexpr uint32_t kVolL = 0x5E285A66;          // "volL"
constexpr uint32_t kVolR = 0xA4276705;          // "volR"
constexpr uint32_t kId = 0x40C698AF;            // "id"
constexpr uint32_t kBank = 0x279F4085;          // "bank"
constexpr uint32_t kLevelSound = 0x389129E4;    // (name unknown) LoadSound: a level sound (load type 0)
constexpr uint32_t kPosUpdateWithDoppler = 0xCF15C120; // "PosUpdateWithDoppler"
constexpr uint32_t kNoReverb = 0x8B87176F;      // "NoReverb"
constexpr uint32_t kDebugSoundFx = 0xD7BB618D;  // "DebugSoundFX"
constexpr uint32_t kMode = 0x6835B854;          // "mode"
constexpr uint32_t kInstant = 0xF16FB852;       // "instant"
constexpr uint32_t kPitchFlagA = 0x6A3BD54B;    // (name unknown) SetGlobalPitchModulation: its third argument
constexpr uint32_t kPitchFlagB = 0xC406CE6F;    // (name unknown) SetGlobalPitchModulation: its second argument
constexpr uint32_t kSfxVol = 0x8D6F6554;        // "sfxvol"
constexpr uint32_t kMusicVolume = 0xA5162BD2;   // (name unknown) GetValueFromVolume: the music volume
constexpr uint32_t kSfxVolume = 0xE32F3525;     // (name unknown) GetValueFromVolume: the sound effects volume
constexpr uint32_t kValue = 0xE288A7CB;         // "value"

constexpr float kFeetToInches = 12.0f; // (0x0064546C)

// The sound effects manager (0x006F85A8).
Sfx::CSfxManager* SfxManager() { return Sfx::g_sfxManager; }

// A stereo volume of the manager's default type: left and right.
void SetStereo(Sfx::sVolume& volume, float left, float right)
{
    volume.Init();
    volume.SetSilent();
    volume.m_channels[0] = left;
    volume.m_channels[1] = right;
}

} // namespace

// 0x0044D7D0: LoadSound <file> [pitch=100] [dropoff=feet] [vol=100] [0x389129E4 | bank=n] [PosUpdateWithDoppler |
// NoReverb]: the sound loaded - into the level's table with the flag 0x389129E4 (load type 0), as a bank's sound
// (2) with bank=, else permanently (1); the flags 1 / 2. Always true (nothing without a file).
bool __cdecl CFunc_LoadSound(CStruct* params, CScript*)
{
    const char* file;
    if (!params->GetString(0u, &file, 1))
        return true;
    float pitch = 100.0f;
    float volume = 100.0f;
    float dropoff = 0.0f;
    int bank = 0;
    int loadType = 1;
    params->GetFloat(kPitch, &pitch, 0);
    if (params->GetFloat(kDropoff, &dropoff, 0))
        dropoff *= kFeetToInches;
    params->GetFloat(kVol, &volume, 0);
    if (params->ContainsFlag(kLevelSound))
        loadType = 0;
    else if (params->GetInteger(kBank, &bank, 0))
        loadType = 2;
    uint16_t flags = 0;
    if (params->ContainsFlag(kPosUpdateWithDoppler))
        flags = 1;
    else if (params->ContainsFlag(kNoReverb))
        flags = 2;
    SfxManager()->LoadSound(file, flags, loadType, bank, dropoff, pitch, volume);
    return true;
}

// 0x0044DC10: PlaySound <sound> [volL= volR= | vol= (100)] [pitch=100] [id=<unique id>]: played (stereo). Always
// true.
bool __cdecl CFunc_PlaySound(CStruct* params, CScript*)
{
    uint32_t sound = 0;
    uint32_t id = 0;
    float vol = 100.0f;
    float left = 100.0f;
    float right = 100.0f;
    float pitch = 100.0f;
    params->GetChecksum(0u, &sound, 0);
    params->GetFloat(kVolL, &left, 0);
    params->GetFloat(kVolR, &right, 0);
    if (params->GetFloat(kVol, &vol, 0)) {
        left = vol;
        right = vol;
    }
    params->GetFloat(kPitch, &pitch, 0);
    params->GetChecksum(kId, &id, 0);
    Sfx::sVolume volume;
    SetStereo(volume, left, right);
    SfxManager()->PlaySound(sound, &volume, pitch, id, nullptr, 0);
    return true;
}

// 0x0044DD50: StopSound <id or name>: whether it was playing (and is now stopped). False without one.
bool __cdecl CFunc_StopSound(CStruct* params, CScript*)
{
    uint32_t sound = 0; // (uninitialised in the original: only read when found)
    if (!params->GetChecksum(0u, &sound, 0))
        return false;
    bool stopped = SfxManager()->StopSound(sound);
    if (!stopped)
        Script::GetInteger(kDebugSoundFx); // (a compiled-out report: the global is read, not used)
    return stopped;
}

// 0x0044DDA0: StopAllSounds. Always true.
bool __cdecl CFunc_StopAllSounds(CStruct*, CScript*)
{
    SfxManager()->StopAllSounds();
    return true;
}

// 0x0044DDB0: SetSoundParams <id or name> [volL= volR= | vol=] [pitch=100]: the playing sound's volume (default
// 100 each) and pitch. Always true (nothing without a sound).
bool __cdecl CFunc_SetSoundParams(CStruct* params, CScript*)
{
    uint32_t sound = 0;
    if (!params->GetChecksum(0u, &sound, 0))
        return true;
    float left = 100.0f;
    float right = 100.0f;
    float pitch = 100.0f;
    float vol = 0.0f; // (uninitialised in the original: only read when found)
    params->GetFloat(kVolL, &left, 0);
    params->GetFloat(kVolR, &right, 0);
    if (params->GetFloat(kVol, &vol, 0)) {
        left = vol;
        right = vol;
    }
    params->GetFloat(kPitch, &pitch, 0);
    Sfx::sVolume volume;
    SetStereo(volume, left, right);
    SfxManager()->SetSoundParams(sound, &volume, pitch);
    return true;
}

// 0x0044DED0: IsSoundPlaying <unique id>. False without one.
bool __cdecl CFunc_IsSoundPlaying(CStruct* params, CScript*)
{
    uint32_t sound = 0;
    if (!params->GetChecksum(0u, &sound, 0))
        return false;
    return SfxManager()->IsSoundPlaying(sound, nullptr);
}

// 0x0044DF10: AddSoundBanks [<bank numbers>]: each bank's group (number << 10) given to the manager's StopGroup
// (empty in this build). Always true.
bool __cdecl CFunc_AddSoundBanks(CStruct* params, CScript*)
{
    CArray* banks = nullptr; // (uninitialised in the original: only read when found)
    if (params->GetArray(0u, &banks, 0)) {
        Sfx::CSfxManager* manager = SfxManager();
        for (uint32_t i = 0; i < banks->m_size; ++i)
            manager->StopGroup(static_cast<int>(banks->GetElement(i) << 10));
    }
    return true;
}

// 0x0044DF70: ClearSoundBank <group>: the group's permanent sounds unloaded. Always true.
bool __cdecl CFunc_ClearSoundBank(CStruct* params, CScript*)
{
    int group = 0; // (uninitialised in the original: only read when found)
    if (params->GetInteger(0u, &group, 0))
        SfxManager()->UnloadGroup(group);
    return true;
}

// 0x0044DFA0: RemoveAllSoundBanks. Always true.
bool __cdecl CFunc_RemoveAllSoundBanks(CStruct*, CScript*)
{
    SfxManager()->UnloadAllGroups();
    return true;
}

// 0x0044DFB0: SetSfxReverb [level (0; over 100: 100)] [mode=n] [instant]: the reverb - compiled out of the PC
// build (0x0047B6B0 does nothing; its first argument is the level's float bits). Always true.
bool __cdecl CFunc_SetSfxReverb(CStruct* params, CScript*)
{
    float level = 0.0f;
    int mode = 0;
    params->GetFloat(0u, &level, 0);
    if (level > 100.0f) // (0x006463D4; a NaN is kept)
        level = 100.0f;
    params->GetInteger(kMode, &mode, 0);
    bool instant = params->ContainsFlag(kInstant);
    SfxManager()->PrintSounds(std::bit_cast<int>(level), mode, instant);
    return true;
}

// 0x0044E040: SetSfxDropoff [feet (1020)]: the default dropoff distance. False (and nothing set) for 0 or less.
bool __cdecl CFunc_SetSfxDropoff(CStruct* params, CScript*)
{
    float dropoff = 1020.0f;
    params->GetFloat(0u, &dropoff, 0);
    if (dropoff <= 0.0f) // (0x0066D520; a NaN goes on)
        return false;
    SfxManager()->SetDefaultDropoff(dropoff * kFeetToInches);
    return true;
}

// 0x0044E090: SetSfxVolume [percent (100)]: the main volume. Always true.
bool __cdecl CFunc_SetSfxVolume(CStruct* params, CScript*)
{
    float volume = 100.0f;
    params->GetFloat(0u, &volume, 0);
    SfxManager()->SetMainVolume(volume);
    return true;
}

// 0x0044E0C0: SetGlobalPitchModulation [n (1)] [0xC406CE6F] [0x6A3BD54B]: the sound effects' pitch. Always true.
bool __cdecl CFunc_SetGlobalPitchModulation(CStruct* params, CScript*)
{
    float pitch = 1.0f;
    params->GetFloat(0u, &pitch, 0);
    Sfx::CSfxManager* manager = SfxManager();
    bool a = params->ContainsFlag(kPitchFlagA);
    bool b = params->ContainsFlag(kPitchFlagB);
    manager->SetPitch(pitch, b, a);
    return true;
}

// 0x0044E200: GetValueFromVolume sfxvol | 0xA5162BD2 (the music's) | 0xE32F3525 (the sound effects'): the volume
// (percent) / 10, truncated, as the script's value. Always true (nothing for another name).
bool __cdecl CFunc_GetValueFromVolume(CStruct* params, CScript* script)
{
    uint32_t which = 0; // (uninitialised in the original without a name)
    params->GetChecksum(0u, &which, 1);
    float volume;
    switch (which) {
    case kSfxVol:
        volume = Sfx::GetMainVolume(); // (called on the manager in the original; it ignores it)
        break;
    case kMusicVolume:
        volume = Pcm::GetMusicVolume();
        break;
    case kSfxVolume:
        volume = Pcm::GetSfxVolume();
        break;
    default:
        return true;
    }
    script->m_params->AddComponentOfType(kValue, Script::ESYMBOLTYPE_INTEGER,
                                         static_cast<uint32_t>(static_cast<int>(volume) / 10));
    return true;
}

// 0x0044E290: SetPositionalSoundSilenceOverrideMode [n (1)]: positional sounds keep playing while paused (the
// manager's +0) for a non-zero n. Always true.
bool __cdecl CFunc_SetPositionalSoundSilenceOverrideMode(CStruct* params, CScript*)
{
    int mode = 1;
    params->GetInteger(0u, &mode, 1);
    SfxManager()->m_muteOverride = mode != 0;
    return true;
}

} // namespace CFuncs
