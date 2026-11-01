#pragma once

#include <cstddef>
#include <cstdint>

struct _OVERLAPPED;

namespace render {
struct Bink;
}

// The PC's streamed audio (the platform layer under Pcm's music and streams): the music and three sound
// streams, each played by a Bink file through binkw32's DirectSound output, serviced by a thread every 5 ms.
// The pcm.wad stream class left from the console versions (WadStream) is still created in places, but its
// playing is compiled out on PC (its update, volume and "done" calls are linker-folded stubs).
namespace audio {

using Hdl = void*;   // a Win32 HANDLE
using Result = long; // an HRESULT: 0 or an error (E_INVALIDARG 0x80070057, HRESULT_FROM_WIN32(...))

// A raw stream from pcm.wad (0xA8 bytes): reads the file in 8 KB pieces with overlapped ReadFile. On PC nothing
// opens one with a valid file (see 0x00453850), so only the set-up and the flags are live.
struct WadStream {
    // 0x00454340: a closed stream (file -1); `unknown15` kept; unknown5f_ set.
    explicit WadStream(bool unknown15);
    // 0x00454490: start reading `file` (the whole file) into `buffer`. HRESULT_FROM_WIN32 of the last error
    // for an invalid handle.
    Result unseal(Hdl file, void* buffer);
    // 0x00454450: start reading `size` bytes of `wad` at `offset` into `buffer`.
    Result open_from_wad(Hdl wad, uint32_t offset, uint32_t size, void* buffer);
    // 0x00454370: the first 8 KB read requested (overlapped): 0 if it's done or pending, else
    // HRESULT_FROM_WIN32 of the error (an error <= 0 as it is).
    Result start_read();
    // 0x004543F0: whether the preload (state 1; any other state: false) is far enough: the read done at once,
    // or 8 KB loaded ahead.
    bool prefetch_complete();
    // 0x00454430: pause (non-zero) or resume.
    void get_freeze(int pause);
    // 0x00454440 (empty; also called on BinkStreams - linker-folded): the five channels' volumes.
    void set_channel_volumes(float front_left, float front_right, float center, float rear_left, float rear_right);

    // Linker-folded stubs on PC: 0x005BA1D0 (the thread's update: 0), 0x005BAB90 (the volume), 0x0048DCD0
    // (volume and pan), 0x005B9760 (ready to be freed: true).
    int tick_frame() { return 0; }
    void change_loudness(float) {}
    void set_volume_and_pan(float, float) {}
    bool ready_to_free() { return true; }
    // The destructor: empty (a call to 0x005480A0).
    void destruct() {}

    // The status word status_index_ picks (E_PENDING 0x8000000A: still going).
    uint32_t get_standing() const { return cur_standing_[status_index_]; }

    Hdl file_ref_;           // +0x00 INVALID_HANDLE_VALUE when closed
    uint32_t unknown04_;     // +0x04
    _OVERLAPPED* async_ctl_; // +0x08 (from the ring, 0x004535E0; never set for these on PC)
    bool reading_;           // +0x0C
    bool read_done_;         // +0x0D the read finished at once
    uint8_t pad0e_[2];
    uint32_t unknown10_; // +0x10
    bool from_wad_;      // +0x14 read at shift_ (else from the start)
    bool unknown15_;     // +0x15
    uint8_t pad16_[2];
    uint32_t shift_;     // +0x18
    uint32_t extent_;    // +0x1C
    uint32_t unknown20_; // +0x20
    uint32_t unknown24_; // +0x24
    uint8_t unknown28_[4];
    uint32_t cur_standing_[9]; // +0x2C indexed by status_index_ (0x8000000A: still fine)
    uint32_t unknown50_;       // +0x50
    bool is_halted_;           // +0x54
    bool finished_;            // +0x55
    uint8_t pad56_[2];
    int status_index_; // +0x58
    uint8_t unknown5c_[3];
    bool unknown5f_; // +0x5F set by the constructor and the "start" calls, cleared once loaded
    uint8_t unknown60_[0x30];
    uint32_t unknown90_;  // +0x90
    void* octets_;        // +0x94
    int ready_flag_;      // +0x98
    int played_;          // +0x9C
    int cur_phase_;       // +0xA0 1: preloading
    bool stop_requested_; // +0xA4 freed by the update once ready_to_free
    uint8_t pada5_[3];
};
static_assert(offsetof(WadStream, async_ctl_) == 0x08 && offsetof(WadStream, from_wad_) == 0x14 &&
                  offsetof(WadStream, shift_) == 0x18 && offsetof(WadStream, cur_standing_) == 0x2C &&
                  offsetof(WadStream, unknown50_) == 0x50 && offsetof(WadStream, status_index_) == 0x58 &&
                  offsetof(WadStream, unknown5f_) == 0x5F && offsetof(WadStream, octets_) == 0x94 &&
                  offsetof(WadStream, cur_phase_) == 0xA0 && offsetof(WadStream, stop_requested_) == 0xA4 &&
                  sizeof(WadStream) == 0xA8,
              "layout must match the original");

// A Bink file played for its sound (0x1C0 bytes): opened from a path (the music) or from a file handle into
// pcm.wad (the streams; 0x00452C60). Pausing closes the Bink and notes the frame; resuming reopens it there.
struct BinkStream {
    // 0x00452C00 (the constructor; not rewritten here): unknown19 and the number of plays (0: once).
    // 0x00452D20: open the Bink file at `path` (the path kept for reopening): 0, or E_INVALIDARG if it won't
    // open. Bink's sound goes to DirectSound (set up once; "Bink can't use DSound" if that fails).
    Result unseal(const char* path);
    // 0x00452DC0: ask the thread to pause (non-zero) or resume - unless pausing is locked out (0x0069CB98,
    // never set).
    void get_freeze(int pause);
    // 0x00452DE0: the volume (0-100; Bink's 0-32768): silent at 0 or less.
    void change_loudness(float volume);
    // 0x00452FC0: the volume from the left and right levels (their mean is kept as the volume): the louder
    // one's level, at most 32768, and the pan right / (left + right) of 65536.
    void set_volume_and_pan(float left, float right);
    // 0x00452E30 (the destructor): the Bink closed, the file closed unless shared, the wad entry let go of
    // (its last use noted), the memory block freed.
    void destruct();
    // 0x00452EA0 (on the stream thread): pause or resume as asked, then the frames due decoded while the
    // window has focus; the plays counted down at the last frame (finished when they run out). 0.
    int tick_frame();
    // 0x005B9760 (linker-folded `return true`): whether it may be freed now (else it's asked to stop), and
    // whether its preload is far enough to start.
    bool ready_to_free() { return true; }
    bool prefetch_complete() { return true; }
    // 0x00452C00 (bink_stream_files.cpp): the fields cleared (slots -1); unknown19_ and plays_ from the arguments;
    // async_ctl_ the next free stream buffer (0x004535E0).
    BinkStream(bool unknown19, int plays);
    // 0x00452C60: open stream file `key` (load_stream_file) with Bink from memory, video off; S_OK, or E_INVALIDARG
    // if it can't be read or opened. Bink is told to use DirectSound the first time ("Bink can't use DSound").
    long unseal(uint32_t key, const char* path, long offset, size_t size);
    // 0x00454440 (WadStream::set_channel_volumes, linker-folded: empty).
    void set_channel_volumes(float, float, float, float, float) {}

    Hdl file_ref_;           // +0x00 INVALID_HANDLE_VALUE: none
    uint32_t unknown04_;     // +0x04
    _OVERLAPPED* async_ctl_; // +0x08 (from the ring, 0x004535E0)
    int wad_entry_;          // +0x0C the pcm.wad entry open (0x0069CADC), -1: none
    bool silent_;            // +0x10 the volume is 0: not updated
    bool is_halted_;         // +0x11 the pause state carried out
    uint8_t unknown12_[6];
    bool shared_file_; // +0x18 file_ref_ isn't closed with the stream
    bool unknown19_;   // +0x19
    uint8_t unknown1a_[0xA];
    int plays_;                 // +0x24 plays left (counted down at the last frame; 0 after one: finished)
    render::Bink* bink_;        // +0x28 null while paused
    void* memory_;              // +0x2C freed with the stream
    uint32_t unknown30_;        // +0x30
    uint32_t status_words_[11]; // +0x34: indexed by status_index_ (see Status)
    bool pause_requested_;      // +0x60
    bool finished_;             // +0x61
    uint8_t unknown62_[2];
    int status_index_; // +0x64
    uint8_t unknown68_[3];
    bool unknown6b_; // +0x6B set by the constructor and the "start" calls, cleared once loaded
    uint8_t unknown6c_[0x14];
    float loudness_;         // +0x80
    uint32_t resume_frame_;  // +0x84 the frame it was paused on
    bool from_handle_;       // +0x88 from_where_ holds a file handle, not a path
    char from_where_[0x11F]; // +0x89 the path, or the handle (video_open is given its address)
    uint32_t unknown1a8_;    // +0x1A8
    uint32_t unknown1ac_;    // +0x1AC
    uint8_t unknown1b0_[0xC];
    bool stop_requested_; // +0x1BC freed by the update once ready_to_free
    uint8_t pad1bd_[3];

    // The status word status_index_ picks (0x8000000A: still fine).
    uint32_t get_standing() const { return status_words_[status_index_]; }
};
static_assert(offsetof(BinkStream, wad_entry_) == 0x0C && offsetof(BinkStream, shared_file_) == 0x18 &&
                  offsetof(BinkStream, plays_) == 0x24 && offsetof(BinkStream, bink_) == 0x28 &&
                  offsetof(BinkStream, pause_requested_) == 0x60 && offsetof(BinkStream, status_index_) == 0x64 &&
                  offsetof(BinkStream, unknown6b_) == 0x6B && offsetof(BinkStream, loudness_) == 0x80 &&
                  offsetof(BinkStream, from_handle_) == 0x88 && offsetof(BinkStream, from_where_) == 0x89 &&
                  offsetof(BinkStream, unknown1a8_) == 0x1A8 && offsetof(BinkStream, stop_requested_) == 0x1BC &&
                  sizeof(BinkStream) == 0x1C0,
              "layout must match the original");

// A stream's place (0x14 bytes): the music's at 0x006EDFF4, the three sound streams' at 0x006EDFB8. Playing
// while it holds a stream of either class.
struct StreamSlot {
    // 0x00453500: a new stream in the slot: a WadStream (type 0; unknown) or a BinkStream (type 1; unknown and
    // the number of plays). Other types: nothing.
    void fabricate(int type, bool unknown, int plays);
    // 0x00453090: open the slot's stream on pcm.wad's entry `checksum` (`offset`, `size` in the file `wad`,
    // read into `buffer`; a Bink stream opens it by its own wad index, 0x00452C60). -1 with no stream.
    Result unseal(uint32_t checksum, const char* path, Hdl wad, uint32_t offset, uint32_t size, void* buffer);
    // 0x004530E0: the stream's volume and pan from left and right levels (nothing for a WadStream).
    void set_volume_and_pan(float left, float right);
    // 0x004538F0: the stream deleted (either class).
    void dealloc();

    bool get_playing() const { return wad || bink; }

    WadStream* wad;     // +0x00
    BinkStream* bink;   // +0x04
    uint32_t unknown08; // +0x08
    float volume;       // +0x0C
    bool paused;        // +0x10 (cleared as a dword at start-up)
    uint8_t pad11[3];
};
static_assert(sizeof(StreamSlot) == 0x14, "layout must match the original");

constexpr int kStreamSlots = 3;
StreamSlot* stream_slots(); // 0x006EDFB8
StreamSlot& music_slot();   // 0x006EDFF4

// The thread that services the streams (created by Init).
// 0x00453100 (the thread's function): until told to stop, every 5 ms (with the streams' lock held) each
// stream's Update - the music's, then the three streams'.
unsigned long __stdcall stream_thread(void* unused);
// 0x004531A0: tell the thread to stop and wait for it.
void __cdecl stop_stream_thread();

// 0x004535E0: the next of a ring of 256 OVERLAPPED blocks (0x0069CBB8) that no stream uses, zeroed.
_OVERLAPPED* __cdecl new_overlapped();

// 0x00453670: start-up: the slots emptied, the user soundtracks counted, pcm.wad opened for streaming (kept in
// 0x0067D7FC), pcm.dat's index of it (checksum, offset, size per entry) loaded and sorted by checksum, and the
// stream thread started (above normal priority).
void __cdecl init_platform();

// pcm.wad's index (12 bytes each, sorted by checksum).
struct WadEntry {
    uint32_t checksum;
    uint32_t offset;
    uint32_t size;
};
// 0x004533A0: the index of pcm.wad's entry `checksum` (binary search), -1 if none.
int find_wad_entry(uint32_t checksum);
// 0x004533E0: `checksum` if it's a stream in pcm.wad (never for music: 0), else 0.
uint32_t __cdecl stream_exists(uint32_t checksum, int music);

// 0x00453250 / 0x00453270: 1 if the music / stream `slot` (-1: any of the three) is free, 2 if playing.
int __cdecl music_status();
int __cdecl stream_status(int slot);
// 0x00453220 / 0x004531D0: whether the music / stream `slot`'s preload is far enough to start (true if none).
bool __cdecl music_preload_done();
bool __cdecl stream_preload_done(int slot);
// 0x004532C0: pause (or resume) the music (`which` 1; the state is kept for music started later) or the three
// streams.
void __cdecl pause_streams(bool pause, int which);

// 0x00453940: free the music and streams that finished, failed or were asked to stop. 0.
int __cdecl update_streams();

// The music.
// 0x00453C20: load music track `checksum` (data\streams\music\<checksum as 8 hex digits>.bik under the
// platform's root) as a Bink stream played once, at the music volume and paused if the music is: false if
// music is already loaded or the file won't open.
bool __cdecl load_music_track(uint32_t checksum);
// 0x00453D70: the same for the track named by the file name in `path` (any directories dropped).
bool __cdecl load_music_track_by_name(const char* path);
// 0x00453DB0: load and start music track `checksum` (noted in 0x0069CBA8).
bool __cdecl start_track_playback(uint32_t checksum);
// 0x00453BF0: start the music loaded (then frees what finished): false if there's none.
bool __cdecl start_preloaded_music();
// 0x00453850: play track `track` of user soundtrack `soundtrack` as a WadStream (there are no user
// soundtracks on PC: the file never opens, so this is always false - after leaving a WadStream in the music's
// slot).
bool __cdecl play_user_track(int soundtrack, int track);
// The file of track `track` of user soundtrack `soundtrack`: none on PC (INVALID_HANDLE_VALUE).
Hdl __cdecl open_user_track(int soundtrack, int track);
// 0x00453AA0: stop the music (freed now, or asked to stop if it can't go yet).
void __cdecl platform_stop_music();
// 0x00453400: the music's volume (0-100), if music is loaded. 1. (Not SetMusicVolume: music_player.h's front-end call
// has that name.)
int __cdecl platform_set_music_volume(float volume);
// 0x00453470: keep the music within 5 frames of `seconds` in (cutscenes) - a Bink stream with a Bink open is
// assumed.
void __cdecl sync_music_to_time(float seconds);

// The sound streams.
// A stream's volume: five channel levels (0-100), or the first two as left and right.
struct StreamGain {
    float level[5];
    int type; // 2: five channels; 0, 1, 3: left and right; others: unchanged
};
// 0x00454080: load pcm.wad's entry `checksum` into stream `slot` (free) as a Bink stream played `plays`
// times (0: once), at `volume` (if given); a preload isn't started (StartPreloadedStream). Two arguments unused.
bool __cdecl load_stream(uint32_t checksum, int slot, const StreamGain* volume, int unused4, int unused5, bool preload,
                         int plays);
// 0x00454200: load and start stream `checksum` (noted in 0x0069CBAC) in `slot`, played once.
bool __cdecl play_stream(uint32_t checksum, int slot, int unused);
// 0x00454270: start the stream preloaded in `slot` at `volume`: false if there's none.
bool __cdecl start_preloaded_stream(int slot, const StreamGain* volume);
// 0x00453DF0: stream `slot`'s volume: the levels times the sound effects volume / 100; the slot's volume is
// their mean (left for two). True.
bool __cdecl change_stream_loudness(const StreamGain* volume, int slot);
// 0x00453B30: stop stream `slot` (freed now, or asked to stop if it can't go yet). The second argument is
// unused.
void __cdecl halt_stream(int slot, int unused);
// 0x00453BD0: stop all three.
void __cdecl halt_streams();

// The PC's user soundtracks (none: the count is set to 0 on first use).
// 0x004542D0: how many.
int __cdecl user_soundtrack_count();
// 0x00454300: soundtrack `index`'s name (wide), null past the count.
const wchar_t* __cdecl user_soundtrack_name(int index);
// 0x00454320: how many tracks soundtrack `index` has, 0 past the count.
int __cdecl user_soundtrack_tracks(int index);

extern void* g_wad_file;
extern bool g_bink_sound_set;
extern uint32_t g_stream_file_uses;
extern WadEntry* g_wad_entries;
extern void* g_stream_lock;

} // namespace audio
