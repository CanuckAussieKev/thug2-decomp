#pragma once

#include <cstdint>

// Music: playlists of streamed tracks.
namespace Pcm {

// A playlist (0xCB0 bytes; three at 0x00698098, the current one's index at 0x0069BA9C): up to 80 tracks, and
// 128-bit masks of the tracks switched off (in the music menu) and those already played this round.
struct Playlist {
    uint8_t tracks[80][40]; // a track's name first (the rest unknown)
    int count;
    uint32_t unknownC84;
    uint64_t off[2];    // tracks 0-63, 64-127
    uint64_t played[2];
    uint8_t unknownCA8; // set: no next track (cleared by AddTrackToPlaylist, set by ClearPlaylist / ChangeTrackState)
    uint8_t padCA9[7];
};
static_assert(sizeof(Playlist) == 0xCB0, "layout must match the original");

// 0x00452200 (reached by a jump from 0x00452910): start the next track when no stream is playing - on the
// script's chosen track for playlist 1, the track playing (0x0069806C) again when looping, else the next in the
// shuffled order of the playlist's tracks that are on (reshuffled when the order runs out; the last track
// played doesn't come first). Then runs a script (0x300CAD34) with the track's index. Originally from PartyMod Fixes: the shuffle
// draws from the C runtime's rand (seeded at start-up from the time), so the order differs from launch to
// launch.
void __cdecl PlayNextTrack();

// PartyMod's shuffle draw (its our_random): one of the game's random numbers in [0, n), thrown away, then
// rand() % n.
int ShuffleRandom(int n);

// 0x00452680 (reached by a jump from 0x00452910, when tracks play in order): like PlayNextTrack, but the next
// track is the first of the playlist's tracks that's on and not played yet; when all are played the round
// starts again (nothing plays this time). No script's choice for playlist 1.
void __cdecl PlayNextTrackInOrder();

// 0x00452910: the music's per-frame update - the stream system's (0x00453940; a stream that ended: 0x00453BD0,
// and the music paused when it reports a failure), the streams that finished let go of, and every 64 frames,
// unless paused, the next track (PlayNextTrack, or PlayNextTrackInOrder when tracks don't play in random order
// and it isn't the user's playlist) - if the music is audible or it's playlist 1.
void __cdecl Update();

struct FrameAmp;

// The three streams (0x74 bytes each, at 0x0069B938; started by StartStream / PlayStream, stream.h).
struct Stream {
    uint32_t name;      // +0 what's playing (a checksum: the id asked for, or the stream's)
    uint32_t id;        // +4 the stream's id
    float volume;       // +8 percent (100; PlayStreamFromObject scales it)
    float dropoff;      // +0xC (1020)
    int channel;        // +0x10 the platform's stream (-1: any; 0x00453270 says whether it's playing)
    int priority;       // +0x14
    int option;         // +0x18 PlayStream's (the script's 0xE46111E0)
    uint32_t unknown1C; // +0x1C (set 0)
    int nameCount;      // +0x20 names in use
    uint32_t startFrame; // +0x24 the frame it started (Tmr::GetRenderFrame)
    uint8_t usePosInfo; // +0x28 its volume follows the owner's position (PlayStreamFromObject)
    uint8_t pad29[3];
    int dropoffFunction; // +0x2C
    uint32_t* listener; // +0x30 the stream's frame amplitudes (a FrameAmp: checksum and size cleared when it ends)
    struct {
        uint32_t checksum;
        FrameAmp* frameAmp;
    } names[8];         // +0x34 its names (AddStream adds more) and their frame amplitudes
};
static_assert(sizeof(Stream) == 0x74, "layout must match the original");

// 0x00450220: whether no stream with this id is playing.
bool __cdecl StreamIdFinished(uint32_t id);
// 0x00450260: the stream playing with this id, else the one playing this name; -1 if none.
int __cdecl FindPlayingStream(uint32_t idOrName);
// 0x004502D0: the first id from `id` up that no playing stream has.
uint32_t __cdecl FreeStreamId(uint32_t id);

// User soundtracks (the PC's own music folders, platform code at 0x004542D0 and on).
// 0x00450310: shuffle the order the current user soundtrack's tracks play in (at most 600; 2000 random swaps;
// the track last first before isn't first again).
void __cdecl ShuffleUserSoundtrack();
// 0x004503D0: play user soundtrack `soundtrack` (from the top of its shuffled order when shuffling is on).
void __cdecl UseUserSoundtrack(int soundtrack);
// 0x00450410: back to the game's own music.
void __cdecl UseStandardSoundtrack();
// 0x00450420: a jump to 0x004531A0 (the platform's music stop; not rewritten yet).
void __cdecl StopUserSoundtrack();
// 0x004B2350: a jump to 0x004542D0 - the number of user soundtracks (found on first use; not rewritten yet).
int __cdecl GetNumSoundtracks();
// 0x004B2360: user soundtrack `index`'s name (wide) as text in a static buffer (0x00701590): at most 99
// characters, each through wsprintf's "%ls"; ones that don't convert, and the bytes 0xA1-0xAC, become '~'.
// "~" for no such soundtrack.
const char* __cdecl GetSoundtrackName(int index);

// 0x00450430: whether the music is paused.
bool __cdecl MusicIsPaused();
// 0x00450440 / 0x00450450: the music and sound effects volumes (0-100; 50 and 100 at start).
float __cdecl GetMusicVolume();
float __cdecl GetSfxVolume();
// 0x00450460: 1 if tracks play in random order (on at start), else 0.
int __cdecl RandomTrackOrder();
// 0x00450470: loop the current track (PlayNextTrack plays it again).
void __cdecl SetMusicLooping(bool loop);
// 0x00450480 / 0x004504B0: playlist 0's masks of tracks switched off (0-63, 64-127).
void __cdecl GetTracksOff(uint64_t* low, uint64_t* high);
void __cdecl SetTracksOff(uint64_t low, uint64_t high);
// 0x004504E0: the volume of the user soundtrack, when it's the one playing (0x00453400, not rewritten yet).
void __cdecl SetUserSoundtrackVolume(float volume);

// The playlists and the music track (the script commands of the same names call these). Each does nothing
// (returns 0 / false) while the music is off: no sound, the null sound platform (0x007CEFA8 == 2), or the music
// header not loaded.

// 0x00450F70: pause (1) or unpause (0) the music; -1 unpauses only if it wasn't paused when last paused with 1
// (pausing notes whether it was). Script::GetInteger(0xD7BB618D) is read and ignored (a compiled-out debug print).
void __cdecl PauseMusic(int pause);
// 0x004510B0: the music volume (0-100; others ignored). Applied to the track playing if it's the music's
// (stream type 0) - for playlists other than 0 only when the script integer 0x450BAE2D is set.
void __cdecl SetMusicVolume(float volume);
// 0x00451130: the volume of streams played on the music's channel (PlayMusicStream: stream type 1) - 0-100;
// applied if one is playing. (The value GetSfxVolume reads.)
void __cdecl SetMusicStreamVolume(float volume);
// 0x004511A0: add a track (the stream's file name, upper-cased; at most 80) to playlist `list`; for playlist 0 also
// its title (at most 99 characters; "No title provided" for none) - kept in one table by the track's index.
void __cdecl AddTrackToPlaylist(const char* name, int list, const char* title);
// 0x004512D0: empty playlist `list` (its masks cleared; marked as having no track to play).
void __cdecl ClearPlaylist(int list);
// 0x00451350: skip to the next track (stops the one playing; playlist 0 only).
void __cdecl SkipMusicTrack();
// 0x00451380: tracks in random order (non-zero) or in order; a positive mode also shuffles the user soundtrack
// and starts it from the top. The current playlist's round starts again.
void __cdecl SetRandomMode(int mode);
// 0x00451400: the volume of the track starting: the music volume for a user soundtrack, the sound effects volume
// for playlist 1 unless the script integer 0x450BAE2D is set, else unchanged.
void __cdecl ApplyTrackVolume();
// 0x00451450: whether track `track` of playlist `list` is the one playing (same name).
bool __cdecl IsTrackPlaying(int list, int track);
// 0x004514C0: the number of tracks in playlist `list`.
int __cdecl GetNumTracks(int list);
// 0x00451500: track `track` of playlist `list`'s file name after its last \ or /. With the music off: "Empty."
// (written over playlist 0's first track).
const char* __cdecl GetTrackName(int track, int list);
// 0x004515A0: play from playlist `list` (the track playing stops if it changes).
void __cdecl SetMusicMode(int list);
// 0x004515E0: load the music's header (PC: nothing to load) - the music is on while it succeeds.
bool __cdecl LoadMusicHeader(const char* name);
// 0x00451C70: switch track `track` of playlist `list` off (stopping it if it's playing) or on; the shuffle starts
// again. Marks the playlist as having no track to play only when it's empty (the original's check, see source).
void __cdecl ChangeTrackState(int track, bool off, int list);
// 0x004520D0: play the music track `name` (copied to 0x0069806C; looping or not) at the music volume (the sound
// effects volume for playlists other than 0 unless the script integer 0x450BAE2D is set). A track that doesn't
// start is switched off in the current playlist (unless the debug checks are on). Not while a music stream plays.
void __cdecl PlayTrack(const char* name, bool loop);

} // namespace Pcm
