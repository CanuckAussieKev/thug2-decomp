#pragma once

// The sound effects manager (THUG's sound::AudioMixer over the PC DirectSound driver; 0x0047AC10-0x0047D41E): the
// wave tables (sounds loaded from data\sounds\*.snd: permanent ones 0x006F4268 and level ones 0x006F5E68), the
// sounds playing (0x006F7A68: 48 slots, each on one of the driver's voices 0x006F85C8), the positional sounds
// that follow objects (a list of 128 entries from 0x006F3668), volumes (VolumeRef: up to 5 channels) and the
// listener.
//
// Nothing plays when there's no sound output (0x007D6A38: DirectSound's device, made through audio::Device) or on
// the platforms without sound (0x007CEFA8 = 2 or 6): most functions check that first ("NoSound()").
//
// NOT ORIGINAL: thug2-decomp.ini [features] mute_audio=1 silences everything (effects, music, streams, movies) from
// the first frame - the muted sound output, engine/audio/muted_device.h; the workspaces' test games run so.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "base/core/layout_32bit.h"
#include "base/maths/vec4.h"
#include "engine/audio/audio_handles.h"
#include "engine/memory/mem.h"
#include "base/core/checksums.h"
#include <gsl/pointers>
#include <array>

namespace objects {
class AudioEmitterModule;
}

namespace sound {
constexpr size_t kWaveSlotCount = 256;       // loaded waves: the permanent and the level's
constexpr size_t kPlayingSoundCount = 48;
constexpr size_t kPositionalVoiceCount = 128;
constexpr size_t kLaneCount = 5;             // a volume's levels: left, right and the surround channels
constexpr float kPercentScale = 0.0099999998F; // a percentage as a fraction (the original's float)
constexpr size_t kVolumeRefBytes = 0x2C;      // layouts
constexpr size_t kPcWaveBytes = 0x44;
constexpr size_t kDescSizeOffset = 0x14;
constexpr size_t kDataOffset = 0x3C;
constexpr size_t kWaveSlotBytes = 0x1C;
constexpr size_t kPlaybackPropsBytes = 0x2C;
constexpr size_t kPlayingSoundBytes = 0x3C;

// A volume: a level per channel (percent; 2 channels, or 5 for the surround type) and its type; a 3D one also
// has the sound's position and range.
struct VolumeRef {
    static constexpr int kTypeStereo = 0;
    static constexpr int kTypePositional = 1;
    static constexpr int kTypeSurround = 2; // 5 channels
    static constexpr int kType3d = 3;       // the voice's 3D buffer places it (never silent)

    // 0x0047B030: the manager's default type, every channel 0.
    VolumeRef();
    // 0x0047B070 (returns this)
    VolumeRef* change_species(int type);
    // 0x0047AC50: every channel scaled by `percent` percent.
    void percent_tweak(float percent);
    // 0x0047ACF0: whether every channel is 0 (and it isn't 3D).
    bool is_audio_off();
    // 0x0047AD60: every channel 0.
    void change_muted();
    // 0x0047B080: the channel with the greatest magnitude (its signed value; the first of equals).
    float find_peak_channel();

    [[nodiscard]] int channel_count() const { return kind_tag_ == kTypeSurround ? static_cast<int>(kLaneCount) : 2; }

    // The fields other code reads and sets.
    std::array<float, kLaneCount>& lanes() { return lanes_; }
    [[nodiscard]] const std::array<float, kLaneCount>& lanes() const { return lanes_; }
    [[nodiscard]] int kind_tag() const { return kind_tag_; }
    void set_kind_tag(int value) { kind_tag_ = value; }
    maths::Vec4& anchor_point() { return anchor_point_; }
    [[nodiscard]] const maths::Vec4& anchor_point() const { return anchor_point_; }
    [[nodiscard]] float dropoff() const { return dropoff_; }
    void set_dropoff(float value) { dropoff_ = value; }

private:
    std::array<float, kLaneCount> lanes_{};         // +0x00
    int kind_tag_;             // +0x14
    maths::Vec4 anchor_point_{}; // +0x18: 3D: where the sound is
    float dropoff_{};            // +0x28: 3D: its range
};
static_assert(sizeof(VolumeRef) == kVolumeRefBytes, "layout must match the original");

// A loaded sound: its WAVEFORMATEX, the DirectSound buffer description, its samples (0x44 bytes).
struct PcWave {

    uint16_t format_tag_;           // +0x00 (WAVEFORMATEX)
    uint16_t channels;              // +0x02
    uint32_t samples_per_second_;   // +0x04
    uint32_t bytes_per_second_;     // +0x08
    uint16_t block_align_;          // +0x0C
    uint16_t bits_per_sample_;      // +0x0E
    uint16_t extra_size_;           // +0x10
    uint16_t unknown12;             // +0x12 (0x40 once loaded)
    uint32_t desc_size_;            // +0x14 (DSBUFFERDESC: 0x24)
    uint32_t desc_flags_;           // +0x18
    uint32_t desc_bytes_;           // +0x1C
    uint32_t desc_reserved_;        // +0x20
    PcWave* desc_format_;           // +0x24: this (the WAVEFORMATEX at its start)
    std::array<uint32_t, 4> desc_3d_algorithm_; // +0x28
    uint32_t data_size_;            // +0x38
    uint8_t* data;                  // +0x3C
    bool looping;                   // +0x40: the file's smpl chunk has loops
};

// 0x0047C870: the .wav file's format and samples (converted, 0x005F5A20), its loops (a smpl chunk with some);
// the buffer description with `descFlags`. False if the file can't be opened. (The sample buffer is the
// format's bytes-per-second long: one second, whatever the data chunk's size.)
bool read_in(PcWave& pc_wave, const char* file_name, uint32_t desc_flags);
static_assert(core::layout_32bit(sizeof(PcWave) == kPcWaveBytes && offsetof(PcWave, desc_size_) == kDescSizeOffset && offsetof(PcWave, data) == kDataOffset),
              "layout must match the original");

// How a sound is asked to play (the play parameters: pitch and volume in percent, the dropoff - 0: the wave's - and
// the falloff function).
struct SoundParams {
    float pitch;
    float volume;
    float dropoff;
    int falloff;
};

// A wave table entry (0x1C bytes).
struct SoundWaveSlot {
    uint32_t checksum;  // +0x00: of the file's name (without folders)
    uint16_t flags;     // +0x04
    uint8_t load_type_; // +0x06: 0 = level (the temporary table), else permanent (2: ... by group)
    uint8_t group;      // +0x07
    float pitch;        // +0x08 (percent)
    float volume;       // +0x0C (percent)
    float dropoff;      // +0x10 (0: the default 0x0067DA34)
    PcWave* wave;       // +0x14
    bool looping;       // +0x18
    bool not_streamed_; // +0x19
    std::array<uint8_t, 2> unknown1a_;
};
static_assert(core::layout_32bit(sizeof(SoundWaveSlot) == kWaveSlotBytes), "layout must match the original");

// How a sound plays (0x2C bytes): from the play parameters {pitch, volume, dropoff, falloff} and the wave's
// defaults.
struct PlaybackProps {
    uint32_t checksum;  // +0x00
    float dropoff;      // +0x04
    float pitch;        // +0x08 (percent)
    float volume;       // +0x0C (percent)
    float pitch2;       // +0x10
    float volume2;      // +0x14
    float pitch3;       // +0x18
    float volume3;      // +0x1C
    float unknown20; // +0x20 (a pitch step)
    float unknown24; // +0x24 (a volume step)
    uint32_t falloff;   // +0x28
};
static_assert(sizeof(PlaybackProps) == kPlaybackPropsBytes, "layout must match the original");

// A sound playing (0x3C bytes; 48 of them).
struct PlayingSound {
    uint32_t unique_id_;  // +0x00
    uint32_t checksum;    // +0x04
    SoundWaveSlot* entry; // +0x08 (the wave table's)
    PlaybackProps props;  // +0x0C
    uint32_t unknown38;   // +0x38: the next time (frame_clock::GetTime) a distant positional sound is checked
};
static_assert(core::layout_32bit(sizeof(PlayingSound) == kPlayingSoundBytes), "layout must match the original");

// A positional sound following an object (0x18 bytes; 128 of them).
struct PositionalSound {
    uint32_t unique_id_;                 // +0x00
    uint32_t checksum;                   // +0x04
    uint32_t in_use_;                    // +0x08 (bit 0)
    objects::AudioEmitterModule* object; // +0x0C the object's sound component
    PositionalSound* next;               // +0x10
    PositionalSound* prev;               // +0x14
};

// One of the driver's voices (0x18 bytes; up to 48): its buffer on the sound output (audio::Device).
struct Voice {
    audio::SoundBuffer* buffer;       // +0x00 (DirectSound: IDirectSoundBuffer)
    audio::SoundBuffer3d* buffer_3d_; // +0x04: its 3D controls (3D hardware only)
    int base_frequency_; // +0x08
    int frequency;       // +0x0C: the last set
    bool playing;        // +0x10
    std::array<uint8_t, 3> unknown11;
    PcWave* wave; // +0x14
};

class AudioMixer {
public:
    // 0x0047B160: the voices set up, nothing playing.
    AudioMixer();
    // 0x0047AD80: the slot of the playing sound `id` - by unique id, else by name (-1 if neither is playing).
    static int get_playing_index(uint32_t id);
    // 0x0047AE20: a positional sound for `object` (nothing at 128).
    static void add_positional_sound(uint32_t unique_id, checksums::Name sound_name,
                                     objects::AudioEmitterModule* object);
    // 0x0047AE90: (a group's sounds stopped - empty in this build).
    static void stop_group(int group);
    // 0x0047AEA0: the permanent waves loaded for `group` (load type 2) dropped from the table.
    static void unload_group(int group);
    // 0x0047AF10: unload_group for every group (there are none in this build).
    static void unload_all_groups();
    // 0x0047AF40: `props` from the wave's defaults and the play parameters `params` (null: defaults).
    static void init_playback_props(PlaybackProps* props, SoundWaveSlot* entry, const SoundParams* params);
    // 0x0047B340: whether sounds are paused (the game's two pause flags) - unless +0 is set.
    [[nodiscard]] bool is_frozen() const;
    // How a sound is loaded: its flags, its load type and its group.
    struct SoundLoad {
        uint16_t flags{};
        int load_type_{};
        int group{};
    };

    // A sound's dropoff, pitch and volume.
    struct SoundLevels {
        float dropoff{};
        float pitch{};
        float volume{};
    };

    // 0x0047B360: `path` loaded into the level (`loadType` 0) or the permanent table; false if it's loaded
    // already, the table is full (256) or the file can't be read.
    static bool ingest_sound(const char* path, SoundLoad sound_load, SoundLevels levels);
    // 0x0047B480: the wave's volume (and the game's 50 %) applied to `volume`, its pitch to `pitch`.
    static void adjust_volume_and_pitch(VolumeRef* volume, float* pitch, SoundWaveSlot* entry);
    // 0x0047B4E0: the sound `id` stopped (false if it isn't playing).
    static bool stop_sound(uint32_t id);
    // 0x0047B520: the volume and pitch of the playing sound `id`.
    static bool set_sound_params(uint32_t id, VolumeRef* volume, float pitch);
    // 0x0047B5D0: the main volume (0x0067DA38; below 0.1 % silent) for the voices and the user soundtrack.
    static void change_master_loudness(float volume);
    // 0x0047B650: the sfx pitch (0x0067DA3C; 1 % or less: 0.01).
    static void set_pitch(float pitch, int unused1, int unused2);
    // 0x0047B6B0: (debug output, compiled out).
    static void print_sounds(int a, int b, int value);
    // 0x0047B6F0: whether the sound `uniqueId` is playing (its slot in `index`, if given).
    static bool is_sound_playing(uint32_t unique_id, int* index);
    // 0x0047B760: the sound's dropoff distance (the default if it has none or isn't loaded).
    static float get_dropoff_distance(uint32_t checksum);
    // 0x0047B7C0: SetSoundParams by unique id with the sound's own volume and pitch percentages.
    static bool set_sound_params_scaled(uint32_t unique_id, VolumeRef* volume, float pitch);
    // 0x0047B1B0: everything dropped (the positional list, the permanent waves); the default dropoff back to 1020.
    static void sweep_up();
    // 0x0047B210: the default dropoff distance.
    static void set_default_dropoff(float dropoff);
    // 0x0047B240: every voice stopped, the positional list emptied.
    static void stop_all_sounds();
    // 0x0047B2A0: every playing voice set silent (0x0047D390).
    static void update_voice_volumes();
    // 0x0047B2C0: the wave table entry for `checksum` (level ones first).
    static SoundWaveSlot* fetch_wave_slot(uint32_t checksum);

    // --- 0x0047B8E0-0x0047C6BB ---
    // 0x0047B8E0: `pitch` shifted by the sound's motion relative to the camera (Doppler): `position` now,
    // `previous` a frame ago, over `frame_seconds` (nothing when it moved more than 360 units a 60th).
    static void doppler_shift(float* pitch, const maths::Vec4* position, const maths::Vec4* previous,
                              float frame_seconds, void* camera);
    // 0x0047BA00: the props of the positional sound of `object` with that unique id (or, `by_checksum`, name), if
    // it's playing.
    static PlaybackProps* get_positional_props(objects::AudioEmitterModule* object, uint32_t id, bool by_checksum);
    // 0x0047BAB0: whether `id` isn't the unique id of a playing or positional sound.
    static bool is_unique_id_free(uint32_t id);
    // 0x0047BB20: `id`, or the next free one after it.
    static uint32_t fetch_distinct_id(uint32_t id);
    // 0x0047BB50 / 0x0047BC30: the positional sounds of `object` (with that name, unless 0) stopped and dropped.
    void stop_object_sounds(objects::AudioEmitterModule* object);
    void halt_object_sound(objects::AudioEmitterModule* object, uint32_t checksum);
    // A sound's pitch and the unique id it is played with.
    struct PitchId {
        float pitch{};
        uint32_t unique_id_{};
    };

    // 0x0047BD20: the sound `checksum` played at `volume` / `pitch` (the unique id `uniqueId`, else the name;
    // made free); `params` its play parameters. Returns the unique id (0: not played).
    static uint32_t emit_sound(uint32_t checksum, VolumeRef* volume, PitchId pitch_id, const SoundParams* params,
                               int unused);
    // A sound's dropoff distance and its falloff curve.
    struct Falloff {
        float dropoff{};
        int falloff{};
    };

    // 0x0047BE10: `volume` for a sound at `position` heard from `camera` (the nearest if null): a level by the
    // distance over `dropoff` (falloff 0 smooth, 1 linear, 2 squared, 3 root), 3D. False when it's out of range
    // (5 % past the dropoff); true otherwise, or when paused (then silent).
    bool fetch_loudness(VolumeRef* volume, const maths::Vec4* position, Falloff falloff_in, void* camera,
                        void* extent) const;
    // 0x0047C140: the sound played on `object` (positional unless `not_positional`); a silent looping one is kept
    // by the object to start later (unless `force`, which plays it anyway). Returns the unique id.
    uint32_t play_object_sound(uint32_t checksum, const SoundParams* params, objects::AudioEmitterModule* object,
                               bool not_positional, bool force) const;
    // 0x0047C360: a positional sound's volume and pitch from its object's place; false when it should stop.
    bool update_positional_sound(objects::AudioEmitterModule* object, PlayingSound* sound, uint32_t time) const;
    // 0x0047C560: every positional sound updated; those that ended or stop are dropped.
    void tick_positional_sounds();

    // The fields other code reads and sets.
    void set_mute_override(uint8_t value) { mute_override_ = value; }
    [[nodiscard]] int fallback_channel_mode() const { return fallback_channel_mode_; }
    void set_fallback_channel_mode(int value) { fallback_channel_mode_ = value; }

private:
    uint8_t mute_override_{}; // +0x00
    std::array<uint8_t, 3> unknown1_{};
    int fallback_channel_mode_; // +0x04
};

// 0x0047AC10: the falloff function named by `checksum` (0: linear and others).
int falloff_from_checksum(uint32_t checksum);
// 0x0047AFD0: the main volume.
float fetch_master_loudness();
// 0x0047AFE0: how many of the 48 voices are playing.
int count_playing_voices();
// 0x0047B010: whether there's no sound (no device, or a platform without sound).
bool no_sound();
// 0x0047BA60: the manager made when `create` and there's none; a reference taken.
void create_manager(bool create);
// 0x0047C6C0: a reference dropped; the last one deletes the manager (everything dropped first).
void release_manager();

// --- the voices (0x0047C720-0x0047D41E) ---
// 0x0047C720: a voice for `wave`: one holding it already, else the last free one after the last used (or the one
// after the last used).
int fetch_idle_voice(PcWave* wave);
// 0x0047C790: the number of sound groups (none: -1).
int group_count();
// 0x0047C7A0
void stop_voice(int voice);
// 0x0047C7C0 / 0x0047C7D0: the voices' volume (percent) and pitch.
void set_voice_volume(float volume);
void set_voice_pitch(float pitch);
// 0x0047C7E0: whether the voice is playing (1 / 0).
int is_voice_playing(int voice);
// 0x0047C810 (and 0x0047B000, a jump to it): voices whose buffers stopped marked so.
void update_voice_states();
// 0x0047CA50: the voices emptied; their number and the 3D hardware from the device's capabilities; with 3D, the
// listener (0x006F85C0).
void init_voices(AudioMixer* manager);
// 0x0047CBD0: the music, the streams and every voice stopped.
void stop_all_voices();
// 0x0047CC10: stop_all_voices; the permanent waves freed.
void free_permanent_waves();
// 0x0047CC70: data\sounds\`path`.snd loaded into a new PcWave for the entry whose `wave` is `slot`.
bool load_wave(const char* path, uint32_t checksum, SoundWaveSlot* entry, int load_type, int unused);
// 0x0047CD70: the voice's volume, pan and frequency (3D: the buffer's position, the listener's orientation).
void apply_voice_settings(int index, VolumeRef* volume, float pitch);
// 0x0047D130: the wave (the entry's `wave` at `slot`) played on a free voice; the voice, or -1.
int play_voice(const SoundWaveSlot* entry, VolumeRef* volume, float pitch);
// 0x0047D390: every playing voice set silent.
void silence_voices();

// NOT ORIGINAL - the sound effects manager (null until the game makes it), the level's loaded waves (the level editor
// lists them) and how many there are.
AudioMixer* get_audio_mixer();
std::span<const SoundWaveSlot, kWaveSlotCount> loaded_level_waves();
int level_wave_count();

} // namespace sound
