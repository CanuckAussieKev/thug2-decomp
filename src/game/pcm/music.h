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
    uint8_t unknownCA8; // set: no next track
    uint8_t padCA9[7];
};
static_assert(sizeof(Playlist) == 0xCB0, "layout must match the original");

// 0x00452200 (reached by a jump from 0x00452910): start the next track when no stream is playing - on the
// script's chosen track for playlist 1, the track playing (0x0069806C) again when looping, else the next in the
// shuffled order of the playlist's tracks that are on (reshuffled when the order runs out; the last track
// played doesn't come first). Then runs a script (0x300CAD34) with the track's index. The PC port: the shuffle
// draws from the C runtime's rand (seeded at start-up from the time), so the order differs from launch to
// launch.
void __cdecl PlayNextTrack();

// The PC port's shuffle draw (its our_random): one of the game's random numbers in [0, n), thrown away, then
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

// The three streams (0x74 bytes each, at 0x0069B938).
struct Stream {
    uint32_t name;      // +0 what's playing (a checksum)
    uint32_t id;        // +4 the stream's id
    uint8_t unknown8[8];
    int channel;        // +0x10 the platform's stream (-1: any; 0x00453270 says whether it's playing)
    uint8_t unknown14[0x1C];
    uint32_t* listener; // +0x30 two words cleared when the stream ends
    uint8_t unknown34[0x40];
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

} // namespace Pcm
