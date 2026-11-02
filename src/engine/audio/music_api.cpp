// The music and streams front end: the game's calls for music tracks, music streams and streamed sounds.

#include "engine/audio/music_api.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/stubs.h"
#include "engine/file/async.h"
#include "engine/maths/vec4.h"
#include "engine/objects/stream_component.h"
#include "engine/audio/continue_music.h"
#include "engine/audio/music_player.h"
#include "engine/audio/stream.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/gfx/viewport.h"
#include "engine/sys/game_config.h"
#include "engine/objects/anim_movies.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/audio/audio_state.h"

namespace audio {

MusicApiState& music_api_state()
{
    static core::NoDestroy<MusicApiState> state;
    return state.get();
}

namespace {

bool sound_available() { return music_state().sound_device_ && game_config::hardware() != 2; }
bool music_on() { return sound_available() && music_state().music_header_loaded_; }
bool streams_on() { return sound_available() && music_state().streams_on_; }

bool& initialised() { return music_api_state().initialised_; }
// The music stream: 0 none, 1 playing, 2 preloaded.
int& music_channel_kind() { return music_state().bgm_stream_kind_; }
constexpr int kMusicStreamNone = 0;
constexpr int kMusicStreamNormal = 1;
constexpr int kMusicStreamPreload = 2;
// Frames until play_next_track is next tried (Update counts them).
int& frame_count() { return music_api_state().frame_count_; }
float& default_stream_volume() { return music_state().music_volume_; }
float& base_music_gain() { return music_state().music_stream_volume_; }
float get_zero() { return objects::g_float_zero; }
int& current_track_index() { return music_api_state().current_track_index_; }

// The three streams (music_player.h).
Stream* stream_at(int index) { return &core::at(music_state().streams_, index); }
constexpr int kStreams = 3;
// The objects that started each stream (null: none); each notes the ids of the streams it started at
// +0x18 + 4 * slot.
std::array<objects::AudioStreamModule*, 3>& get_owners() { return music_state().stream_owners_; }

// The user soundtrack (see music_player.cpp).
int& user_soundtrack() { return music_api_state().user_soundtrack_; }
bool custom_soundtrack_active() { return music_api_state().custom_playlist_on_; }
std::array<int, 600>& user_order() { return music_state().user_order_; }
uint32_t& user_track() { return music_api_state().user_track_; }
uint32_t& user_position() { return music_api_state().user_position_; }
bool shuffle_user() { return music_state().shuffle_user_; }
int current_playlist() { return music_api_state().current_playlist_; }

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
sound::AudioMixer* sound_manager() { return sound::get_audio_mixer(); }

} // namespace

// 0x004505F0
bool music_off_requested() { return !music_on(); }

// 0x00450620
bool streams_off() { return !streams_on(); }

// 0x00450500
bool stream_exists(uint32_t name) { return stream_exists(name, 0) != 0; }

// 0x00450520
int get_current_track() { return current_track_index(); }

// 0x00450530
void pick_track(int track) { current_track_index() = track; }

// 0x00450650
void stop_music()
{
    if (!music_on())
        return;
    if (continue_music::is_held()) // NOT ORIGINAL - continue_music: a level change keeps the song
        return;
    frame_count() = 1;
    music_channel_kind() = kMusicStreamNone;
    platform_stop_music();
}

// 0x00450820
bool play_track_by_name(const char* name, float volume)
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
        user_track() = core::at(user_order(), user_position());
    } else if (++user_track() >= num_user_tracks(user_soundtrack())) {
        user_track() = 0;
    }
    platform_set_music_volume(volume);
    return true;
}

// 0x00450EF0
void play_music_stream(uint32_t /*name*/, float volume)
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
bool prime_music_track(uint32_t name)
{
    if (!music_on())
        return false;
    platform_stop_music();
    if (!music_on())
        return false;
    const bool loading = start_track_playback(name);
    if (loading)
        music_channel_kind() = kMusicStreamPreload;
    return loading;
}

// 0x00450A50
bool is_music_track_primed()
{
    if (!music_on())
        return false;
    return music_preload_done();
}

// 0x00450A80
bool begin_primed_music_track(float volume)
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
void halt_streams(int which)
{
    if (!streams_on())
        return;
    if (which == -1)
        platform_stop_streams();
    else
        platform_stop_stream(which);
}

// 0x004506D0
void halt_stream(uint32_t id_or_name)
{
    if (!streams_on())
        return;
    uint32_t id = id_or_name;
    const int index = find_playing_stream(id_or_name);
    if (index >= 0) {
        platform_stop_stream(index);
        id = stream_at(index)->id;
    }
    for (int slot = 0; slot < kStreams; ++slot) {
        objects::AudioStreamModule* owner = core::at(get_owners(), slot);
        if (!owner)
            continue;
        for (int i = 0; i < kStreams; ++i) {
            if (core::at(owner->streams_, i) == id) {
                core::at(owner->streams_, i) = 0;
                core::at(get_owners(), slot) = nullptr;
                return;
            }
        }
    }
}

// 0x00450780
bool change_stream_loudness(uint32_t id_or_name, void* volume)
{
    if (streams_on()) {
        const int index = find_playing_stream(id_or_name);
        if (index < 0)
            return false;
        platform_set_stream_volume(volume, index);
    }
    return true;
}

// 0x004507D0
bool set_stream_pitch(uint32_t id_or_name, float /*pitch*/)
{
    if (streams_on()) {
        const int index = find_playing_stream(id_or_name);
        if (index < 0)
            return false;
        stubs::return_true(); // (the platform's set pitch, 0x005B9760: nothing on PC)
    }
    return true;
}

// 0x00450940
bool pre_load_stream_done(uint32_t id_or_name)
{
    if (!streams_on())
        return false;
    const int index = find_playing_stream(id_or_name);
    if (index < 0)
        return false;
    return stream_preload_done(index);
}

// 0x00450980
bool start_pre_loaded_stream(uint32_t id_or_name, void* volume, float pitch, int priority)
{
    if (!streams_on())
        return false;
    const int index = find_playing_stream(id_or_name);
    if (index < 0)
        return false;
    stream_at(index)->option = priority;
    (void)pitch; // (the original passes it on; the platform's start doesn't take it)
    return start_preloaded_stream(index, static_cast<const StreamGain*>(volume));
}

// 0x00450AE0
bool remove_stream(uint32_t id_or_name, uint32_t entry, void* owner)
{
    if (!streams_on())
        return false;
    int index = -1;
    if (owner) {
        for (int i = 0; i < kStreams; ++i) {
            if (core::at(get_owners(), i) == owner && stream_at(i)->name == id_or_name &&
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
    Stream* stream = stream_at(index);
    if (stream->name_count_ <= 0)
        return false;
    if (core::at(stream->names, 0).checksum != entry)
        return true;
    for (int i = 1; i < stream->name_count_; ++i)
        core::at(stream->names, i - 1) = core::at(stream->names, i);
    const int count = --stream->name_count_;
    // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): the original's, as written
    if (stream->name_position_ >= 0 && --stream->name_position_ < 0) {
        stream->name_position_ = count - 1;
        if (stream->option >= 0)
            ++stream->option;
    }
    return true;
}

// 0x00450BE0
bool update_stream_volume(int slot)
{
    if (!streams_on())
        return false;
    objects::AudioStreamModule* owner = core::at(get_owners(), slot);
    const uint32_t id = core::at(owner->streams_, slot);
    int index = 0;
    while (index < kStreams && stream_at(index)->id != id)
        ++index;
    if (index == kStreams)
        return false;
    // (the status is of stream `slot`'s channel, not the one found - as the original)
    if (channel_status(stream_at(slot)->channel) == 1)
        return false;
    Stream* stream = stream_at(index);
    sound::VolumeRef volume;
    sound::AudioMixer* manager = sound_manager();
    if (stream->use_pos_info_) {
        // (the emitter nearest the owner's object: 0x004A09E0, not rewritten yet)
        void* object = render::fetch_nearest_camera(
            &owner->entity_->at_point_, nullptr);
        if (object) {
            auto* component = owner;
            maths::Vec4 extent{};
            maths::Vec4* extent_or_null = nullptr;
            if (component->get_stream_extent(object, &extent))
                extent_or_null = &extent;
            maths::Vec4 position{};
            const maths::Vec4* where = component->get_stream_position(&position, object);
            manager->fetch_loudness(&volume, where, stream->dropoff, stream->falloff_curve_,
                                    object, extent_or_null);
        } else {
            volume.change_muted();
        }
    } else {
        std::memcpy(&core::at(volume.lanes_, 0), &stream->volume, 4);
        std::memcpy(&core::at(volume.lanes_, 1), &stream->volume, 4);
    }
    if (std::bit_cast<uint32_t>(stream->volume) != kHundredPercent) // (the bits compared, as the original)
        volume.percent_tweak(stream->volume);
    platform_set_stream_volume(&volume, stream->channel);
    return true;
}

// 0x00450D60
void prepare_all()
{
    get_owners().fill(nullptr);
    if (sound_available() && (music_state().music_header_loaded_ || streams_on())) {
        init_platform();
        initialised() = true;
        default_stream_volume() = static_cast<float>(script::fetch_int_value(0xABD4A575u));
        if (default_stream_volume() == get_zero())
            default_stream_volume() = 50.0f;
        base_music_gain() = static_cast<float>(script::fetch_int_value(0x73F8E03Bu));
        if (base_music_gain() == get_zero())
            base_music_gain() = 100.0f;
        auto& playlists = music_state().playlists_;
        for (int i = 0; i < 2; ++i) { // (the first two of the three)
            Playlist& list = core::at(playlists, i);
            list.count = 0;
            core::at(list.off, 0) = core::at(list.off, 1) = 0;
            core::at(list.played, 0) = core::at(list.played, 1) = 0;
            list.unknownca8_ = 1;
        }
        for (int i = 0; i < kStreams; ++i) {
            stream_at(i)->name = 0;
            stream_at(i)->id = 0;
        }
    }
    get_owners().fill(nullptr);
}

// 0x00450EB0
bool disc_mode_active()
{
    if (game_config::from_cd() && initialised()) {
        // NOT ORIGINAL - continue_music: the song plays on through the load (on the PC it streams from its own file,
        // read by Bink's thread - nothing for the level's reads to wait for, unlike a console's disc)
        if (!continue_music::is_switched_on())
            stop_music();
        if (streams_on())
            platform_stop_streams();
    }
    return false;
}

// 0x00450540
bool FamData::read_in(uint32_t fam_name)
{
    std::array<char, 256> path{};
    text::format_to(path, "fam\\%x.fam", fam_name);
    file::AsyncFileHandle* file = file::async_open(path.data(), true, 100);
    if (!file)
        return false;
    size = file->get_file_size();
    if (size >= 0x1000) {
        file::async_close(file);
        return false;
    }
    file->get_take_in(data.data(), 1, size);
    file::async_close(file);
    name = fam_name;
    return true;
}

} // namespace audio
