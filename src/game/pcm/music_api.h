#pragma once

#include "hook/game_global.h"

#include <cstdint>

// The music and streams front end (Neversoft's Pcm, gel/music): the game's calls for music tracks, music streams
// and streamed sounds, each doing nothing when there's no sound system (0x007D6A38), the sound is off
// (0x007CEFA8 == 2) or music (0x0067D7E0) / streams (0x0067D7E1) are switched off. The platform layer under it
// (PCMAudio_*, 0x00453xxx-0x00454xxx) isn't rewritten yet.
namespace Pcm {

// 0x004505F0 / 0x00450620: whether music / streamed sounds are unavailable (no sound system, sound off, or
// switched off).
bool __cdecl NoMusicPlease();
bool __cdecl StreamsDisabled();

// 0x00450D60: the music system set up - the owners of the streams forgotten; if music or streams are on, the
// platform's set up (0x00453670), the default music and stream volumes from the script globals (0x73F8E03B and
// 0xABD4A575: 0 means 100 and 50), the first two playlists emptied and the streams cleared.
void __cdecl Init();
// 0x00450EB0: only with the debug flag 0x00684FA8 (the PS2's "streaming from the CD") and once set up: the
// music and the streams stopped. Always false (no load needs to be skipped).
bool __cdecl UsingCD();

// 0x00450500: whether `name` is a stream in the stream index (0x004533E0).
bool __cdecl StreamExists(uint32_t name);
// 0x00450520 / 0x00450530: the track playing (the script's GetCurrentTrack; PlayNextTrack sets it).
int __cdecl GetCurrentTrack();
void __cdecl SetCurrentTrack(int track);

// 0x00450650: the music stopped (0x00453AA0(true)); no music stream; the next track looked for next frame.
void __cdecl StopMusic();
// 0x00450820: play the music track `name` at `volume` (the platform's 0x00453D70) - or, while the user's
// soundtrack plays (not on playlist 1), its current track (0x00453850) and then the next one chosen (from its
// shuffled order when shuffling, reshuffled at the end; else the next in order, back to the first after the
// last). True if one plays.
bool __cdecl PlayTrackByName(const char* name, float volume);
// 0x00450EF0: the music stopped and its volume set (`volume`, or the default when negative); a music stream
// counts as playing. (`name` isn't used: the platform stream isn't started here.)
void __cdecl PlayMusicStream(uint32_t name, float volume);
// 0x004509E0: the music stopped and the music stream `name` preloaded (0x00453DB0). True if it is.
bool __cdecl PreLoadMusicStream(uint32_t name);
// 0x00450A50: whether the preloaded music stream is ready (0x00453220).
bool __cdecl PreLoadMusicStreamDone();
// 0x00450A80: start the preloaded music stream (0x00453BF0) at `volume` (the default when negative).
bool __cdecl StartPreLoadedMusicStream(float volume);

// The streams (by id, or the name playing - see FindPlayingStream).
// 0x00450690: stop stream `which` (an index), or all of them for -1.
void __cdecl StopStreams(int which);
// 0x004506D0: stop the stream with this id (or name): the platform's stopped, then the first owner slot naming it
// (an owner's +0x18 words, then the owner) cleared.
void __cdecl StopStream(uint32_t idOrName);
// 0x00450780: the stream's volume (0x00453DF0). False if no such stream plays (true when streams are off).
bool __cdecl SetStreamVolume(uint32_t idOrName, void* volume);
// 0x004507D0: the stream's pitch - nothing on PC (the platform call is a stub returning true). The same results.
bool __cdecl SetStreamPitch(uint32_t idOrName, float pitch);
// 0x00450940: whether the stream's preload is done (0x004531D0).
bool __cdecl PreLoadStreamDone(uint32_t idOrName);
// 0x00450980: start the preloaded stream (0x00454270) with `volume` and `pitch`, `priority` noted in the stream.
bool __cdecl StartPreLoadedStream(uint32_t idOrName, void* volume, float pitch, int priority);
// 0x00450AE0: the stream (by id; or with an owner, the playing stream that owner started for that name) drops
// the first entry of its queue if it's `entry`, the queue's positions moved back with it. True if the stream was
// found.
bool __cdecl RemoveStream(uint32_t idOrName, uint32_t entry, void* owner);
// 0x00450BE0: owner slot `slot`'s stream's volume updated from its object's position (for a positional stream)
// or its own volume. False if the slot's stream isn't one of the streams or no longer plays.
bool __cdecl UpdateStreamVolume(int slot);

// The lip-sync data of a stream: fam\<checksum>.fam, less than 4 KB (0x1008 bytes; one at 0x0069BAC8).
struct FamData {
    uint32_t name;
    int size;
    uint8_t data[0x1000];

    // 0x00450540: load fam\<name in hex>.fam (blocking). False if it can't be opened or is 4 KB or more.
    bool Load(uint32_t name);
};
static_assert(sizeof(FamData) == 0x1008, "layout must match the original");

GAME_GLOBAL_EXTERN(int, g_frameCount);
GAME_GLOBAL_EXTERN(int, g_currentTrackIndex);
GAME_GLOBAL_EXTERN(int, g_currentPlaylist);
GAME_GLOBAL_EXTERN(bool, g_initialised);
GAME_GLOBAL_EXTERN(bool, g_usingUserSoundtrack);
GAME_GLOBAL_EXTERN(int, g_userSoundtrack);

GAME_GLOBAL_EXTERN(uint32_t, g_userTrack);
GAME_GLOBAL_EXTERN(uint32_t, g_userPosition);
} // namespace Pcm
