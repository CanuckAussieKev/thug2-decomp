// The sound effects manager (0x0047AC10-0x0047D41E). See sfx.h.

#include "engine/sound/sfx.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "frontend/automation.h"
#include "engine/audio/music_player.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_config.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/objects/movie_manager.h"
#include "engine/core/owner.h"

namespace sound {

float g_default_dropoff{1020.0f};
float g_volume_scale{100.0f};
SoundWaveSlot g_permanent_waves[256];
SoundWaveSlot g_level_waves[256];
PlayingSound g_playing_sounds[48];
memory::owner<AudioMixer*> g_audio_mixer;
PositionalSound* g_positional_head;
int g_permanent_count;
int g_level_count;
int g_positional_count;

namespace {

constexpr int kMaxPlaying = 48;
constexpr int kMaxPositional = 128;
constexpr int kMaxWaves = 256;

// Script globals the original reads (their values unused here; debug switches).
constexpr uint32_t kDebugA = 0x6D2E270E;
constexpr uint32_t kDebugB = 0xD7BB618D;

PlayingSound* get_playing() { return g_playing_sounds; }

float g_master_gain{100.0f};
float g_pitch{1.0f};
PositionalSound g_spatial_voices[128];

PositionalSound* positional_pool() { return g_spatial_voices; }
PositionalSound*& positional_head() { return g_positional_head; }
int& positional_count() { return g_positional_count; }
SoundWaveSlot* permanent_waves() { return g_permanent_waves; }
int& permanent_count() { return g_permanent_count; }
SoundWaveSlot* level_waves() { return g_level_waves; }
int& get_level_count() { return g_level_count; }
float& default_dropoff() { return g_default_dropoff; }
float& master_loudness() { return g_master_gain; }
float& get_pitch() { return g_pitch; }
// Whether a missing sound goes unreported (the report itself is compiled out; only the global is read).
bool quiet_misses() { return game_config::g_from_cd; }

// The missing-sound report (compiled out; the global still read).
void get_report_missing()
{
    if (!quiet_misses())
        script::fetch_int_value(kDebugA);
}

// (debug output, compiled out)
inline void print_sound_debug(int, int, int) {}
inline void print_group(int) {}

} // namespace

// 0x0047AC10
int falloff_from_checksum(uint32_t checksum)
{
    switch (checksum) {
    case 0xF221474C:
        return 1;
    case 0x9706104B:
        return 2;
    case 0x0CA64C41:
        return 3;
    case 0xEF082878: // (named, but the default)
    default:
        return 0;
    }
}

// 0x0047B030
VolumeRef::VolumeRef()
{
    AudioMixer* manager = g_audio_mixer;
    kind_tag_ = manager ? manager->fallback_channel_mode_ : kTypeStereo;
    change_muted();
}

// 0x0047B070
VolumeRef* VolumeRef::change_species(int type)
{
    kind_tag_ = type;
    return this;
}

// 0x0047AC50
void VolumeRef::percent_tweak(float percent)
{
    for (int i = 0; i < channel_count(); ++i)
        lanes_[i] = static_cast<float>(static_cast<double>(percent) * lanes_[i] * 0.0099999998f);
}

// 0x0047ACF0
bool VolumeRef::is_audio_off()
{
    for (int i = 0; i < channel_count(); ++i) {
        if (lanes_[i] != 0.0f)
            return false;
    }
    return kind_tag_ != kType3d;
}

// 0x0047AD60
void VolumeRef::change_muted()
{
    for (float& channel : lanes_)
        channel = 0.0f;
}

// 0x0047B080
float VolumeRef::find_peak_channel()
{
    float loudest = lanes_[0];
    for (int i = 1; i < channel_count(); ++i) {
        if (std::fabs(loudest) < std::fabs(lanes_[i]))
            loudest = lanes_[i];
    }
    return loudest;
}

// 0x0047B010
bool no_sound()
{
    if (!audio::g_sound_device)
        return true;
    int platform = game_config::g_platform;
    return platform == 2 || platform == 6;
}

// 0x0047AFD0
float fetch_master_loudness() { return master_loudness(); }

// 0x0047AFE0
int count_playing_voices()
{
    int playing = 0;
    for (int voice = 0; voice < kMaxPlaying; ++voice) {
        if (is_voice_playing(voice))
            ++playing;
    }
    return playing;
}

// 0x0047B160
AudioMixer::AudioMixer()
{
    if (!no_sound()) {
        fallback_channel_mode_ = VolumeRef::kTypeStereo;
        init_voices(this);
        for (int i = 0; i < kMaxPlaying; ++i) {
            get_playing()[i].unique_id_ = 0;
            get_playing()[i].checksum = 0;
        }
    }
}

// 0x0047AD80
int AudioMixer::get_playing_index(uint32_t id)
{
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (get_playing()[i].unique_id_ == id && is_voice_playing(i))
            return i;
    }
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (get_playing()[i].checksum == id && is_voice_playing(i)) {
            // (a debug report, compiled out)
            if (script::fetch_int_value(kDebugA) == 0)
                script::fetch_int_value(kDebugB);
            return i;
        }
    }
    return -1;
}

// 0x0047AE20
void AudioMixer::add_positional_sound(uint32_t unique_id, uint32_t checksum, objects::AudioEmitterModule* object)
{
    if (unique_id == 0 || positional_count() == kMaxPositional)
        return;
    ++positional_count(); // (counted even when no entry is free, as the original)
    PositionalSound* entry = nullptr;
    for (int i = 0; i < kMaxPositional; ++i) {
        if (!(positional_pool()[i].in_use_ & 1)) {
            entry = &positional_pool()[i];
            break;
        }
    }
    if (!entry)
        return;
    if (PositionalSound* head = positional_head())
        head->prev = entry;
    entry->next = positional_head();
    entry->in_use_ = 1;
    entry->prev = nullptr;
    positional_head() = entry;
    entry->unique_id_ = unique_id;
    entry->checksum = checksum;
    entry->object = object;
}

// 0x0047AE90
void AudioMixer::stop_group(int group)
{
    (void)group;
    group_count();
}

// 0x0047AEA0
void AudioMixer::unload_group(int group)
{
    int count = permanent_count();
    for (int i = 0; i < count;) {
        SoundWaveSlot& entry = permanent_waves()[i];
        if (entry.load_type_ == 2 && entry.group == static_cast<uint32_t>(group)) {
            --count;
            entry = permanent_waves()[count]; // (the last moves into its place)
        } else {
            ++i;
        }
    }
    if (permanent_count() > 0)
        permanent_count() = count;
    print_group(group);
}

// 0x0047AF10
void AudioMixer::unload_all_groups()
{
    for (int group = 0; group < group_count(); ++group)
        unload_group(group);
}

// 0x0047AF40
void AudioMixer::init_playback_props(PlaybackProps* props, SoundWaveSlot* entry, PlaybackProps* params)
{
    if (params) {
        // (the parameters: {pitch, volume, dropoff, falloff} in the props' first four words' order)
        auto* given = reinterpret_cast<uint32_t*>(params);
        std::memcpy(&props->pitch, &given[0], 4);
        std::memcpy(&props->volume, &given[1], 4);
        float dropoff;
        std::memcpy(&dropoff, &given[2], 4);
        props->dropoff = dropoff != 0.0f ? dropoff : entry->dropoff;
        props->unknown20 = 0;
        props->unknown24 = 0;
        props->pitch3 = props->pitch;
        props->pitch2 = props->pitch;
        props->volume3 = props->volume;
        props->volume2 = props->volume;
        props->falloff = given[3];
    } else {
        props->pitch = 100.0f;
        props->volume = 100.0f;
        props->dropoff = 0.0f;
        props->falloff = 0;
    }
    props->checksum = entry->checksum;
}

// 0x0047B1B0
void AudioMixer::sweep_up()
{
    if (no_sound())
        return;
    positional_head() = nullptr;
    positional_count() = 0;
    for (int i = 0; i < kMaxPositional; ++i)
        positional_pool()[i].in_use_ = 0;
    free_permanent_waves();
    permanent_count() = 0;
    default_dropoff() = 1020.0f;
}

// 0x0047B210
void AudioMixer::set_default_dropoff(float dropoff)
{
    if (!no_sound())
        default_dropoff() = dropoff;
}

// 0x0047B240
void AudioMixer::stop_all_sounds()
{
    if (no_sound())
        return;
    stop_all_voices();
    positional_head() = nullptr;
    positional_count() = 0;
    for (int i = 0; i < kMaxPositional; ++i)
        positional_pool()[i].in_use_ = 0;
}

// 0x0047B2A0
void AudioMixer::update_voice_volumes()
{
    if (!no_sound())
        silence_voices();
}

// 0x0047B2C0
SoundWaveSlot* AudioMixer::fetch_wave_slot(uint32_t checksum)
{
    if (no_sound())
        return nullptr;
    for (int i = 0; i < get_level_count(); ++i) {
        if (level_waves()[i].checksum == checksum)
            return &level_waves()[i];
    }
    for (int i = 0; i < permanent_count(); ++i) {
        if (permanent_waves()[i].checksum == checksum)
            return &permanent_waves()[i];
    }
    return nullptr;
}

// 0x0047B340
bool AudioMixer::is_frozen()
{
    if (mute_override_)
        return false;
    return render::g_loading_screen_shown || objects::g_simulation_halted;
}

// 0x0047B360
bool AudioMixer::ingest_sound(const char* path, uint16_t flags, int load_type, int group, float dropoff, float pitch,
                              float volume)
{
    if (no_sound())
        return false;
    // the name without its folders
    const char* name = path;
    int length = static_cast<int>(std::strlen(path));
    for (int i = 0; i < length; ++i) {
        if (path[i] == '\\' || path[i] == '/')
            name = path + i + 1;
    }
    uint32_t checksum = checksums::hash_from_text(name);
    if (fetch_wave_slot(checksum))
        return false;
    SoundWaveSlot* entry;
    if (load_type == 0) {
        if (get_level_count() >= kMaxWaves)
            return false;
        entry = &level_waves()[get_level_count()];
        if (!load_wave(path, checksum, &entry->wave, load_type, group))
            return false;
        ++get_level_count();
    } else {
        if (permanent_count() >= kMaxWaves)
            return false;
        entry = &permanent_waves()[permanent_count()];
        if (!load_wave(path, checksum, &entry->wave, load_type, group))
            return false;
        ++permanent_count();
    }
    entry->pitch = pitch;
    entry->checksum = checksum;
    entry->dropoff = dropoff;
    entry->load_type_ = static_cast<uint8_t>(load_type);
    entry->group = static_cast<uint8_t>(group);
    entry->volume = volume;
    entry->flags = flags;
    return true;
}

// 0x0047B480
void AudioMixer::adjust_volume_and_pitch(VolumeRef* volume, float* pitch, SoundWaveSlot* entry)
{
    if (no_sound())
        return;
    volume->percent_tweak(50.0f);
    if (entry->volume != 100.0f)
        volume->percent_tweak(entry->volume);
    if (entry->pitch != 100.0f)
        *pitch = static_cast<float>(static_cast<double>(*pitch) * entry->pitch * 0.0099999998f);
}

// 0x0047B4E0
bool AudioMixer::stop_sound(uint32_t id)
{
    if (no_sound())
        return true;
    int index = get_playing_index(id);
    if (index < 0)
        return false;
    stop_voice(index);
    return true;
}

// 0x0047B520
bool AudioMixer::set_sound_params(uint32_t id, VolumeRef* volume, float pitch)
{
    if (no_sound())
        return true;
    int index = get_playing_index(id);
    if (index < 0) {
        get_report_missing();
        return false;
    }
    SoundWaveSlot* entry = get_playing()[index].entry;
    if (!entry)
        return false;
    if (!volume->is_audio_off())
        adjust_volume_and_pitch(volume, &pitch, entry);
    apply_voice_settings(index, volume, pitch);
    return true;
}

// 0x0047B5D0
void AudioMixer::change_master_loudness(float volume)
{
    if (no_sound())
        return;
    script::fetch_int_value(kDebugB);
    master_loudness() = volume;
    float scaled = static_cast<float>(static_cast<double>(g_volume_scale) * volume * 0.0099999998f);
    if (scaled < 0.1f)
        scaled = 0.0f;
    set_voice_volume(scaled);
    audio::set_user_soundtrack_volume(scaled);
}

// 0x0047B650
void AudioMixer::set_pitch(float pitch, int /*unused1*/, int /*unused2*/)
{
    if (no_sound())
        return;
    script::fetch_int_value(kDebugB);
    if (!(pitch > 0.0099999998f) && !std::isnan(pitch))
        pitch = 0.0099999998f;
    get_pitch() = pitch;
    set_voice_pitch(pitch);
    // (then a call with the pitch to 0x004504F0, a jump to the folded stub returning false: nothing to do)
}

// 0x0047B6B0
void AudioMixer::print_sounds(int a, int b, int c)
{
    if (!no_sound())
        print_sound_debug(a, b, c);
}

// 0x0047B6F0
bool AudioMixer::is_sound_playing(uint32_t unique_id, int* index)
{
    if (no_sound() || unique_id == 0)
        return false;
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (get_playing()[i].unique_id_ == unique_id) {
            if (!is_voice_playing(i))
                return false;
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

// 0x0047B760
float AudioMixer::get_dropoff_distance(uint32_t checksum)
{
    if (no_sound())
        return default_dropoff();
    SoundWaveSlot* entry = fetch_wave_slot(checksum);
    if (!entry)
        return 0.0f;
    return entry->dropoff == 0.0f ? default_dropoff() : entry->dropoff;
}

// 0x0047B7C0
bool AudioMixer::set_sound_params_scaled(uint32_t unique_id, VolumeRef* volume, float pitch)
{
    if (no_sound())
        return true;
    int index = 0;
    for (; index < kMaxPlaying; ++index) {
        if (get_playing()[index].unique_id_ == unique_id)
            break;
    }
    if (index == kMaxPlaying || !is_voice_playing(index)) {
        get_report_missing();
        return false;
    }
    PlayingSound& sound = get_playing()[index];
    if (!volume->is_audio_off()) {
        if (sound.props.volume != 100.0f)
            volume->percent_tweak(sound.props.volume);
        if (sound.props.pitch != 100.0f)
            pitch = static_cast<float>(static_cast<double>(sound.props.pitch) * pitch * 0.0099999998f);
    }
    SoundWaveSlot* entry = sound.entry;
    if (!entry)
        return false;
    if (!volume->is_audio_off())
        adjust_volume_and_pitch(volume, &pitch, entry);
    apply_voice_settings(index, volume, pitch);
    return true;
}

} // namespace sound
