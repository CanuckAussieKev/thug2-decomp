#pragma once

// The sound effects manager (THUG's Sfx::CSfxManager over the PC DirectSound driver; 0x0047AC10-0x0047D41E): the
// wave tables (sounds loaded from data\sounds\*.snd: permanent ones 0x006F4268 and level ones 0x006F5E68), the
// sounds playing (0x006F7A68: 48 slots, each on one of the driver's voices 0x006F85C8), the positional sounds
// that follow objects (a list of 128 entries from 0x006F3668), volumes (sVolume: up to 5 channels) and the
// listener.
//
// Nothing plays when there's no DirectSound device (0x007D6A38) or on the platforms without sound (0x007CEFA8 = 2
// or 6): most functions check that first ("NoSound()").
//
// NOT ORIGINAL: thug2-decomp.ini [features] mute_audio=1 silences everything (effects, music, streams, movies) from
// the first frame - the audio session and the volume paths, game/sfx/mute_audio.h; the workspaces' test games run so.

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "hook/game_global.h"

namespace Obj {
class CObject;
}

namespace Sfx {

// A volume: a level per channel (percent; 2 channels, or 5 for the surround type) and its type; a 3D one also
// has the sound's position and range.
struct sVolume {
    enum : int {
        TYPE_STEREO = 0,
        TYPE_POSITIONAL = 1,
        TYPE_SURROUND = 2,   // 5 channels
        TYPE_3D = 3,         // the voice's 3D buffer places it (never silent)
    };

    // 0x0047B030 (returns this): the manager's default type, every channel 0.
    sVolume* Init();
    // 0x0047B070 (returns this)
    sVolume* SetType(int type);
    // 0x0047AC50: every channel scaled by `percent` percent.
    void PercentageAdjustment(float percent);
    // 0x0047ACF0: whether every channel is 0 (and it isn't 3D).
    bool IsSilent();
    // 0x0047AD60: every channel 0.
    void SetSilent();
    // 0x0047B080: the channel with the greatest magnitude (its signed value; the first of equals).
    float GetLoudestChannel();

    int ChannelCount() const { return m_type == TYPE_SURROUND ? 5 : 2; }

    float m_channels[5];      // +0x00
    int m_type;               // +0x14
    Mth::Vector m_position;   // +0x18: 3D: where the sound is
    float m_dropoff;          // +0x28: 3D: its range
};
static_assert(sizeof(sVolume) == 0x2C, "layout must match the original");

// A loaded sound: its WAVEFORMATEX, the DirectSound buffer description, its samples (0x44 bytes).
struct PCWave {
    // 0x0047C870: the .wav file's format and samples (converted, 0x005F5A20), its loops (a smpl chunk with some);
    // the buffer description with `descFlags`. False if the file can't be opened. (The sample buffer is the
    // format's bytes-per-second long: one second, whatever the data chunk's size.)
    bool Load(const char* fileName, uint32_t descFlags);

    uint16_t formatTag;        // +0x00 (WAVEFORMATEX)
    uint16_t channels;         // +0x02
    uint32_t samplesPerSecond; // +0x04
    uint32_t bytesPerSecond;   // +0x08
    uint16_t blockAlign;       // +0x0C
    uint16_t bitsPerSample;    // +0x0E
    uint16_t extraSize;        // +0x10
    uint16_t unknown12;        // +0x12 (0x40 once loaded)
    uint32_t descSize;         // +0x14 (DSBUFFERDESC: 0x24)
    uint32_t descFlags;        // +0x18
    uint32_t descBytes;        // +0x1C
    uint32_t descReserved;     // +0x20
    PCWave* descFormat;        // +0x24: this (the WAVEFORMATEX at its start)
    uint32_t desc3DAlgorithm[4]; // +0x28
    uint32_t dataSize;         // +0x38
    uint8_t* data;             // +0x3C
    bool looping;              // +0x40: the file's smpl chunk has loops
};
static_assert(sizeof(PCWave) == 0x44 && offsetof(PCWave, descSize) == 0x14 && offsetof(PCWave, data) == 0x3C,
              "layout must match the original");

// A wave table entry (0x1C bytes).
struct WaveTableEntry {
    uint32_t checksum;    // +0x00: of the file's name (without folders)
    uint16_t flags;       // +0x04
    uint8_t loadType;     // +0x06: 0 = level (the temporary table), else permanent (2: ... by group)
    uint8_t group;        // +0x07
    float pitch;          // +0x08 (percent)
    float volume;         // +0x0C (percent)
    float dropoff;        // +0x10 (0: the default 0x0067DA34)
    PCWave* wave;         // +0x14
    bool looping;         // +0x18
    bool notStreamed;     // +0x19
    uint8_t unknown1A[2];
};
static_assert(sizeof(WaveTableEntry) == 0x1C, "layout must match the original");

// How a sound plays (0x2C bytes): from the play parameters {pitch, volume, dropoff, falloff} and the wave's
// defaults.
struct PlaybackProps {
    uint32_t checksum;    // +0x00
    float dropoff;        // +0x04
    float pitch;          // +0x08 (percent)
    float volume;         // +0x0C (percent)
    float pitch2;         // +0x10
    float volume2;        // +0x14
    float pitch3;         // +0x18
    float volume3;        // +0x1C
    uint32_t unknown20;   // +0x20
    uint32_t unknown24;   // +0x24
    uint32_t falloff;     // +0x28
};
static_assert(sizeof(PlaybackProps) == 0x2C, "layout must match the original");

// A sound playing (0x3C bytes; 48 of them).
struct PlayingSound {
    uint32_t uniqueId;        // +0x00
    uint32_t checksum;        // +0x04
    WaveTableEntry* entry;    // +0x08
    PlaybackProps props;      // +0x0C
    uint32_t unknown38;       // +0x38: the next time (Tmr::GetTime) a distant positional sound is checked
};
static_assert(sizeof(PlayingSound) == 0x3C, "layout must match the original");

// A positional sound following an object (0x18 bytes; 128 of them).
struct PositionalSound {
    uint32_t uniqueId;        // +0x00
    uint32_t checksum;        // +0x04
    uint32_t inUse;           // +0x08 (bit 0)
    Obj::CObject* object;     // +0x0C
    PositionalSound* next;    // +0x10
    PositionalSound* prev;    // +0x14
};

// One of the driver's voices (0x18 bytes; up to 48).
struct Voice {
    void* buffer;             // +0x00: IDirectSoundBuffer
    void* buffer3D;           // +0x04: IDirectSound3DBuffer (3D hardware only)
    int baseFrequency;        // +0x08
    int frequency;            // +0x0C: the last set
    bool playing;             // +0x10
    uint8_t unknown11[3];
    PCWave* wave;             // +0x14
};

class CSfxManager {
public:
    // 0x0047B160 (returns this): the voices set up, nothing playing.
    CSfxManager* Construct();
    // 0x0047AD80: the slot of the playing sound `id` - by unique id, else by name (-1 if neither is playing).
    int GetPlayingIndex(uint32_t id);
    // 0x0047AE20: a positional sound for `object` (nothing at 128).
    void AddPositionalSound(uint32_t uniqueId, uint32_t checksum, Obj::CObject* object);
    // 0x0047AE90: (a group's sounds stopped - empty in this build).
    void StopGroup(int group);
    // 0x0047AEA0: the permanent waves loaded for `group` (load type 2) dropped from the table.
    void UnloadGroup(int group);
    // 0x0047AF10: UnloadGroup for every group (there are none in this build).
    void UnloadAllGroups();
    // 0x0047AF40: `props` from the wave's defaults and the play parameters `params` (null: defaults).
    void InitPlaybackProps(PlaybackProps* props, WaveTableEntry* entry, PlaybackProps* params);
    // 0x0047B340: whether sounds are paused (the game's two pause flags) - unless +0 is set.
    bool IsPaused();
    // 0x0047B360: `path` loaded into the level (`loadType` 0) or the permanent table; false if it's loaded
    // already, the table is full (256) or the file can't be read.
    bool LoadSound(const char* path, uint16_t flags, int loadType, int group, float dropoff, float pitch,
                   float volume);
    // 0x0047B480: the wave's volume (and the game's 50 %) applied to `volume`, its pitch to `pitch`.
    void AdjustVolumeAndPitch(sVolume* volume, float* pitch, WaveTableEntry* entry);
    // 0x0047B4E0: the sound `id` stopped (false if it isn't playing).
    bool StopSound(uint32_t id);
    // 0x0047B520: the volume and pitch of the playing sound `id`.
    bool SetSoundParams(uint32_t id, sVolume* volume, float pitch);
    // 0x0047B5D0: the main volume (0x0067DA38; below 0.1 % silent) for the voices and the user soundtrack.
    void SetMainVolume(float volume);
    // 0x0047B650: the sfx pitch (0x0067DA3C; 1 % or less: 0.01).
    void SetPitch(float pitch, int unused1, int unused2);
    // 0x0047B6B0: (debug output, compiled out).
    void PrintSounds(int a, int b, int c);
    // 0x0047B6F0: whether the sound `uniqueId` is playing (its slot in `index`, if given).
    bool IsSoundPlaying(uint32_t uniqueId, int* index);
    // 0x0047B760: the sound's dropoff distance (the default if it has none or isn't loaded).
    float GetDropoffDistance(uint32_t checksum);
    // 0x0047B7C0: SetSoundParams by unique id with the sound's own volume and pitch percentages.
    bool SetSoundParamsScaled(uint32_t uniqueId, sVolume* volume, float pitch);
    // 0x0047B1B0: everything dropped (the positional list, the permanent waves); the default dropoff back to 1020.
    void CleanUp();
    // 0x0047B210: the default dropoff distance.
    void SetDefaultDropoff(float dropoff);
    // 0x0047B240: every voice stopped, the positional list emptied.
    void StopAllSounds();
    // 0x0047B2A0: every playing voice set silent (0x0047D390).
    void UpdateVoiceVolumes();
    // 0x0047B2C0: the wave table entry for `checksum` (level ones first).
    WaveTableEntry* GetWaveTableEntry(uint32_t checksum);

    // --- 0x0047B8E0-0x0047C6BB ---
    // 0x0047B8E0: `pitch` shifted by the sound's motion relative to the camera (Doppler): `position` now,
    // `previous` a frame ago, over `frameSeconds` (nothing when it moved more than 360 units a 60th).
    void DopplerShift(float* pitch, const Mth::Vector* position, const Mth::Vector* previous, float frameSeconds,
                      void* camera);
    // 0x0047BA00: the props of the positional sound of `object` with that unique id (or, `byChecksum`, name), if
    // it's playing.
    PlaybackProps* GetPositionalProps(Obj::CObject* object, uint32_t id, bool byChecksum);
    // 0x0047BAB0: whether `id` isn't the unique id of a playing or positional sound.
    bool IsUniqueIdFree(uint32_t id);
    // 0x0047BB20: `id`, or the next free one after it.
    uint32_t GetUniqueId(uint32_t id);
    // 0x0047BB50 / 0x0047BC30: the positional sounds of `object` (with that name, unless 0) stopped and dropped.
    void StopObjectSounds(Obj::CObject* object);
    void StopObjectSound(Obj::CObject* object, uint32_t checksum);
    // 0x0047BD20: the sound `checksum` played at `volume` / `pitch` (the unique id `uniqueId`, else the name;
    // made free); `params` its play parameters. Returns the unique id (0: not played).
    uint32_t PlaySound(uint32_t checksum, sVolume* volume, float pitch, uint32_t uniqueId, void* params, int unused);
    // 0x0047BE10: `volume` for a sound at `position` heard from `camera` (the nearest if null): a level by the
    // distance over `dropoff` (falloff 0 smooth, 1 linear, 2 squared, 3 root), 3D. False when it's out of range
    // (5 % past the dropoff); true otherwise, or when paused (then silent).
    bool GetVolume(sVolume* volume, const Mth::Vector* position, float dropoff, int falloff, void* camera,
                   void* extent);
    // 0x0047C140: the sound played on `object` (positional unless `notPositional`); a silent looping one is kept
    // by the object to start later (unless `force`, which plays it anyway). Returns the unique id.
    uint32_t PlayObjectSound(uint32_t checksum, void* params, Obj::CObject* object, bool notPositional, bool force);
    // 0x0047C360: a positional sound's volume and pitch from its object's place; false when it should stop.
    bool UpdatePositionalSound(Obj::CObject* object, PlayingSound* sound, uint32_t time);
    // 0x0047C560: every positional sound updated; those that ended or stop are dropped.
    void UpdatePositionalSounds();

    uint8_t m_muteOverride;   // +0x00
    uint8_t m_unknown1[3];
    int m_defaultVolumeType;  // +0x04
};

// 0x0047AC10: the falloff function named by `checksum` (0: linear and others).
int FalloffFromChecksum(uint32_t checksum);
// 0x0047AFD0: the main volume.
float GetMainVolume();
// 0x0047AFE0: how many of the 48 voices are playing.
int CountPlayingVoices();
// 0x0047B010: whether there's no sound (no device, or a platform without sound).
bool NoSound();
// 0x0047BA60: the manager made when `create` and there's none; a reference taken.
void CreateManager(bool create);
// 0x0047C6C0: a reference dropped; the last one deletes the manager (everything dropped first).
void ReleaseManager();

// --- the voices (0x0047C720-0x0047D41E) ---
// 0x0047C720: a voice for `wave`: one holding it already, else the last free one after the last used (or the one
// after the last used).
int GetFreeVoice(PCWave* wave);
// 0x0047C790: the number of sound groups (none: -1).
int GroupCount();
// 0x0047C7A0
void StopVoice(int voice);
// 0x0047C7C0 / 0x0047C7D0: the voices' volume (percent) and pitch.
void SetVoiceVolume(float volume);
void SetVoicePitch(float pitch);
// 0x0047C7E0: whether the voice is playing (1 / 0).
int IsVoicePlaying(int voice);
// 0x0047C810 (and 0x0047B000, a jump to it): voices whose buffers stopped marked so.
void UpdateVoiceStates();
// 0x0047CA50: the voices emptied; their number and the 3D hardware from the device's capabilities; with 3D, the
// listener (0x006F85C0).
void InitVoices(CSfxManager* manager);
// 0x0047CBD0: the music, the streams and every voice stopped.
void StopAllVoices();
// 0x0047CC10: StopAllVoices; the permanent waves freed.
void FreePermanentWaves();
// 0x0047CC70: data\sounds\`path`.snd loaded into a new PCWave for the entry whose `wave` is `slot`.
bool LoadWave(const char* path, uint32_t checksum, PCWave** slot, int loadType, int unused);
// 0x0047CD70: the voice's volume, pan and frequency (3D: the buffer's position, the listener's orientation).
void SetVoiceParameters(int voice, sVolume* volume, float pitch);
// 0x0047D130: the wave (the entry's `wave` at `slot`) played on a free voice; the voice, or -1.
int PlayVoice(PCWave** slot, sVolume* volume, float pitch);
// 0x0047D390: every playing voice set silent.
void SilenceVoices();

GAME_GLOBAL_EXTERN(float, g_defaultDropoff);
GAME_GLOBAL_EXTERN(WaveTableEntry[256], g_permanentWaves);
GAME_GLOBAL_EXTERN(WaveTableEntry[256], g_levelWaves);
GAME_GLOBAL_EXTERN(PlayingSound[48], g_playingSounds);
GAME_GLOBAL_EXTERN(CSfxManager*, g_sfxManager);
GAME_GLOBAL_EXTERN(PositionalSound*, g_positionalHead);
GAME_GLOBAL_EXTERN(int, g_permanentCount);
GAME_GLOBAL_EXTERN(int, g_levelCount);
GAME_GLOBAL_EXTERN(int, g_positionalCount);

} // namespace Sfx
