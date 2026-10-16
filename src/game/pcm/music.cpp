// Music: picking the next track of a playlist.

#include "game/pcm/music.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "game/core/dbg.h"
#include "game/mth/random.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

Playlist* Playlists() { return reinterpret_cast<Playlist*>(0x00698098); }
int& CurrentPlaylist() { return GameVariable<int>(0x0069BA9C); }
// The track playing (40 bytes, just before the playlists) and whether it loops (SetMusicLooping).
void* SpecialTrack() { return reinterpret_cast<void*>(0x0069806C); }
bool& Looping() { return GameVariable<bool>(0x0069BAA0); }
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
// The same for streamed sound effects (0x0067D7E1).
bool StreamsOn()
{
    return GameVariable<void*>(0x007D6A38) && GameVariable<int>(0x007CEFA8) != 2 && GameVariable<bool>(0x0067D7E1);
}

Stream* Streams() { return reinterpret_cast<Stream*>(0x0069B938); }
constexpr int kStreams = 3;
// 1 if the platform's stream `channel` (-1: all three) isn't playing, 2 if it is (0x00453270, not rewritten yet).
int ChannelStatus(int channel) { return GameFunction<int(__cdecl*)(int)>(0x00453270)(channel); }
bool Playing(const Stream& stream) { return ChannelStatus(stream.channel) != 1; }
// The objects that started each stream (null: none); they note the stream's id at +0x18 + 4 * stream.
uint32_t** StreamOwners() { return reinterpret_cast<uint32_t**>(0x00698060); }
int& FrameCount() { return GameVariable<int>(0x0067D7F0); }

// The user soundtrack: which, whether it's playing (in place of the game's music), its shuffled order and the
// track in it, whether it's shuffled.
int& UserSoundtrack() { return GameVariable<int>(0x0069BAB4); }
bool& UsingUserSoundtrack() { return GameVariable<bool>(0x0069BAB0); }
int* UserOrder() { return reinterpret_cast<int*>(0x00697700); }
constexpr int kUserOrderSize = 600;
int& UserTrack() { return GameVariable<int>(0x0069BAB8); }
int& UserPosition() { return GameVariable<int>(0x0069BAC0); }
bool ShuffleUser() { return GameVariable<bool>(0x0069BABC); }

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
    if (Looping()) {
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

// 0x00452680
void __cdecl PlayNextTrackInOrder()
{
    if (!MusicOn())
        return;
    Playlist& list = Playlists()[CurrentPlaylist()];
    if (list.unknownCA8)
        return;
    if (GameFunction<int(__cdecl*)()>(0x00453250)() != 1) // 1: no stream playing
        return;
    GameVariable<int>(0x0069BAAC) = 0;
    GameFunction<void(__cdecl*)()>(0x00451400)(); // the music volume
    if (Looping()) {
        PlayTrack(SpecialTrack(), 1);
        return;
    }
    for (int track = 0; track < list.count; ++track) {
        if ((Bit(track) & list.off[0]) == 0 && track < 64 && (Bit(track) & list.played[0]) == 0) {
            list.played[0] |= Bit(track);
            StartTrack(list, track);
            return;
        }
        if ((Bit(track - 64) & list.off[1]) == 0 && track >= 64 && (Bit(track - 64) & list.played[1]) == 0) {
            list.played[1] |= Bit(track - 64);
            StartTrack(list, track);
            return;
        }
    }
    list.played[0] = 0;
    list.played[1] = 0;
}

// 0x00452910
void __cdecl Update()
{
    if (!GameVariable<void*>(0x007D6A38) || GameVariable<int>(0x007CEFA8) == 2)
        return;
    if (!GameVariable<bool>(0x0067D7E0) && !GameVariable<bool>(0x0067D7E1))
        return;
    int streams = GameFunction<int(__cdecl*)()>(0x00453940)(); // the stream system's update
    if (streams != 0) {
        if (StreamsOn())
            GameFunction<void(__cdecl*)()>(0x00453BD0)();
        if (streams < 0)
            GameFunction<void(__cdecl*)()>(0x00450650)(); // pause the music
    }
    ++FrameCount();
    for (int i = 0; i < kStreams; ++i) {
        uint32_t*& owner = StreamOwners()[i];
        if (owner && !GameFunction<bool(__cdecl*)(int)>(0x00450BE0)(i)) { // (false: the stream is over)
            owner[6 + i] = 0;
            owner = nullptr;
        }
    }
    for (int i = 0; i < kStreams; ++i) {
        Stream& stream = Streams()[i];
        if (stream.listener && !Playing(stream)) {
            if (stream.listener[0]) {
                stream.listener[0] = 0;
                stream.listener[1] = 0;
            }
            stream.listener = nullptr;
        }
    }
    if (FrameCount() & 0x3F)
        return;
    int playlist = CurrentPlaylist();
    if (GameVariable<float>(0x0067D7E4) == 0.0f && playlist != 1) // (the music volume)
        return;
    if (MusicIsPaused())
        return;
    if (!RandomTrackOrder() && playlist != 1)
        PlayNextTrackInOrder();
    else
        PlayNextTrack();
}

// 0x00450220
bool __cdecl StreamIdFinished(uint32_t id)
{
    for (int i = 0; i < kStreams; ++i)
        if (Streams()[i].id == id && Playing(Streams()[i]))
            return false;
    return true;
}

// 0x00450260
int __cdecl FindPlayingStream(uint32_t idOrName)
{
    for (int i = 0; i < kStreams; ++i)
        if (Streams()[i].id == idOrName && Playing(Streams()[i]))
            return i;
    for (int i = 0; i < kStreams; ++i)
        if (Streams()[i].name == idOrName && Playing(Streams()[i]))
            return i;
    return -1;
}

// 0x004502D0
uint32_t __cdecl FreeStreamId(uint32_t id)
{
    for (;; ++id) {
        bool used = false;
        for (int i = 0; i < kStreams && !used; ++i)
            used = Streams()[i].id == id && Playing(Streams()[i]);
        if (!used)
            return id;
    }
}

// 0x00450310 (with the game's random numbers: PartyMod's shuffle change is only in PlayNextTrack's)
void __cdecl ShuffleUserSoundtrack()
{
    int count = GameFunction<int(__cdecl*)(int)>(0x00454320)(UserSoundtrack()); // the soundtrack's tracks
    if (count == 0)
        return;
    if (count > kUserOrderSize)
        count = kUserOrderSize;
    int last = UserOrder()[count - 1]; // (the last of the order before - not the track played last)
    for (int i = 0; i < count; ++i)
        UserOrder()[i] = i;
    for (int i = 0; i < 2000; ++i) {
        int a = Mth::Rnd(count);
        int b = Mth::Rnd(count);
        int swap = UserOrder()[a];
        UserOrder()[a] = UserOrder()[b];
        UserOrder()[b] = swap;
    }
    if (UserOrder()[0] == last) {
        int other = count > 1 ? Mth::Rnd(count - 1) + 1 : 0;
        int swap = UserOrder()[0];
        UserOrder()[0] = UserOrder()[other];
        UserOrder()[other] = swap;
    }
}

// 0x004503D0
void __cdecl UseUserSoundtrack(int soundtrack)
{
    UserSoundtrack() = soundtrack;
    UsingUserSoundtrack() = true;
    if (ShuffleUser()) {
        ShuffleUserSoundtrack();
        UserPosition() = 0;
        UserTrack() = UserOrder()[0];
    } else {
        UserTrack() = 0;
    }
}

// 0x00450410
void __cdecl UseStandardSoundtrack() { UsingUserSoundtrack() = false; }

// 0x00450420
void __cdecl StopUserSoundtrack() { GameFunction<void(__cdecl*)()>(0x004531A0)(); }

// 0x004B2350
int __cdecl GetNumSoundtracks() { return GameFunction<int(__cdecl*)()>(0x004542D0)(); }

// 0x004B2360
const char* __cdecl GetSoundtrackName(int index)
{
    char* name = reinterpret_cast<char*>(0x00701590);
    // the soundtrack's name (0x00454300, not rewritten yet; null past the last)
    const wchar_t* wide = GameFunction<const wchar_t*(__cdecl*)(int)>(0x00454300)(index);
    if (wide) {
        char* out = name;
        int length = 0;
        for (; *wide; ++wide) {
            wchar_t one[2] = {*wide, 0};
            char converted[12];
            wsprintfA(converted, "%ls", one);
            if (length < 99) {
                *out++ = converted[0] ? converted[0] : '~';
                ++length;
            }
        }
        *out = 0;
    } else {
        std::strcpy(name, "~");
    }
    int length = static_cast<int>(std::strlen(name));
    for (int i = 0; i < length; ++i) {
        signed char c = static_cast<signed char>(name[i]);
        if (c >= static_cast<signed char>(0xA1) && c <= static_cast<signed char>(0xAC))
            name[i] = '~';
    }
    return name;
}

// 0x00450430
bool __cdecl MusicIsPaused() { return GameVariable<bool>(0x0069BAA8); }

// 0x00450440
float __cdecl GetMusicVolume() { return GameVariable<float>(0x0067D7E4); }

// 0x00450450
float __cdecl GetSfxVolume() { return GameVariable<float>(0x0067D7E8); }

// 0x00450460
int __cdecl RandomTrackOrder() { return GameVariable<uint8_t>(0x0067D7EC); }

// 0x00450470
void __cdecl SetMusicLooping(bool loop) { Looping() = loop; }

// 0x00450480
void __cdecl GetTracksOff(uint64_t* low, uint64_t* high)
{
    *low = Playlists()[0].off[0];
    *high = Playlists()[0].off[1];
}

// 0x004504B0
void __cdecl SetTracksOff(uint64_t low, uint64_t high)
{
    Playlists()[0].off[0] = low;
    Playlists()[0].off[1] = high;
}

// 0x004504E0
void __cdecl SetUserSoundtrackVolume(float volume)
{
    if (CurrentPlaylist() == 1)
        GameFunction<int(__cdecl*)(float)>(0x00453400)(volume);
}

} // namespace Pcm

REPLACE(0x00452200, Pcm::PlayNextTrack);
REPLACE(0x00452680, Pcm::PlayNextTrackInOrder);
REPLACE(0x00452910, Pcm::Update);
REPLACE(0x00450220, Pcm::StreamIdFinished);
REPLACE(0x00450260, Pcm::FindPlayingStream);
REPLACE(0x004502D0, Pcm::FreeStreamId);
REPLACE(0x00450310, Pcm::ShuffleUserSoundtrack);
REPLACE(0x004503D0, Pcm::UseUserSoundtrack);
REPLACE(0x00450410, Pcm::UseStandardSoundtrack);
REPLACE_SOURCE_ONLY(0x00450420, Pcm::StopUserSoundtrack); // a jump to 0x004531A0
REPLACE(0x00450430, Pcm::MusicIsPaused);
REPLACE(0x00450440, Pcm::GetMusicVolume);
REPLACE(0x00450450, Pcm::GetSfxVolume);
REPLACE(0x00450460, Pcm::RandomTrackOrder);
REPLACE(0x00450470, Pcm::SetMusicLooping);
REPLACE(0x00450480, Pcm::GetTracksOff);
REPLACE(0x004504B0, Pcm::SetTracksOff);
REPLACE(0x004504E0, Pcm::SetUserSoundtrackVolume);
REPLACE_SOURCE_ONLY(0x004B2350, Pcm::GetNumSoundtracks); // a jump to 0x004542D0
REPLACE_SOURCE_ONLY(0x0048C150, Pcm::GetNumSoundtracks); // a jump to 0x004B2350
REPLACE(0x004B2360, Pcm::GetSoundtrackName);
REPLACE_SOURCE_ONLY(0x0048C160, Pcm::GetSoundtrackName); // a jump to 0x004B2360
