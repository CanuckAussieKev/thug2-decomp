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
// script's chosen track for playlist 1, the special track (0x0069806C) when one is set, else the next in the
// shuffled order of the playlist's tracks that are on (reshuffled when the order runs out; the last track
// played doesn't come first). Then runs a script (0x300CAD34) with the track's index. PartyMod: the shuffle
// draws from the C runtime's rand (seeded at start-up from the time), so the order differs from launch to
// launch.
void __cdecl PlayNextTrack();

// PartyMod's shuffle draw (its our_random): one of the game's random numbers in [0, n), thrown away, then
// rand() % n.
int ShuffleRandom(int n);

} // namespace Pcm
