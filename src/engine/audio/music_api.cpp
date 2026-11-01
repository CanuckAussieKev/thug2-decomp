// The music and streams front end: the game's calls for music tracks, music streams and streamed sounds.

#include "engine/audio/music_api.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/stubs.h"
#include "engine/file/async.h"
#include "engine/maths/vec4.h"
#include "engine/objects/stream_component.h"
#include "engine/audio/music_player.h"
#include "engine/audio/stream.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/gfx/viewport.h"
#include "engine/sys/game_config.h"
#include "engine/objects/anim_movies.h"

namespace audio {

int g_frame_count{1};
int g_current_track_index;
int g_current_playlist;
bool g_initialised;
bool g_custom_playlist_on;
int g_user_soundtrack;
uint32_t g_user_track;
uint32_t g_user_position;

namespace {

bool sound_available() { return g_sound_device && game_config::g_platform != 2; }
bool music_on() { return sound_available() && g_music_header_loaded; }
bool streams_on() { return sound_available() && g_streams_on; }

bool& initialised() { return g_initialised; }
// The music stream: 0 none, 1 playing, 2 preloaded.
int& music_channel_kind() { return g_bgm_stream_kind; }
enum : int { kMusicStreamNone = 0, kMusicStreamNormal = 1, kMusicStreamPreload = 2 };
// Frames until play_next_track is next tried (Update counts them).
int& frame_count() { return g_frame_count; }
float& default_stream_volume() { return g_music_volume; }
float& base_music_gain() { return g_music_stream_volume; }
float get_zero() { return objects::g_float_zero; }
int& current_track_index() { return g_current_track_index; }

// The stream fields used here (Stream in music_player.h; 0x74 bytes each, three at 0x0069B938).
struct StreamFields {
    uint32_t name;    // +0x00
    uint32_t id;      // +0x04
    uint32_t volume;  // +0x08 (a float: percent)
    uint32_t dropoff; // +0x0C (a float)
    int channel;      // +0x10
    uint8_t pad14[4];
    int priority;        // +0x18
    int queue_position_; // +0x1C
    int queue_count_;    // +0x20
    uint8_t pad24[4];
    bool positional; // +0x28: the volume follows the owner's object
    uint8_t pad29[3];
    int falloff_curve_; // +0x2C
    uint8_t pad30[4];
    struct TableRow {
        uint32_t first;
        uint32_t second;
    } queue[8]; // +0x34
};
static_assert(sizeof(StreamFields) == 0x74, "layout must match the original");
StreamFields* stream_at(int index) { return reinterpret_cast<StreamFields*>(g_streams) + index; }
constexpr int kStreams = 3;
// The objects that started each stream (null: none); each notes the ids of the streams it started at
// +0x18 + 4 * slot.
uint8_t** get_owners() { return reinterpret_cast<uint8_t**>(g_stream_owners); }
uint32_t* owner_ids(uint8_t* owner) { return reinterpret_cast<objects::AudioStreamModule*>(owner)->streams_; }

// The user soundtrack (see music_player.cpp).
int& user_soundtrack() { return g_user_soundtrack; }
bool custom_soundtrack_active() { return g_custom_playlist_on; }
int* user_order() { return g_user_order; }
uint32_t& user_track() { return g_user_track; }
uint32_t& user_position() { return g_user_position; }
bool shuffle_user() { return g_shuffle_user; }
int current_playlist() { return g_current_playlist; }

// The platform layer (stream.h). The original passes the music's stop 1 (StopMusic) or 0 (the music streams),
// which it doesn't use; the stream's stop is given 1 (unused too).
void platform_stop_streams() { audio::halt_streams(); }
void platform_stop_stream(int index) { audio::halt_stream(index, 1); }
void platform_set_stream_volume(const void* volume, int index)
{
    // (an VolumeRef starts with the platform's StreamGain: five channels and the type)
    audio::change_stream_loudness(static_cast<const StreamGain*>(volume), index);
}
int channel_status(int channel) { return stream_status(channel); }
uint32_t num_user_tracks(int soundtrack) { return static_cast<uint32_t>(user_soundtrack_tracks(soundtrack)); }

constexpr uint32_t kHundredPercent = 0x42C80000; // 100.0f
sound::AudioMixer* sound_manager() { return sound::g_audio_mixer; }

} // namespace

// 0x004505F0
bool __cdecl music_off_requested() { return !music_on(); }

// 0x00450620
bool __cdecl streams_off() { return !streams_on(); }

// 0x00450500
bool __cdecl stream_exists(uint32_t name) { return stream_exists(name, 0) != 0; }

// 0x00450520
int __cdecl get_current_track() { return current_track_index(); }

// 0x00450530
void __cdecl pick_track(int track) { current_track_index() = track; }

// 0x00450650
void __cdecl stop_music()
{
    if (!music_on())
        return;
    frame_count() = 1;
    music_channel_kind() = kMusicStreamNone;
    platform_stop_music();
}

// 0x00450820
bool __cdecl play_track_by_name(const char* name, float volume)
{
    if (!music_on())
        return false;
    if (!custom_soundtrack_active() || current_playlist() == 1) {
        if (!load_music_track_by_name(name))
            return false;
        platform_set_music_volume(volume);
        return true;
    }
    if (!num_user_tracks(user_soundtrack()))
        return false;
    if (!play_user_track(user_soundtrack(), static_cast<int>(user_track())))
        return false;
    if (shuffle_user()) {
        if (++user_position() >= num_user_tracks(user_soundtrack())) {
            shuffle_user_soundtrack();
            user_position() = 0;
        }
        user_track() = user_order()[user_position()];
    } else if (++user_track() >= num_user_tracks(user_soundtrack())) {
        user_track() = 0;
    }
    platform_set_music_volume(volume);
    return true;
}

// 0x00450EF0
void __cdecl play_music_stream(uint32_t /*name*/, float volume)
{
    if (!music_on())
        return;
    platform_stop_music();
    if (volume < get_zero())
        volume = base_music_gain();
    if (music_on())
        platform_set_music_volume(volume);
    music_channel_kind() = kMusicStreamNormal;
}

// 0x004509E0
bool __cdecl prime_music_track(uint32_t name)
{
    if (!music_on())
        return false;
    platform_stop_music();
    if (!music_on())
        return false;
    bool loading = start_track_playback(name);
    if (loading)
        music_channel_kind() = kMusicStreamPreload;
    return loading;
}

// 0x00450A50
bool __cdecl is_music_track_primed()
{
    if (!music_on())
        return false;
    return music_preload_done();
}

// 0x00450A80
bool __cdecl begin_primed_music_track(float volume)
{
    if (!music_on())
        return false;
    if (volume < get_zero())
        volume = base_music_gain();
    if (!start_preloaded_music())
        return false;
    platform_set_music_volume(volume);
    return true;
}

// 0x00450690
void __cdecl halt_streams(int which)
{
    if (!streams_on())
        return;
    if (which == -1)
        platform_stop_streams();
    else
        platform_stop_stream(which);
}

// 0x004506D0
void __cdecl halt_stream(uint32_t id_or_name)
{
    if (!streams_on())
        return;
    uint32_t id = id_or_name;
    int index = find_playing_stream(id_or_name);
    if (index >= 0) {
        platform_stop_stream(index);
        id = stream_at(index)->id;
    }
    for (int slot = 0; slot < kStreams; ++slot) {
        uint8_t* owner = get_owners()[slot];
        if (!owner)
            continue;
        for (int i = 0; i < kStreams; ++i) {
            if (owner_ids(owner)[i] == id) {
                owner_ids(owner)[i] = 0;
                get_owners()[slot] = nullptr;
                return;
            }
        }
    }
}

// 0x00450780
bool __cdecl change_stream_loudness(uint32_t id_or_name, void* volume)
{
    if (streams_on()) {
        int index = find_playing_stream(id_or_name);
        if (index < 0)
            return false;
        platform_set_stream_volume(volume, index);
    }
    return true;
}

// 0x004507D0
bool __cdecl set_stream_pitch(uint32_t id_or_name, float /*pitch*/)
{
    if (streams_on()) {
        int index = find_playing_stream(id_or_name);
        if (index < 0)
            return false;
        stubs::return_true(); // (the platform's set pitch, 0x005B9760: nothing on PC)
    }
    return true;
}

// 0x00450940
bool __cdecl pre_load_stream_done(uint32_t id_or_name)
{
    if (!streams_on())
        return false;
    int index = find_playing_stream(id_or_name);
    if (index < 0)
        return false;
    return stream_preload_done(index);
}

// 0x00450980
bool __cdecl start_pre_loaded_stream(uint32_t id_or_name, void* volume, float pitch, int priority)
{
    if (!streams_on())
        return false;
    int index = find_playing_stream(id_or_name);
    if (index < 0)
        return false;
    stream_at(index)->priority = priority;
    (void)pitch; // (the original passes it on; the platform's start doesn't take it)
    return start_preloaded_stream(index, static_cast<const StreamGain*>(volume));
}

// 0x00450AE0
bool __cdecl remove_stream(uint32_t id_or_name, uint32_t entry, void* owner)
{
    if (!streams_on())
        return false;
    int index = -1;
    if (owner) {
        for (int i = 0; i < kStreams; ++i) {
            if (get_owners()[i] == owner && stream_at(i)->name == id_or_name &&
                channel_status(stream_at(i)->channel) != 1) {
                index = i;
                break;
            }
        }
        if (index < 0)
            return false;
    } else {
        index = find_playing_stream(id_or_name);
        if (index < 0)
            return false;
    }
    StreamFields* stream = stream_at(index);
    if (stream->queue_count_ <= 0)
        return false;
    if (stream->queue[0].first != entry)
        return true;
    for (int i = 1; i < stream->queue_count_; ++i)
        stream->queue[i - 1] = stream->queue[i];
    int count = --stream->queue_count_;
    if (stream->queue_position_ >= 0 && --stream->queue_position_ < 0) {
        stream->queue_position_ = count - 1;
        if (stream->priority >= 0)
            ++stream->priority;
    }
    return true;
}

// 0x00450BE0
bool __cdecl update_stream_volume(int slot)
{
    if (!streams_on())
        return false;
    uint8_t* owner = get_owners()[slot];
    uint32_t id = owner_ids(owner)[slot];
    int index = 0;
    while (index < kStreams && stream_at(index)->id != id)
        ++index;
    if (index == kStreams)
        return false;
    // (the status is of stream `slot`'s channel, not the one found - as the original)
    if (channel_status(stream_at(slot)->channel) == 1)
        return false;
    StreamFields* stream = stream_at(index);
    sound::VolumeRef volume;
    sound::AudioMixer* manager = sound_manager();
    if (stream->positional) {
        // (the emitter nearest the owner's object: 0x004A09E0, not rewritten yet)
        void* object = render::fetch_nearest_camera(
            &reinterpret_cast<objects::AudioStreamModule*>(owner)->entity_->at_point_, nullptr);
        if (object) {
            auto* component = reinterpret_cast<objects::AudioStreamModule*>(owner);
            maths::Vec4 extent;
            maths::Vec4* extent_or_null = nullptr;
            if (component->get_stream_extent(object, &extent))
                extent_or_null = &extent;
            maths::Vec4 position;
            maths::Vec4* where = component->get_stream_position(&position, object);
            manager->fetch_loudness(&volume, where, std::bit_cast<float>(stream->dropoff), stream->falloff_curve_,
                                    object, extent_or_null);
        } else {
            volume.change_muted();
        }
    } else {
        std::memcpy(&volume.lanes_[0], &stream->volume, 4);
        std::memcpy(&volume.lanes_[1], &stream->volume, 4);
    }
    if (stream->volume != kHundredPercent)
        volume.percent_tweak(std::bit_cast<float>(stream->volume));
    platform_set_stream_volume(&volume, stream->channel);
    return true;
}

// 0x00450D60
void __cdecl prepare_all()
{
    get_owners()[0] = get_owners()[1] = get_owners()[2] = nullptr;
    if (sound_available() && (g_music_header_loaded || streams_on())) {
        init_platform();
        initialised() = true;
        default_stream_volume() = static_cast<float>(script::fetch_int_value(0xABD4A575u));
        if (default_stream_volume() == get_zero())
            default_stream_volume() = 50.0f;
        base_music_gain() = static_cast<float>(script::fetch_int_value(0x73F8E03Bu));
        if (base_music_gain() == get_zero())
            base_music_gain() = 100.0f;
        auto* playlists = g_playlists;
        for (int i = 0; i < 2; ++i) { // (the first two of the three)
            Playlist& list = playlists[i];
            list.count = 0;
            list.off[0] = list.off[1] = 0;
            list.played[0] = list.played[1] = 0;
            list.unknownca8_ = 1;
        }
        for (int i = 0; i < kStreams; ++i) {
            stream_at(i)->name = 0;
            stream_at(i)->id = 0;
        }
    }
    get_owners()[0] = get_owners()[1] = get_owners()[2] = nullptr;
}

// 0x00450EB0
bool __cdecl disc_mode_active()
{
    if (game_config::g_from_cd && initialised()) {
        stop_music();
        if (streams_on())
            platform_stop_streams();
    }
    return false;
}

// 0x00450540
bool FamData::read_in(uint32_t fam_name)
{
    char path[256];
    std::snprintf(path, sizeof(path), "fam\\%x.fam", fam_name);
    file::AsyncFileHandle* file = file::async_open(path, true, 100);
    if (!file)
        return false;
    size = file->get_file_size();
    if (size >= 0x1000) {
        file::async_close(file);
        return false;
    }
    file->get_take_in(data, 1, size);
    file::async_close(file);
    name = fam_name;
    return true;
}

} // namespace audio
