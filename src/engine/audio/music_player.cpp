// Music: picking the next track of a playlist.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/audio/music_player.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/stubs.h"
#include "engine/gfx/movie.h"
#include "engine/maths/random.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/game_config.h"
#include "engine/objects/stream_component.h"
#include "platform/compiler.h"
#include "engine/core/bits_as.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace audio {

bool g_music_header_loaded{true};
bool g_streams_on{true};
float g_music_volume{50.0f};
float g_music_stream_volume{100.0f};
int g_bgm_stream_kind;
bool g_shuffle_user;
void* g_sound_device;
std::array<Playlist, 2> g_playlists; // (the music's two playlists; 0 is the game's, 1 the script's)
std::array<std::array<char, 100>, 80> g_track_titles;
std::array<Stream, 3> g_streams;
std::array<objects::AudioStreamModule*, 3> g_stream_owners;
std::array<int, 600> g_user_order;
namespace {
std::array<int, 600> g_track_order; // the shuffled order (the word before it, 0x00696D9C, is read once)
std::array<char, 44> g_special_track;
std::array<char, 100> g_soundtrack_name;

std::array<Playlist, 2>& get_playlists() { return g_playlists; }
int& current_playlist() { return g_current_playlist; }
// The track playing (40 bytes, just before the playlists) and whether it loops (SetMusicLooping).
void* special_track() { return g_special_track.data(); }

uint8_t g_random_order{1};
bool g_reshuffle{true};
int g_order_count{1};
bool g_looping;
int g_cleared_by_clear_playlist;
bool g_bgm_held;
int g_order_position;
bool g_music_paused_before;

bool& get_looping() { return g_looping; }
int& current_track() { return g_current_track_index; }

// The shuffled order: the indices of the tracks that are on, how many, and the position of the last one
// played. The track played before the shuffle is read at order[count - 1] - with an empty order that's the
// word before it (0x00696D9C, the movie frame dump counter).
int* get_order() { return g_track_order.data(); }
int& order_count() { return g_order_count; }
int& order_position() { return g_order_position; }
bool& reshuffle() { return g_reshuffle; }

bool music_on() { return g_sound_device && game_config::g_platform != 2 && g_music_header_loaded; }
// The same for streamed sound effects (0x0067D7E1).
bool streams_on() { return g_sound_device && game_config::g_platform != 2 && g_streams_on; }

Stream* get_streams() { return g_streams.data(); }
constexpr int kStreams = 3;
// Whether the platform's stream `channel` is playing (stream_status: 1 free, 2 playing).
bool get_playing(const Stream& stream) { return stream_status(stream.channel) != 1; }
// The stream components that started each stream (null: none); each notes the stream's id.
std::array<objects::AudioStreamModule*, 3>& stream_owners() { return g_stream_owners; }
int& frame_count() { return g_frame_count; }

// The user soundtrack: which, whether it's playing (in place of the game's music), its shuffled order and the
// track in it, whether it's shuffled.
int& user_soundtrack() { return g_user_soundtrack; }
bool& custom_soundtrack_active() { return g_custom_playlist_on; }
int* user_order() { return g_user_order.data(); }
constexpr int kUserOrderSize = 600;
int& user_track() { return memory::bits_as<int>(g_user_track); }
int& user_position() { return memory::bits_as<int>(g_user_position); }
bool shuffle_user() { return g_shuffle_user; }

// 1 << n as the C runtime's 64-bit shift (__allshl) does it: by n's low byte, 0 from 64 up.
uint64_t get_bit(int n)
{
    auto shift = static_cast<uint8_t>(n);
    return shift >= 64 ? 0 : uint64_t{1} << shift;
}
bool is_off(const Playlist& list, int track)
{
    return track < 64 ? (get_bit(track) & core::at(list.off, 0)) != 0 : (get_bit(track - 64) & core::at(list.off, 1)) != 0;
}

// The track's start: the stream played, and the script told which track it is.
void start_track(Playlist& list, int track)
{
    play_track(raw::chars(core::at(list.tracks, track).data()), false);
    current_track() = track;
    script::Struct* params = script::spawn_param_bag();
    params = params ? new (params) script::Struct : nullptr;
    params->put_int(0x8D02705Du, current_track());
    script::launch_script(0x300CAD34u, params, nullptr, false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// The volumes (0-100): the music's, and that of streams played on the music's channel.
float& current_music_volume() { return g_music_volume; }
float& music_stream_volume() { return g_music_stream_volume; }
// What the music's channel plays: 0 a track, 1 a music stream (0x00450EF0), 2 another kind of stream (0x004509E0).
int& music_channel_kind() { return g_bgm_stream_kind; }
// Set by the music's initialisation (0x00450D60).
bool initialised() { return g_initialised; }
bool& music_halted() { return g_bgm_held; }
bool& music_paused_before() { return g_music_paused_before; }
bool& music_header_loaded() { return g_music_header_loaded; }
uint8_t& random_order() { return g_random_order; }
bool& shuffle_user_flag() { return g_shuffle_user; }
// Playlist 0's track titles, by the track's index (100 characters each).
char* get_title(int track) { return core::at(g_track_titles, track).data(); }
// A global empty_queue clears (just past the three streams; nothing else uses it).
int& cleared_by_clear_playlist() { return g_cleared_by_clear_playlist; }

// The platform's music: its volume and pause (music 1 / streams 0).
void set_platform_volume(float volume) { platform_set_music_volume(volume); }
void platform_pause(bool pause, int music) { pause_streams(pause, music); }
// The sound effects manager's volume (the original calls it as a method of 0x006F85A8; it reads a global).
float sfx_volume() { return sound::fetch_master_loudness(); }
// 0x005B9760 (the shared `return true` stub): whether the file is there (the PS2's check, nothing on the PC).
bool file_there(const char*, int) { return stubs::return_true(); }
// A script integer that's read and ignored: what used it is compiled out (debug output).
void read_debug_flag() { script::fetch_int_value(0xD7BB618Du); }
// Set: playlists other than 0 play at the music volume, not the sound effects one.
bool other_lists_at_music_volume() { return script::fetch_int_value(0x450BAE2Du) != 0; }

bool in_range(float volume) { return !(volume > 100.0f) && !(volume < 0.0f); } // (NaN passes)

} // namespace

// 0x00450F70
void pause_music(int pause)
{
    if (!music_on())
        return;
    if (pause == -1) {
        if (music_paused_before())
            return;
        pause = 0;
    }
    if (pause == 0) {
        read_debug_flag();
        music_halted() = false;
        platform_pause(false, 1);
    } else if (pause == 1) {
        read_debug_flag();
        music_paused_before() = music_halted();
        music_halted() = true;
        platform_pause(true, 1);
    }
}

// 0x004510B0
void set_music_volume(float volume)
{
    if (!music_on() || !in_range(volume))
        return;
    current_music_volume() = volume;
    if (!initialised() || music_channel_kind() != 0)
        return;
    if (current_playlist() != 0 && !other_lists_at_music_volume())
        return;
    set_platform_volume(volume);
}

// 0x00451130
void set_music_stream_volume(float volume)
{
    if (!music_on() || !in_range(volume))
        return;
    music_stream_volume() = volume;
    if (initialised() && music_channel_kind() == 1)
        set_platform_volume(volume);
}

// 0x004511A0
void queue_track(const char* name, int list, const char* title)
{
    if (!music_on())
        return;
    Playlist& playlist = core::at(get_playlists(), list);
    if (playlist.count >= 80)
        return;
    char* track = raw::chars(core::at(playlist.tracks, playlist.count).data());
    THUG2_MSVC_WARNING(suppress : 4996) // (the slot is 40 bytes; names are shorter)
    std::strcpy(track, name);
    for (size_t i = 0; i < std::strlen(track); ++i) {
        auto c = static_cast<signed char>(track[i]);
        if (c >= 'a' && c <= 'z')
            track[i] = static_cast<char>(c - 0x20);
    }
    if (list == 0) {
        char* slot = get_title(playlist.count);
        if (title) {
            THUG2_MSVC_WARNING(suppress : 4996) // (terminated by hand, as in the original)
            std::strncpy(slot, title, 100);
            slot[99] = 0;
        } else {
            std::memcpy(slot, "No title provided", 18); // "No title provided"
        }
    }
    if (file_there(track, 1)) {
        playlist.unknownca8_ = 0;
        ++playlist.count;
    }
}

// 0x004512D0
void empty_queue(int list)
{
    if (!music_on())
        return;
    Playlist& playlist = core::at(get_playlists(), list);
    playlist.count = 0;
    playlist.unknownca8_ = 1;
    core::at(playlist.off, 0) = 0;
    core::at(playlist.off, 1) = 0;
    core::at(playlist.played, 0) = 0;
    core::at(playlist.played, 1) = 0;
    cleared_by_clear_playlist() = 0;
}

// 0x00451350
void skip_music_track()
{
    if (music_on() && current_playlist() == 0)
        stop_music();
}

// 0x00451380
void set_random_mode(int mode)
{
    if (!music_on())
        return;
    random_order() = mode != 0;
    shuffle_user_flag() = mode > 0;
    if (mode != 0) {
        shuffle_user_soundtrack();
        user_position() = 0;
        user_track() = user_order()[0];
    }
    Playlist& playlist = core::at(get_playlists(), current_playlist());
    core::at(playlist.played, 0) = 0;
    core::at(playlist.played, 1) = 0;
}

// 0x00451400
void apply_track_volume()
{
    const bool at_music_volume = other_lists_at_music_volume();
    if (custom_soundtrack_active())
        set_platform_volume(current_music_volume());
    else if (current_playlist() == 1 && !at_music_volume)
        set_platform_volume(sfx_volume());
}

// 0x00451450
bool is_track_playing(int list, int track)
{
    if (!music_on() || music_status() == 1) // 1: no stream playing
        return false;
    if (track >= core::at(get_playlists(), list).count)
        return false;
    const uint32_t name = checksums::hash_from_text(raw::chars(core::at(core::at(get_playlists(), list).tracks, track).data()));
    return checksums::hash_from_text(static_cast<const char*>(special_track())) == name;
}

// 0x004514C0
int track_total(int list) { return music_on() ? core::at(get_playlists(), list).count : 0; }

// 0x00451500
const char* track_label_at(int track, int list)
{
    if (!music_on()) {
        char* first = raw::chars(core::at(core::at(get_playlists(), 0).tracks, 0).data());
        std::memcpy(first, "Empty.", 7); // "Empty."
        return first;
    }
    const char* name = raw::chars(core::at(core::at(get_playlists(), list).tracks, track).data());
    const char* file = name;
    const int length = static_cast<int>(std::strlen(name));
    for (int i = 0; i < length; ++i)
        if (name[i] == '\\' || name[i] == '/')
            file = name + i + 1;
    return file;
}

// 0x004515A0
void set_music_mode(int list)
{
    if (!music_on())
        return;
    if (current_playlist() != list)
        stop_music();
    current_playlist() = list;
}

// 0x004515E0
bool load_music_header(const char* name)
{
    if (!music_on())
        return false;
    music_header_loaded() = file_there(name, 0) != 0;
    return music_header_loaded();
}

// 0x00451C70
void change_track_state(int track, bool off, int list)
{
    if (!music_on())
        return;
    Playlist& playlist = core::at(get_playlists(), list);
    reshuffle() = true;
    if (!off) {
        if (track < 64)
            core::at(playlist.off, 0) &= ~get_bit(track);
        else
            core::at(playlist.off, 1) &= ~get_bit(track - 64);
        playlist.unknownca8_ = 0;
        return;
    }
    if (track < 64)
        core::at(playlist.off, 0) |= get_bit(track);
    else
        core::at(playlist.off, 1) |= get_bit(track - 64);
    if (is_track_playing(list, track))
        stop_music();
    // Original quirk: every track is tested against both masks (1 << (i - 64) is 0 for i < 64), so any track at
    // all counts as one that's on - only an empty playlist is marked.
    const uint64_t low = core::at(playlist.off, 0);
    for (int i = 0; i < playlist.count; ++i) {
        if ((get_bit(i) & low) == 0 || (get_bit(i - 64) & core::at(playlist.off, 1)) == 0) {
            playlist.unknownca8_ = 0;
            return;
        }
    }
    playlist.unknownca8_ = 1;
}

// 0x004520D0
void play_track(const char* name, bool loop)
{
    if (!music_on() || music_channel_kind() != 0)
        return;
    char* playing = static_cast<char*>(special_track());
    THUG2_MSVC_WARNING(suppress : 4996) // (40 bytes, as in the original)
    std::strcpy(playing, name);
    const int list = current_playlist();
    const bool at_music_volume = other_lists_at_music_volume();
    const float volume = list != 0 && !at_music_volume ? sfx_volume() : current_music_volume();
    get_looping() = loop;
    if (play_track_by_name(playing, volume) || game_config::g_from_cd) // (the debug checks)
        return;
    // It doesn't play: switch it off.
    Playlist& playlist = core::at(get_playlists(), list);
    for (int i = 0; i < playlist.count; ++i) {
        const uint32_t crc = checksums::hash_from_text(raw::chars(core::at(playlist.tracks, i).data()));
        if (checksums::hash_from_text(name) == crc) {
            change_track_state(i, true, list);
            return;
        }
    }
}

// PartyMod's our_random, at the shuffle's two draws (0x004523A7 / 0x004523B4). It keeps drawing the
// game's number so the game's random stream advances as before. (rand() % 0 would divide by zero; the game's
// Rnd(0) is 0, so that's kept for an empty order.)
int shuffle_random(int n)
{
    const int game = maths::rnd(n);
    if (n == 0)
        return game;
    // NOLINTNEXTLINE(misc-predictable-rand): the game's own rand(), as the original
    return std::rand() % n;
}

// 0x00452200
void play_next_track()
{
    Playlist& list = core::at(get_playlists(), current_playlist());
    if (!music_on() || list.unknownca8_)
        return;
    if (music_status() != 1) // 1: no stream playing
        return;
    g_bgm_stream_kind = 0;
    apply_track_volume(); // the music volume
    if (current_playlist() == 1) {
        const int track = script::fetch_int_value(0xF18AB1F9u);
        if (track < core::at(get_playlists(), 1).count)
            play_track(raw::chars(core::at(core::at(get_playlists(), 1).tracks, track).data()), false);
        return;
    }
    if (get_looping()) {
        play_track(static_cast<const char*>(special_track()), true);
        return;
    }
    if (list.count == 0)
        return;

    int position = 0;
    if (reshuffle()) {
        // (an empty order: the original reads the word before it, which is the movie frame dump counter - by name
        // here, as our globals aren't laid out as the original's)
        const int last = order_count() > 0 ? get_order()[order_count() - 1] : render::g_movie_dump_count;
        order_count() = 0;
        int count = 0;
        for (int track = 0; track < list.count; ++track)
            if (!is_off(list, track))
                get_order()[count++] = track;
        order_count() = count;
        debug::printf("shuffling random song order num_songs=%i\n",
                      count); // "shuffling random song order num_songs=%i\n"
        // 2000 swaps of two random places. PartyMod draws them with shuffle_random; the original with
        // maths::Rnd(order_count()).
        for (int i = 0; i < 2000; ++i) {
            const int a = shuffle_random(order_count());
            const int b = shuffle_random(order_count());
            if (a != b) {
                const int swap = get_order()[a];
                get_order()[a] = get_order()[b];
                get_order()[b] = swap;
            }
        }
        // Not the last one again first: swap it with another place (this draw is the game's, under PartyMod too).
        if (get_order()[0] == last) {
            const int other = order_count() > 1 ? maths::rnd(order_count() - 1) + 1 : 0;
            const int swap = get_order()[0];
            get_order()[0] = get_order()[other];
            get_order()[other] = swap;
        }
        order_position() = 0;
        reshuffle() = false;
        core::at(list.played, 0) = 0;
        core::at(list.played, 1) = 0;
        position = 0;
    } else {
        position = ++order_position();
        if (position >= order_count() - 1)
            reshuffle() = true;
    }
    if (list.count <= 0)
        return;
    // The track at that position, unless it's off or already played (then nothing). (The original repeats
    // this same test list.count times.)
    const int track = get_order()[position];
    if ((get_bit(track) & core::at(list.off, 0)) == 0 && track < 64 && (get_bit(track) & core::at(list.played, 0)) == 0) {
        core::at(list.played, 0) |= get_bit(track);
        start_track(list, track);
    } else if ((get_bit(track - 64) & core::at(list.off, 1)) == 0 && track >= 64 && (get_bit(track - 64) & core::at(list.played, 1)) == 0) {
        core::at(list.played, 1) |= get_bit(track - 64);
        start_track(list, track);
    }
}

// 0x00452680
void play_next_track_in_order()
{
    if (!music_on())
        return;
    Playlist& list = core::at(get_playlists(), current_playlist());
    if (list.unknownca8_)
        return;
    if (music_status() != 1) // 1: no stream playing
        return;
    g_bgm_stream_kind = 0;
    apply_track_volume(); // the music volume
    if (get_looping()) {
        play_track(static_cast<const char*>(special_track()), true);
        return;
    }
    for (int track = 0; track < list.count; ++track) {
        if ((get_bit(track) & core::at(list.off, 0)) == 0 && track < 64 && (get_bit(track) & core::at(list.played, 0)) == 0) {
            core::at(list.played, 0) |= get_bit(track);
            start_track(list, track);
            return;
        }
        if ((get_bit(track - 64) & core::at(list.off, 1)) == 0 && track >= 64 && (get_bit(track - 64) & core::at(list.played, 1)) == 0) {
            core::at(list.played, 1) |= get_bit(track - 64);
            start_track(list, track);
            return;
        }
    }
    core::at(list.played, 0) = 0;
    core::at(list.played, 1) = 0;
}

// 0x00452910
void tick_frame()
{
    if (!g_sound_device || game_config::g_platform == 2)
        return;
    if (!g_music_header_loaded && !g_streams_on)
        return;
    const int streams = update_streams(); // the stream system's update
    if (streams != 0) {
        if (streams_on())
            halt_streams();
        if (streams < 0)
            stop_music(); // pause the music
    }
    ++frame_count();
    for (int i = 0; i < kStreams; ++i) {
        objects::AudioStreamModule*& owner = core::at(stream_owners(), i);
        if (owner && !update_stream_volume(i)) { // (false: the stream is over)
            core::at(owner->streams_, i) = 0;
            owner = nullptr;
        }
    }
    for (int i = 0; i < kStreams; ++i) {
        Stream& stream = get_streams()[i];
        if (stream.listener && !get_playing(stream)) {
            if (stream.listener[0]) {
                stream.listener[0] = 0;
                stream.listener[1] = 0;
            }
            stream.listener = nullptr;
        }
    }
    if (frame_count() & 0x3F)
        return;
    const int playlist = current_playlist();
    if (g_music_volume == 0.0f && playlist != 1) // (the music volume)
        return;
    if (music_is_paused())
        return;
    if (!random_track_order() && playlist != 1)
        play_next_track_in_order();
    else
        play_next_track();
}

// 0x00450220
bool stream_id_finished(uint32_t id)
{
    for (int i = 0; i < kStreams; ++i)
        if (get_streams()[i].id == id && get_playing(get_streams()[i]))
            return false;
    return true;
}

// 0x00450260
int find_playing_stream(uint32_t id_or_name)
{
    for (int i = 0; i < kStreams; ++i)
        if (get_streams()[i].id == id_or_name && get_playing(get_streams()[i]))
            return i;
    for (int i = 0; i < kStreams; ++i)
        if (get_streams()[i].name == id_or_name && get_playing(get_streams()[i]))
            return i;
    return -1;
}

// 0x004502D0
uint32_t free_stream_id(uint32_t id)
{
    for (;; ++id) {
        bool used = false;
        for (int i = 0; i < kStreams && !used; ++i)
            used = get_streams()[i].id == id && get_playing(get_streams()[i]);
        if (!used)
            return id;
    }
}

// 0x00450310 (with the game's random numbers: PartyMod's shuffle change is only in play_next_track's)
void shuffle_user_soundtrack()
{
    int count = user_soundtrack_tracks(user_soundtrack()); // the soundtrack's tracks
    if (count == 0)
        return;
    if (count > kUserOrderSize)
        count = kUserOrderSize;
    const int last = user_order()[count - 1]; // (the last of the order before - not the track played last)
    for (int i = 0; i < count; ++i)
        user_order()[i] = i;
    for (int i = 0; i < 2000; ++i) {
        const int a = maths::rnd(count);
        const int b = maths::rnd(count);
        const int swap = user_order()[a];
        user_order()[a] = user_order()[b];
        user_order()[b] = swap;
    }
    if (user_order()[0] == last) {
        const int other = count > 1 ? maths::rnd(count - 1) + 1 : 0;
        const int swap = user_order()[0];
        user_order()[0] = user_order()[other];
        user_order()[other] = swap;
    }
}

// 0x004503D0
void use_user_soundtrack(int soundtrack)
{
    user_soundtrack() = soundtrack;
    custom_soundtrack_active() = true;
    if (shuffle_user()) {
        shuffle_user_soundtrack();
        user_position() = 0;
        user_track() = user_order()[0];
    } else {
        user_track() = 0;
    }
}

// 0x00450410
void use_standard_soundtrack() { custom_soundtrack_active() = false; }

// 0x00450420
void stop_user_soundtrack() { stop_stream_thread(); }

// 0x004B2350
int get_num_soundtracks() { return user_soundtrack_count(); }

// 0x004B2360
const char* get_soundtrack_name(int index)
{
    char* name = g_soundtrack_name.data();
    const wchar_t* wide = user_soundtrack_name(index); // (null past the last)
    if (wide) {
        char* out = name;
        int length = 0;
        for (; *wide; ++wide) {
            std::array<wchar_t, 2> one = {{*wide, 0}};
            std::array<char, 12> converted{};
            wsprintfA(converted.data(), "%ls", one.data());
            if (length < 99) {
                *out++ = core::at(converted, 0) ? core::at(converted, 0) : '~';
                ++length;
            }
        }
        *out = 0;
    } else {
        std::memcpy(name, "~", 2);
    }
    const int length = static_cast<int>(std::strlen(name));
    for (int i = 0; i < length; ++i) {
        auto c = static_cast<signed char>(name[i]);
        if (c >= static_cast<signed char>(0xA1) && c <= static_cast<signed char>(0xAC))
            name[i] = '~';
    }
    return name;
}

// 0x00450430
bool music_is_paused() { return g_bgm_held; }

// 0x00450440
float get_music_volume() { return g_music_volume; }

// 0x00450450
float get_sfx_volume() { return g_music_stream_volume; }

// 0x00450460
int random_track_order() { return g_random_order; }

// 0x00450470
void set_music_looping(bool loop) { get_looping() = loop; }

// 0x00450480
void get_tracks_off(uint64_t* low, uint64_t* high)
{
    *low = core::at(core::at(get_playlists(), 0).off, 0);
    *high = core::at(core::at(get_playlists(), 0).off, 1);
}

// 0x004504B0
void set_tracks_off(uint64_t low, uint64_t high)
{
    core::at(core::at(get_playlists(), 0).off, 0) = low;
    core::at(core::at(get_playlists(), 0).off, 1) = high;
}

// 0x004504E0
void set_user_soundtrack_volume(float volume)
{
    if (current_playlist() == 1)
        platform_set_music_volume(volume);
}

} // namespace audio
