// Music: picking the next track of a playlist.

#include "game/pcm/music.h"

#include <cstdint>
#include <cstdlib>

#include "game/core/dbg.h"
#include "game/mth/random.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

Playlist* Playlists() { return reinterpret_cast<Playlist*>(0x00698098); }
int& CurrentPlaylist() { return GameVariable<int>(0x0069BA9C); }
void* SpecialTrack() { return reinterpret_cast<void*>(0x0069806C); } // (40 bytes, just before the playlists)
bool& SpecialTrackSet() { return GameVariable<bool>(0x0069BAA0); }
int& CurrentTrack() { return GameVariable<int>(0x0069BA98); }

// The shuffled order: the indices of the tracks that are on, how many, and the position of the last one
// played. The track played before the shuffle is read at order[count - 1] - with an empty order that's the
// word before it (0x00696D9C, the movie frame dump counter).
int* Order() { return reinterpret_cast<int*>(0x00696DA0); }
int& OrderCount() { return GameVariable<int>(0x0067D7F8); }
int& OrderPosition() { return GameVariable<int>(0x0069BAC4); }
bool& Reshuffle() { return GameVariable<bool>(0x0067D7F4); }

bool MusicOn()
{
    return GameVariable<void*>(0x007D6A38) && GameVariable<int>(0x007CEFA8) != 2 && GameVariable<bool>(0x0067D7E0);
}

// 1 << n as the C runtime's 64-bit shift (__allshl) does it: by n's low byte, 0 from 64 up.
uint64_t Bit(int n)
{
    uint8_t shift = static_cast<uint8_t>(n);
    return shift >= 64 ? 0 : uint64_t{1} << shift;
}
bool IsOff(const Playlist& list, int track)
{
    return track < 64 ? (Bit(track) & list.off[0]) != 0 : (Bit(track - 64) & list.off[1]) != 0;
}

void PlayTrack(void* track, int special) { GameFunction<void(__cdecl*)(void*, int)>(0x004520D0)(track, special); }

// The track's start: the stream played, and the script told which track it is.
void StartTrack(Playlist& list, int track)
{
    PlayTrack(list.tracks[track], 0);
    CurrentTrack() = track;
    Script::CStruct* params = Script::NewStruct();
    params = params ? params->Init() : nullptr;
    params->AddInteger(0x8D02705Du, CurrentTrack());
    GameFunction<void(__cdecl*)(uint32_t, Script::CStruct*, int, int, int)>(0x00475790)(0x300CAD34u, params, 0, 0, 0);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// PartyMod's our_random (main.c), at the shuffle's two draws (0x004523A7 / 0x004523B4). It keeps drawing the
// game's number so the game's random stream advances as before. (rand() % 0 would divide by zero; the game's
// Rnd(0) is 0, so that's kept for an empty order.)
int ShuffleRandom(int n)
{
    int game = Mth::Rnd(n);
    if (n == 0)
        return game;
    return std::rand() % n;
}

// 0x00452200
void __cdecl PlayNextTrack()
{
    Playlist& list = Playlists()[CurrentPlaylist()];
    if (!MusicOn() || list.unknownCA8)
        return;
    if (GameFunction<int(__cdecl*)()>(0x00453250)() != 1) // 1: no stream playing
        return;
    GameVariable<int>(0x0069BAAC) = 0;
    GameFunction<void(__cdecl*)()>(0x00451400)(); // the music volume
    if (CurrentPlaylist() == 1) {
        int track = Script::GetInteger(0xF18AB1F9u);
        if (track < Playlists()[1].count)
            PlayTrack(Playlists()[1].tracks[track], 0);
        return;
    }
    if (SpecialTrackSet()) {
        PlayTrack(SpecialTrack(), 1);
        return;
    }
    if (list.count == 0)
        return;

    int position;
    if (Reshuffle()) {
        int last = Order()[OrderCount() - 1];
        OrderCount() = 0;
        int count = 0;
        for (int track = 0; track < list.count; ++track)
            if (!IsOff(list, track))
                Order()[count++] = track;
        OrderCount() = count;
        Dbg::Printf(reinterpret_cast<const char*>(0x00647318), count); // "shuffling random song order num_songs=%i\n"
        // 2000 swaps of two random places. PartyMod draws them with ShuffleRandom; the original with
        // Mth::Rnd(OrderCount()).
        for (int i = 0; i < 2000; ++i) {
            int a = ShuffleRandom(OrderCount());
            int b = ShuffleRandom(OrderCount());
            if (a != b) {
                int swap = Order()[a];
                Order()[a] = Order()[b];
                Order()[b] = swap;
            }
        }
        // Not the last one again first: swap it with another place (this draw is the game's, under PartyMod too).
        if (Order()[0] == last) {
            int other = OrderCount() > 1 ? Mth::Rnd(OrderCount() - 1) + 1 : 0;
            int swap = Order()[0];
            Order()[0] = Order()[other];
            Order()[other] = swap;
        }
        OrderPosition() = 0;
        Reshuffle() = false;
        list.played[0] = 0;
        list.played[1] = 0;
        position = 0;
    } else {
        position = ++OrderPosition();
        if (position >= OrderCount() - 1)
            Reshuffle() = true;
    }
    if (list.count <= 0)
        return;
    // The track at that position, unless it's off or already played (then nothing). (The original repeats
    // this same test list.count times.)
    int track = Order()[position];
    if ((Bit(track) & list.off[0]) == 0 && track < 64 && (Bit(track) & list.played[0]) == 0) {
        list.played[0] |= Bit(track);
        StartTrack(list, track);
    } else if ((Bit(track - 64) & list.off[1]) == 0 && track >= 64 && (Bit(track - 64) & list.played[1]) == 0) {
        list.played[1] |= Bit(track - 64);
        StartTrack(list, track);
    }
}

} // namespace Pcm

REPLACE(0x00452200, Pcm::PlayNextTrack);
