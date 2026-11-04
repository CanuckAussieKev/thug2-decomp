// The script commands of the music and streams, 0x0044D100-0x0044D7C7 (see music_cfuncs.h).

#include "engine/audio/music_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <gsl/assert>

#include "engine/core/dbg.h"
#include "engine/core/format.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/audio/continue_music.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace commands {

namespace {
constexpr int kTracksPerMask = 64;     // a playlist's masks are two 64-bit words
constexpr int kNoTrack = 999;          // the track number before a name is looked up
constexpr size_t kTrackPathSize = 0x28;
constexpr int kDefaultPriority = 50;

using script::Script;
using script::Struct;

constexpr uint32_t kAdd = 0x02E58C18;          // "add"
constexpr uint32_t kRemove = 0x977FE2CF;       // "remove"
constexpr uint32_t kVol = 0xF6A36814;          // "vol"
constexpr uint32_t kLoop = 0x5EA0E211;         // "loop"
constexpr uint32_t kTrackTitle = 0x14BADF8A;   // "TrackTitle"
constexpr uint32_t kPlaylist0 = 0x389129E4;    // (name unknown) AddMusicTrack / ClearMusicTrackList: playlist 0, not 1
constexpr uint32_t kVolL = 0x5E285A66;         // "volL"
constexpr uint32_t kVolR = 0xA4276705;         // "volR"
constexpr uint32_t kPitch = 0xD8604126;        // "pitch"
constexpr uint32_t kPriority = 0x9D5923D8;     // "priority"
constexpr uint32_t kStreamOption = 0xE46111E0; // (name unknown) PlayStream: passed on to the platform
constexpr uint32_t kId = 0x40C698AF;           // "id"
constexpr uint32_t kLipsync = 0xF5C3922B;      // "lipsync"
constexpr uint32_t kCurrentTrack = 0x8D02705D; // "current_track"
constexpr uint32_t kStreamId = 0x8A68AB90;     // "streamid"
constexpr uint32_t kVolume = 0x46653221;       // "volume"

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

// A 64-bit mask with bit `bit` set, as the C runtime's _allshl (0x00623CF0) makes it from the count's low byte:
// 0 from 64 on (a negative track's bit too).
uint64_t track_bit(int bit)
{
    auto count = static_cast<uint8_t>(bit);
    return count >= kTracksPerMask ? 0 : uint64_t{1} << count;
}

} // namespace

// 0x0044D100: StreamExists <name>: whether the stream index has it. False without a name.
bool cfunc_stream_exists(Struct* params, Script* /*script*/)
{
    uint32_t name = 0;
    return params->read_hash_value(0U, &name, 0) && audio::stream_exists(name);
}

// 0x0044D140: AddStream <stream> add=<name>: the playing stream (by id or name) also known by the name. False
// without both or if it isn't playing.
bool cfunc_add_stream(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    uint32_t name = 0;
    if (!params->read_hash_value(kAdd, &name, 0)) {
        return false;
    }
    return audio::add_stream({.stream = stream, .name = name}, nullptr);
}

// 0x0044D1B0: RemoveStream <stream> remove=<entry>: the stream's queue drops that entry (if first). False without
// both or if the stream isn't playing.
bool cfunc_remove_stream(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    uint32_t entry = 0;
    if (!params->read_hash_value(kRemove, &entry, 0)) {
        return false;
    }
    return audio::remove_stream({.id_or_name_ = stream, .entry = entry}, nullptr);
}

// 0x0044D220: halt_stream [<id or name>]: that stream stopped, or every stream. Always true.
bool cfunc_stop_stream(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0; // (uninitialised in the original: only read when found)
    if (params->read_hash_value(0U, &stream, 0)) {
        audio::halt_stream(stream);
    } else {
        audio::halt_streams(-1);
    }
    return true;
}

// 0x0044D260: IsStreamPlaying <id or name>. False without one.
bool cfunc_is_stream_playing(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    return audio::is_stream_playing(stream);
}

// 0x0044D2A0: PlayTrack <file> | <track number> [loop=n]: the music stopped and the track played (looping for a
// non-zero loop); a number names playlist 0's track, as MUSIC\VAG\SONGS\<its name>. The current track becomes the
// number (999 for a file name). Always true. (Original quirk: a song name of 22 characters or more overruns the
// 40-byte buffer.)
bool cfunc_play_track(Struct* params, Script* /*script*/)
{
    const char* name = nullptr;
    int track = kNoTrack;
    int loop = 0;
    std::array<char, kTrackPathSize> path{};
    if (!params->fetch_text(0U, &name, 0)) {
        params->fetch_int_value(0U, &track, 0);
        name = audio::track_label_at(track, 0);
        get_sprintf(path.data(), R"(MUSIC\VAG\SONGS\%s)", name); // "MUSIC\VAG\SONGS\%s"
        name = path.data();
    }
    params->fetch_int_value(kLoop, &loop, 0);
    if (name) {
        debug::printf("PlayTrack songName ===================== %s\n", name); // "PlayTrack songName ==... %s"
        audio::stop_music();
        audio::play_track(name, loop != 0);
        audio::pick_track(track);
    }
    return true;
}

// 0x0044D380: PlayMusicStream <name> [vol=n (-1: the default)]. False without a name.
bool cfunc_play_music_stream(Struct* params, Script* /*script*/)
{
    uint32_t name = 0;
    if (!params->read_hash_value(0U, &name, 0)) {
        return false;
    }
    float volume = -1.0F;
    params->fetch_float(kVol, &volume, 0);
    audio::play_music_stream(name, volume);
    return true;
}

// 0x0044D3E0: LoadStreamFrameAmp <stream>: whether its frame amplitudes loaded. False without a name.
bool cfunc_load_stream_frame_amp(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    return audio::load_stream_frame_amp(stream) != nullptr;
}

// 0x0044D420: FreeStreamFrameAmp <stream>: whether they were loaded (and are now freed). False without a name.
bool cfunc_free_stream_frame_amp(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    return audio::free_stream_frame_amp(stream);
}

// 0x0044D460: SetMusicVolume [n (100)]. Always true.
bool cfunc_set_music_volume(Struct* params, Script* /*script*/)
{
    float volume = 100.0F;
    params->fetch_float(0U, &volume, 0);
    audio::set_music_volume(volume);
    return true;
}

// 0x0044D490: SetMusicStreamVolume [n (100)]. Always true.
bool cfunc_set_music_stream_volume(Struct* params, Script* /*script*/)
{
    float volume = 100.0F;
    params->fetch_float(0U, &volume, 0);
    audio::set_music_stream_volume(volume);
    return true;
}

// 0x0044D4C0: StopMusic. Always true.
bool cfunc_stop_music(Struct* /*params*/, Script* script)
{
    if (audio::continue_music::skips_script_stop(script)) { // NOT ORIGINAL - continue_music: not at a level change
        return true;
    }
    audio::stop_music();
    return true;
}

// 0x0044D4D0: PauseMusic [n (1)]: 1 pauses, 0 unpauses, -1 unpauses unless paused before. Always true.
bool cfunc_pause_music(Struct* params, Script* script)
{
    int pause = 1;
    params->fetch_int_value(0U, &pause, 0);
    if (pause == 1 &&
        audio::continue_music::skips_script_pause(script)) { // NOT ORIGINAL - continue_music: not in menus
        return true;
    }
    audio::pause_music(pause);
    return true;
}

// 0x0044D500: freeze_stream [n (1)]: as PauseMusic, for the streams. Always true.
bool cfunc_pause_stream(Struct* params, Script* /*script*/)
{
    int pause = 1;
    params->fetch_int_value(0U, &pause, 0);
    audio::freeze_stream(pause);
    return true;
}

// 0x0044D530: AddMusicTrack <file> [TrackTitle=<text>] [0x389129E4]: the track added to playlist 1 (or 0 with the
// flag). Nothing without a file. Always true.
bool cfunc_add_music_track(Struct* params, Script* /*script*/)
{
    const char* name = nullptr;
    const char* title = nullptr;
    params->fetch_text(0U, &name, 0);
    params->fetch_text(kTrackTitle, &title, 0);
    const int list = params->has_bare_flag(kPlaylist0) ? 0 : 1;
    if (name) {
        audio::queue_track(name, list, title);
    }
    return true;
}

// 0x0044D5B0: ChangeTrackState <track> [Off]: playlist 0's track switched off (with Off) or on. Always true.
bool cfunc_change_track_state(Struct* params, Script* /*script*/)
{
    int track = 0;
    params->fetch_int_value(0U, &track, 0);
    const bool off = params->has_bare_flag("Off"); // "Off"
    audio::change_track_state(track, off, 0);
    return true;
}

// 0x0044D5F0: TrackEnabled <track>: whether playlist 0's track isn't switched off. False with no tracks.
bool cfunc_track_enabled(Struct* params, Script* /*script*/)
{
    int track = 0;
    params->fetch_int_value(0U, &track, 0);
    if (audio::track_total(0) == 0) {
        return false;
    }
    uint64_t low = 0;
    uint64_t high = 0;
    audio::get_tracks_off(&low, &high);
    if (track < kTracksPerMask) {
        return (track_bit(track) & low) == 0;
    }
    return (track_bit(track - kTracksPerMask) & high) == 0;
}

// 0x0044D690: ClearMusicTrackList [0x389129E4]: playlist 1 (or 0 with the flag) emptied. Always true.
bool cfunc_clear_music_track_list(Struct* params, Script* /*script*/)
{
    audio::empty_queue(params->has_bare_flag(kPlaylist0) ? 0 : 1);
    return true;
}

// 0x0044D6C0: SkipMusicTrack. Always true.
bool cfunc_skip_music_track(Struct* /*params*/, Script* script)
{
    if (audio::continue_music::skips_script_stop(script)) { // NOT ORIGINAL - continue_music: not at a run start
        return true;
    }
    audio::skip_music_track();
    return true;
}

// 0x0044D6D0: SetMusicMode [n (1)]: play from playlist 0 for a non-zero n, else from playlist 1. Always true.
bool cfunc_set_music_mode(Struct* params, Script* /*script*/)
{
    int mode = 1;
    params->fetch_int_value(0U, &mode, 0);
    audio::set_music_mode(mode == 0 ? 1 : 0);
    return true;
}

// 0x0044D710: SetRandomMode [n (1)]. Always true.
bool cfunc_set_random_mode(Struct* params, Script* /*script*/)
{
    int mode = 1;
    params->fetch_int_value(0U, &mode, 0);
    audio::set_random_mode(mode);
    return true;
}

// 0x0044D740: SetMusicLooping [n (1)]. Always true.
bool cfunc_set_music_looping(Struct* params, Script* /*script*/)
{
    int loop = 1;
    params->fetch_int_value(0U, &loop, 0);
    audio::set_music_looping(loop != 0);
    return true;
}

// 0x0044D770: PreLoadStreamDone <id or name>: whether its preload is done. (Original quirk: no name leaves the id
// uninitialised; here 0.)
bool cfunc_pre_load_stream_done(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    params->read_hash_value(0U, &stream, 1);
    return audio::pre_load_stream_done(stream);
}

// 0x0044D790: UseUserSoundtrack <n>: play the user's soundtrack n. Always true. (Original quirk: no n is
// uninitialised; here 0.)
bool cfunc_use_user_soundtrack(Struct* params, Script* /*script*/)
{
    int soundtrack = 0;
    params->fetch_int_value(0U, &soundtrack, 1);
    audio::use_user_soundtrack(soundtrack);
    return true;
}

// 0x0044D7C0: UseStandardSoundtrack: back to the game's own music. Always true.
bool cfunc_use_standard_soundtrack(Struct* /*params*/, Script* /*script*/)
{
    audio::use_standard_soundtrack();
    return true;
}

// 0x0044D8F0: PlayStream <stream> [volL= volR= | vol= (100)] [pitch=100] [priority=50] [0xE46111E0=n] [id=<id>]
// [lipsync=1 (its frame amplitudes loaded first)]: the stream played (stereo). False without a stream, or when
// lipsync's amplitudes can't be loaded; else true (whether it plays or not).
bool cfunc_play_stream(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    float vol = 100.0F;
    float left = 100.0F;
    float right = 100.0F;
    float pitch = 100.0F;
    int priority = kDefaultPriority;
    int option = 0;
    uint32_t id = 0;
    int lipsync = 0;
    params->fetch_float(kVolL, &left, 0);
    params->fetch_float(kVolR, &right, 0);
    if (params->fetch_float(kVol, &vol, 0)) {
        left = vol;
        right = vol;
    }
    params->fetch_float(kPitch, &pitch, 0);
    params->fetch_int_value(kPriority, &priority, 0);
    params->fetch_int_value(kStreamOption, &option, 0);
    params->read_hash_value(kId, &id, 0);
    params->fetch_int_value(kLipsync, &lipsync, 0);
    if (lipsync == 1 && !audio::load_stream_frame_amp(stream)) {
        return false;
    }
    sound::VolumeRef volume;
    volume.change_species(sound::VolumeRef::kTypeStereo); // (the channels as they were, the first two set below)
    core::at(volume.lanes(), 0) = left;
    core::at(volume.lanes(), 1) = right;
    audio::play_stream(stream, &volume, pitch, option, priority, id); // (the original pushes a seventh argument, 1)
    return true;
}

// 0x0044DAA0: SetStreamParams <id or name> [volL= volR= | vol=] [pitch=]: the playing stream's volume (if any is
// given: the manager's default type, the channels set) and pitch (if given). False without a stream or when
// either change fails.
bool cfunc_set_stream_params(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0U, &stream, 0)) {
        return false;
    }
    float left = 0.0F; // (uninitialised in the original: a channel not given passes on whatever was there)
    float right = 0.0F;
    float vol = 0.0F;
    const bool got_left = params->fetch_float(kVolL, &left, 0);
    bool got_volume = params->fetch_float(kVolR, &right, 0) || got_left;
    if (params->fetch_float(kVol, &vol, 0)) {
        left = vol;
        right = vol;
        got_volume = true;
    }
    bool ok = true;
    if (got_volume) {
        sound::VolumeRef volume;
        volume.change_muted();
        core::at(volume.lanes(), 0) = left;
        core::at(volume.lanes(), 1) = right;
        ok = audio::change_stream_loudness(stream, &volume);
    }
    float pitch = 0.0F;
    if (params->fetch_float(kPitch, &pitch, 0)) {
        return audio::set_stream_pitch(stream, pitch) && ok;
    }
    return ok;
}

// 0x0044E120 / 0x0044E180: LoadMusicHeader / LoadStreamHeader <file>: loaded from the bottom-up heap; whether it
// was. False without a file.
bool cfunc_load_music_header(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    if (!file) {
        return false;
    }
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->bottom_up_heap());
    const bool loaded = audio::load_music_header(file);
    memory::Hub::get_pull()->leave_heap_scope();
    return loaded;
}
bool cfunc_load_stream_header(Struct* params, Script* /*script*/)
{
    const char* file = nullptr;
    params->fetch_text(0U, &file, 0);
    if (!file) {
        return false;
    }
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->bottom_up_heap());
    const bool loaded = audio::load_stream_header(file);
    memory::Hub::get_pull()->leave_heap_scope();
    return loaded;
}

// 0x0044E1E0: GetCurrentTrack: the track playing as the script's current_track. Always true.
bool cfunc_get_current_track(Struct* /*params*/, Script* script)
{
    script->args()->put_int(checksums::Name{kCurrentTrack}, audio::get_current_track());
    return true;
}

// 0x0044E2C0: StartPreLoadedStream streamid=<id> [volume=100]: the preloaded stream started (pitch 100, priority
// 0) at that volume on both channels; whether it did. (Original quirk: no streamid passes an uninitialised one;
// here 0.)
bool cfunc_start_pre_loaded_stream(Struct* params, Script* /*script*/)
{
    uint32_t stream = 0;
    params->read_hash_value(kStreamId, &stream, 1);
    float level = 100.0F;
    params->fetch_float(kVolume, &level, 0);
    gsl::owner<sound::VolumeRef*> volume = memory::adopt<sound::VolumeRef*>(memory::alloc(sizeof(sound::VolumeRef)));
    if (volume) {
        volume = new (volume) sound::VolumeRef;
    }
    Expects(volume != nullptr);
    core::at(volume->lanes(), 0) = level;
    core::at(volume->lanes(), 1) = level;
    const bool started = audio::start_pre_loaded_stream(stream, volume, {.pitch = 100.0F, .priority = 0});
    memory::Hub::dealloc(volume);
    return started;
}

} // namespace commands
