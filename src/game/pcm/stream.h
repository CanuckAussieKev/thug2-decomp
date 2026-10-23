#pragma once

#include <cstddef>
#include <cstdint>

struct _OVERLAPPED;

namespace Nx {
struct Bink;
}

// The PC's streamed audio (the platform layer under Pcm's music and streams): the music and three sound
// streams, each played by a Bink file through binkw32's DirectSound output, serviced by a thread every 5 ms.
// The pcm.wad stream class left from the console versions (WadStream) is still created in places, but its
// playing is compiled out on PC (its update, volume and "done" calls are linker-folded stubs).
namespace Pcm {

using Handle = void*; // a Win32 HANDLE
using Result = long;  // an HRESULT: 0 or an error (E_INVALIDARG 0x80070057, HRESULT_FROM_WIN32(...))

// A raw stream from pcm.wad (0xA8 bytes): reads the file in 8 KB pieces with overlapped ReadFile. On PC nothing
// opens one with a valid file (see 0x00453850), so only the set-up and the flags are live.
struct WadStream {
    // 0x00454340: a closed stream (file -1); `unknown15` kept; m_unknown5F set.
    WadStream* Construct(bool unknown15);
    // 0x00454490: start reading `file` (the whole file) into `buffer`. HRESULT_FROM_WIN32 of the last error
    // for an invalid handle.
    Result Open(Handle file, void* buffer);
    // 0x00454450: start reading `size` bytes of `wad` at `offset` into `buffer`.
    Result OpenFromWad(Handle wad, uint32_t offset, uint32_t size, void* buffer);
    // 0x00454370: the first 8 KB read requested (overlapped): 0 if it's done or pending, else
    // HRESULT_FROM_WIN32 of the error (an error <= 0 as it is).
    Result StartRead();
    // 0x004543F0: whether the preload (state 1; any other state: false) is far enough: the read done at once,
    // or 8 KB loaded ahead.
    bool PreloadDone();
    // 0x00454430: pause (non-zero) or resume.
    void Pause(int pause);
    // 0x00454440 (empty; also called on BinkStreams - linker-folded): the five channels' volumes.
    void SetChannelVolumes(float frontLeft, float frontRight, float center, float rearLeft, float rearRight);

    // Linker-folded stubs on PC: 0x005BA1D0 (the thread's update: 0), 0x005BAB90 (the volume), 0x0048DCD0
    // (volume and pan), 0x005B9760 (ready to be freed: true).
    int Update() { return 0; }
    void SetVolume(float) {}
    void SetVolumeAndPan(float, float) {}
    bool ReadyToFree() { return true; }
    // The destructor: empty (a call to 0x005480A0).
    void Destruct() {}

    // The status word at +0x2C + 4 * m_statusIndex (E_PENDING 0x8000000A: still going).
    uint32_t Status() const
    {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(this) + 0x2C + 4 * m_statusIndex);
    }

    Handle m_file;              // +0x00 INVALID_HANDLE_VALUE when closed
    uint32_t m_unknown04;       // +0x04
    _OVERLAPPED* m_overlapped;  // +0x08 (from the ring, 0x004535E0; never set for these on PC)
    bool m_reading;             // +0x0C
    bool m_readDone;            // +0x0D the read finished at once
    uint8_t m_pad0E[2];
    uint32_t m_unknown10;       // +0x10
    bool m_fromWad;             // +0x14 read at m_offset (else from the start)
    bool m_unknown15;           // +0x15
    uint8_t m_pad16[2];
    uint32_t m_offset;          // +0x18
    uint32_t m_size;            // +0x1C
    uint32_t m_unknown20;       // +0x20
    uint32_t m_unknown24;       // +0x24
    uint8_t m_unknown28[4];
    uint32_t m_status[9];       // +0x2C indexed by m_statusIndex (0x8000000A: still fine)
    uint32_t m_unknown50;       // +0x50
    bool m_paused;              // +0x54
    bool m_finished;            // +0x55
    uint8_t m_pad56[2];
    int m_statusIndex;          // +0x58
    uint8_t m_unknown5C[3];
    bool m_unknown5F;           // +0x5F set by the constructor and the "start" calls, cleared once loaded
    uint8_t m_unknown60[0x30];
    uint32_t m_unknown90;       // +0x90
    void* m_buffer;             // +0x94
    int m_loaded;               // +0x98
    int m_played;               // +0x9C
    int m_state;                // +0xA0 1: preloading
    bool m_stopRequested;       // +0xA4 freed by the update once ReadyToFree
    uint8_t m_padA5[3];
};
static_assert(offsetof(WadStream, m_overlapped) == 0x08 && offsetof(WadStream, m_fromWad) == 0x14 &&
                  offsetof(WadStream, m_offset) == 0x18 && offsetof(WadStream, m_status) == 0x2C &&
                  offsetof(WadStream, m_unknown50) == 0x50 && offsetof(WadStream, m_statusIndex) == 0x58 &&
                  offsetof(WadStream, m_unknown5F) == 0x5F && offsetof(WadStream, m_buffer) == 0x94 &&
                  offsetof(WadStream, m_state) == 0xA0 && offsetof(WadStream, m_stopRequested) == 0xA4 &&
                  sizeof(WadStream) == 0xA8,
              "layout must match the original");

// A Bink file played for its sound (0x1C0 bytes): opened from a path (the music) or from a file handle into
// pcm.wad (the streams; 0x00452C60). Pausing closes the Bink and notes the frame; resuming reopens it there.
struct BinkStream {
    // 0x00452C00 (the constructor; not rewritten here): unknown19 and the number of plays (0: once).
    // 0x00452D20: open the Bink file at `path` (the path kept for reopening): 0, or E_INVALIDARG if it won't
    // open. Bink's sound goes to DirectSound (set up once; "Bink can't use DSound" if that fails).
    Result Open(const char* path);
    // 0x00452DC0: ask the thread to pause (non-zero) or resume - unless pausing is locked out (0x0069CB98,
    // never set).
    void Pause(int pause);
    // 0x00452DE0: the volume (0-100; Bink's 0-32768): silent at 0 or less.
    void SetVolume(float volume);
    // 0x00452FC0: the volume from the left and right levels (their mean is kept as the volume): the louder
    // one's level, at most 32768, and the pan right / (left + right) of 65536.
    void SetVolumeAndPan(float left, float right);
    // 0x00452E30 (the destructor): the Bink closed, the file closed unless shared, the wad entry let go of
    // (its last use noted), the memory block freed.
    void Destruct();
    // 0x00452EA0 (on the stream thread): pause or resume as asked, then the frames due decoded while the
    // window has focus; the plays counted down at the last frame (finished when they run out). 0.
    int Update();
    // 0x005B9760 (linker-folded `return true`): whether it may be freed now (else it's asked to stop), and
    // whether its preload is far enough to start.
    bool ReadyToFree() { return true; }
    bool PreloadDone() { return true; }
    // 0x00454440 (WadStream::SetChannelVolumes, linker-folded: empty).
    void SetChannelVolumes(float, float, float, float, float) {}

    Handle m_file;              // +0x00 INVALID_HANDLE_VALUE: none
    uint32_t m_unknown04;       // +0x04
    _OVERLAPPED* m_overlapped;  // +0x08 (from the ring, 0x004535E0)
    int m_wadEntry;             // +0x0C the pcm.wad entry open (0x0069CADC), -1: none
    bool m_silent;              // +0x10 the volume is 0: not updated
    bool m_paused;              // +0x11 the pause state carried out
    uint8_t m_unknown12[6];
    bool m_sharedFile;          // +0x18 m_file isn't closed with the stream
    bool m_unknown19;           // +0x19
    uint8_t m_unknown1A[0xA];
    int m_plays;                // +0x24 plays left (counted down at the last frame; 0 after one: finished)
    Nx::Bink* m_bink;           // +0x28 null while paused
    void* m_memory;             // +0x2C freed with the stream
    uint8_t m_unknown30[0x30];  // +0x34: status words, indexed by m_statusIndex (see Status)
    bool m_pauseRequested;      // +0x60
    bool m_finished;            // +0x61
    uint8_t m_unknown62[2];
    int m_statusIndex;          // +0x64
    uint8_t m_unknown68[3];
    bool m_unknown6B;           // +0x6B set by the constructor and the "start" calls, cleared once loaded
    uint8_t m_unknown6C[0x14];
    float m_volume;             // +0x80
    uint32_t m_resumeFrame;     // +0x84 the frame it was paused on
    bool m_fromHandle;          // +0x88 m_source holds a file handle, not a path
    char m_source[0x11F];       // +0x89 the path, or the handle (BinkOpen is given its address)
    uint32_t m_unknown1A8;      // +0x1A8
    uint32_t m_unknown1AC;      // +0x1AC
    uint8_t m_unknown1B0[0xC];
    bool m_stopRequested;       // +0x1BC freed by the update once ReadyToFree
    uint8_t m_pad1BD[3];

    // The status word at +0x34 + 4 * m_statusIndex (0x8000000A: still fine).
    uint32_t Status() const
    {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(this) + 0x34 + 4 * m_statusIndex);
    }
};
static_assert(offsetof(BinkStream, m_wadEntry) == 0x0C && offsetof(BinkStream, m_sharedFile) == 0x18 &&
                  offsetof(BinkStream, m_plays) == 0x24 && offsetof(BinkStream, m_bink) == 0x28 &&
                  offsetof(BinkStream, m_pauseRequested) == 0x60 && offsetof(BinkStream, m_statusIndex) == 0x64 &&
                  offsetof(BinkStream, m_unknown6B) == 0x6B && offsetof(BinkStream, m_volume) == 0x80 &&
                  offsetof(BinkStream, m_fromHandle) == 0x88 && offsetof(BinkStream, m_source) == 0x89 &&
                  offsetof(BinkStream, m_unknown1A8) == 0x1A8 && offsetof(BinkStream, m_stopRequested) == 0x1BC &&
                  sizeof(BinkStream) == 0x1C0,
              "layout must match the original");

// A stream's place (0x14 bytes): the music's at 0x006EDFF4, the three sound streams' at 0x006EDFB8. Playing
// while it holds a stream of either class.
struct StreamSlot {
    // 0x00453500: a new stream in the slot: a WadStream (type 0; unknown) or a BinkStream (type 1; unknown and
    // the number of plays). Other types: nothing.
    void Create(int type, bool unknown, int plays);
    // 0x00453090: open the slot's stream on pcm.wad's entry `checksum` (`offset`, `size` in the file `wad`,
    // read into `buffer`; a Bink stream opens it by its own wad index, 0x00452C60). -1 with no stream.
    Result Open(uint32_t checksum, const char* path, Handle wad, uint32_t offset, uint32_t size, void* buffer);
    // 0x004530E0: the stream's volume and pan from left and right levels (nothing for a WadStream).
    void SetVolumeAndPan(float left, float right);
    // 0x004538F0: the stream deleted (either class).
    void Free();

    bool Playing() const { return wad || bink; }

    WadStream* wad;      // +0x00
    BinkStream* bink;    // +0x04
    uint32_t unknown08;  // +0x08
    float volume;        // +0x0C
    bool paused;         // +0x10 (cleared as a dword at start-up)
    uint8_t pad11[3];
};
static_assert(sizeof(StreamSlot) == 0x14, "layout must match the original");

constexpr int kNumStreams = 3;
StreamSlot* StreamSlots(); // 0x006EDFB8
StreamSlot& MusicSlot();   // 0x006EDFF4

// The thread that services the streams (created by Init).
// 0x00453100 (the thread's function): until told to stop, every 5 ms (with the streams' lock held) each
// stream's Update - the music's, then the three streams'.
unsigned long __stdcall StreamThread(void* unused);
// 0x004531A0: tell the thread to stop and wait for it.
void __cdecl StopStreamThread();

// 0x004535E0: the next of a ring of 256 OVERLAPPED blocks (0x0069CBB8) that no stream uses, zeroed.
_OVERLAPPED* __cdecl NewOverlapped();

// 0x00453670: start-up: the slots emptied, the user soundtracks counted, pcm.wad opened for streaming (kept in
// 0x0067D7FC), pcm.dat's index of it (checksum, offset, size per entry) loaded and sorted by checksum, and the
// stream thread started (above normal priority).
void __cdecl InitPlatform();

// pcm.wad's index (12 bytes each, sorted by checksum).
struct WadEntry {
    uint32_t checksum;
    uint32_t offset;
    uint32_t size;
};
// 0x004533A0 (the checksum in EBX): the index of pcm.wad's entry `checksum` (binary search), -1 if none.
int FindWadEntry(uint32_t checksum);
void FindWadEntryStub();
// 0x004533E0: `checksum` if it's a stream in pcm.wad (never for music: 0), else 0.
uint32_t __cdecl StreamExists(uint32_t checksum, int music);

// 0x00453250 / 0x00453270: 1 if the music / stream `slot` (-1: any of the three) is free, 2 if playing.
int __cdecl MusicStatus();
int __cdecl StreamStatus(int slot);
// 0x00453220 / 0x004531D0: whether the music / stream `slot`'s preload is far enough to start (true if none).
bool __cdecl MusicPreloadDone();
bool __cdecl StreamPreloadDone(int slot);
// 0x004532C0: pause (or resume) the music (`which` 1; the state is kept for music started later) or the three
// streams.
void __cdecl PauseStreams(bool pause, int which);

// 0x00453940: free the music and streams that finished, failed or were asked to stop. 0.
int __cdecl UpdateStreams();

// The music.
// 0x00453C20: load music track `checksum` (data\streams\music\<checksum as 8 hex digits>.bik under the
// platform's root) as a Bink stream played once, at the music volume and paused if the music is: false if
// music is already loaded or the file won't open.
bool __cdecl LoadMusicTrack(uint32_t checksum);
// 0x00453D70: the same for the track named by the file name in `path` (any directories dropped).
bool __cdecl LoadMusicTrackByName(const char* path);
// 0x00453DB0: load and start music track `checksum` (noted in 0x0069CBA8).
bool __cdecl PlayMusicTrack(uint32_t checksum);
// 0x00453BF0: start the music loaded (then frees what finished): false if there's none.
bool __cdecl StartPreloadedMusic();
// 0x00453850: play track `track` of user soundtrack `soundtrack` as a WadStream (there are no user
// soundtracks on PC: the file never opens, so this is always false - after leaving a WadStream in the music's
// slot).
bool __cdecl PlayUserTrack(int soundtrack, int track);
// The file of track `track` of user soundtrack `soundtrack`: none on PC (INVALID_HANDLE_VALUE).
Handle __cdecl OpenUserTrack(int soundtrack, int track);
// 0x00453AA0: stop the music (freed now, or asked to stop if it can't go yet).
void __cdecl PlatformStopMusic();
// 0x00453400: the music's volume (0-100), if music is loaded. 1. (Not SetMusicVolume: music.h's front-end call
// has that name.)
int __cdecl PlatformSetMusicVolume(float volume);
// 0x00453470: keep the music within 5 frames of `seconds` in (cutscenes) - a Bink stream with a Bink open is
// assumed.
void __cdecl SyncMusicToTime(float seconds);

// The sound streams.
// A stream's volume: five channel levels (0-100), or the first two as left and right.
struct StreamVolume {
    float level[5];
    int type; // 2: five channels; 0, 1, 3: left and right; others: unchanged
};
// 0x00454080: load pcm.wad's entry `checksum` into stream `slot` (free) as a Bink stream played `plays`
// times (0: once), at `volume` (if given); a preload isn't started (StartPreloadedStream). Two arguments unused.
bool __cdecl LoadStream(uint32_t checksum, int slot, const StreamVolume* volume, int unused4, int unused5,
                        bool preload, int plays);
// 0x00454200: load and start stream `checksum` (noted in 0x0069CBAC) in `slot`, played once.
bool __cdecl PlayStream(uint32_t checksum, int slot, int unused);
// 0x00454270: start the stream preloaded in `slot` at `volume`: false if there's none.
bool __cdecl StartPreloadedStream(int slot, const StreamVolume* volume);
// 0x00453DF0: stream `slot`'s volume: the levels times the sound effects volume / 100; the slot's volume is
// their mean (left for two). True.
bool __cdecl SetStreamVolume(const StreamVolume* volume, int slot);
// 0x00453B30: stop stream `slot` (freed now, or asked to stop if it can't go yet). The second argument is
// unused.
void __cdecl StopStream(int slot, int unused);
// 0x00453BD0: stop all three.
void __cdecl StopStreams();

// The PC's user soundtracks (none: the count is set to 0 on first use).
// 0x004542D0: how many.
int __cdecl UserSoundtrackCount();
// 0x00454300: soundtrack `index`'s name (wide), null past the count.
const wchar_t* __cdecl UserSoundtrackName(int index);
// 0x00454320: how many tracks soundtrack `index` has, 0 past the count.
int __cdecl UserSoundtrackTracks(int index);

} // namespace Pcm
