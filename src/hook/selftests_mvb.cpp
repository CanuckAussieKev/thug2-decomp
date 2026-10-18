// Self-tests for the music's playlists and track commands (Pcm, 0x00450F70-0x004520D0), its streams and the PC's
// Bink stream file cache. Each runs the original and ours against recording stand-ins (standins.h) for the
// platform's music calls and compares the call traces and the memory they write.

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into stand-in data)

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/stubs.h"
#include "game/pcm/music.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

std::vector<Saved> PlaylistGlobals()
{
    return {{0x0069806C, 0x2C + 3 * 0xCB0, {}}, {0x006999F8, 8000, {}}, {0x0069B938, 0x15C, {}},
            {0x0069BA94, 0x34, {}},          {0x0067D7E0, 0x1C, {}},  {0x0069CAD0, 4, {}},
            {0x00684FA8, 1, {}},             {0x006976FC, 4 + 4 * 600, {}}, {0x007D6A38, 4, {}},
            {0x007CEFA8, 4, {}},             {0x0067DA38, 4, {}},     {0x00696D9C, 4 + 4 * 130, {}}};
}
uint32_t PlaylistState()
{
    uint32_t h = HashBytes(reinterpret_cast<const uint8_t*>(0x0069806C), 0x2C + 3 * 0xCB0);
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x006999F8), 8000) * 3;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0069BA94), 0x34) * 5;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0067D7E0), 0x1C) * 7;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0069CAD0), 4) * 11;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x00697700), 4 * 600) * 13;
    return h;
}

Pcm::Playlist* Lists() { return reinterpret_cast<Pcm::Playlist*>(0x00698098); }

int g_streamState;    // 0x00453250's result (1: none playing)
bool g_startOk;       // 0x00450820's
bool g_fileThere;     // 0x005B9760's
int g_scriptFlag;     // Script::GetInteger(0x450BAE2D)

void StandInPlatformMusic(StandIns& s)
{
    s.At({0x00453400}, 0, [](Regs& r) {
        Record({0x3400, r.args[0]});
        return 1u;
    });
    s.At({0x004532C0}, 0, [](Regs& r) {
        Record({0x32C0, r.args[0] & 0xFF, r.args[1]});
        return 0u;
    });
    s.At({0x00450650}, 0, [](Regs&) {
        Record({0x0650});
        return 0u;
    });
    s.At({0x00453250}, 0, [](Regs&) {
        Record({0x3250});
        return static_cast<uint32_t>(g_streamState);
    });
    s.At({0x00450820}, 0, [](Regs& r) {
        Record({0x0820, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return g_startOk ? 1u : 0u;
    });
    s.At({0x005B9760, AddressOf(&Stubs::ReturnTrue)}, 0, [](Regs&) {
        Record({0x9760});
        return g_fileThere ? 1u : 0u;
    });
    s.At({0x00450310, AddressOf(&Pcm::ShuffleUserSoundtrack)}, 0, [](Regs&) {
        Record({0x0310});
        return 0u;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return r.args[0] == 0x450BAE2Du ? static_cast<uint32_t>(g_scriptFlag) : 0x77u;
    });
}

// Sound on (with the music header loaded), or off one of three ways.
void SetSound(int way)
{
    GameVariable<void*>(0x007D6A38) = way == 1 ? nullptr : reinterpret_cast<void*>(0xD50D50);
    GameVariable<int>(0x007CEFA8) = way == 2 ? 2 : 1;
    GameVariable<bool>(0x0067D7E0) = way != 3;
}

// The playlist commands, each with the sound off and on, over their branches: PauseMusic's -1 / 0 / 1 / 2 with the
// paused state before set or not; the volumes in and out of 0-100 (NaN, -0, 100.5) with the stream type, the
// initialisation, the playlist and the script flag; tracks added to all three lists (upper-casing, characters
// over 0x7F, titles long, short, none, the 80-track limit, a file that isn't there); clearing; skipping; the random
// mode's signs; the track volume; IsTrackPlaying / GetNumTracks / GetTrackName (slashes, none, music off - the
// "Empty." write); SetMusicMode; LoadMusicHeader; ChangeTrackState on and off, both halves, the track playing or
// not, empty and full playlists; PlayTrack started or not, the debug checks, a name found in the list or not.
void TestPlaylistCommands()
{
    std::vector<Saved> saved = PlaylistGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInPlatformMusic(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            for (Saved& sv : saved)
                std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
            GameVariable<uint32_t>(0x0067DA38) = Bits(37.5f); // the sound effects volume
            auto state = [&](uint32_t tag) { Record({tag, PlaylistState()}); };

            // PauseMusic
            const int pauses[] = {-1, 0, 1, 2, -1, 1, 1, -1, 0, -1, 5, -2};
            for (int way = 0; way < 4; ++way) {
                SetSound(way);
                for (int before = 0; before < 2; ++before) {
                    GameVariable<uint8_t>(0x0069CAD0) = static_cast<uint8_t>(before);
                    for (int p : pauses) {
                        if (original)
                            Original<void(__cdecl*)(int)>(0x00450F70)(p);
                        else
                            Pcm::PauseMusic(p);
                        state(0x0F70);
                    }
                }
            }

            // The volumes.
            const uint32_t volumes[] = {Bits(50.0f), Bits(100.0f), Bits(100.5f), 0x80000000u, Bits(-1.0f),
                                        0x7FC00000u, 0u, Bits(0.25f)};
            for (int way = 0; way < 4; ++way) {
                SetSound(way);
                for (int init = 0; init < 2; ++init)
                    for (int type = 0; type < 3; ++type)
                        for (int list = 0; list < 3; list += 1)
                            for (int flag = 0; flag < 2; ++flag) {
                                if (way != 0 && (type || list || flag || init))
                                    continue;
                                GameVariable<uint8_t>(0x0069BAA1) = static_cast<uint8_t>(init ? 0x80 : 0);
                                GameVariable<int>(0x0069BAAC) = type;
                                GameVariable<int>(0x0069BA9C) = list;
                                g_scriptFlag = flag * 3;
                                for (uint32_t v : volumes) {
                                    if (original)
                                        Original<void(__cdecl*)(float)>(0x004510B0)(Float(v));
                                    else
                                        Pcm::SetMusicVolume(Float(v));
                                    state(0x10B0);
                                    if (original)
                                        Original<void(__cdecl*)(float)>(0x00451130)(Float(v ^ 1));
                                    else
                                        Pcm::SetMusicStreamVolume(Float(v ^ 1));
                                    state(0x1130);
                                }
                            }
            }

            // Adding tracks.
            static char longTitle[150];
            for (int i = 0; i < 149; ++i)
                longTitle[i] = static_cast<char>('A' + i % 26);
            const char* names[] = {"music\\abc_Track01", "Z\xE9z\x7Bq`", "", "data/Streams/xyz", "aZ{@"};
            const char* titles[] = {"Title one", nullptr, longTitle, ""};
            std::memset(reinterpret_cast<void*>(0x006999F8), 0x5A, 8000); // (titles: the terminator shows)
            for (int way = 0; way < 4; ++way) {
                SetSound(way);
                for (int list = 0; list < 3; ++list) {
                    Lists()[list].unknownCA8 = 1;
                    for (int i = 0; i < 12; ++i) {
                        g_fileThere = i % 5 != 3;
                        if (original)
                            Original<void(__cdecl*)(const char*, int, const char*)>(0x004511A0)(names[i % 5], list, titles[i % 4]);
                        else
                            Pcm::AddTrackToPlaylist(names[i % 5], list, titles[i % 4]);
                        state(0x11A0);
                    }
                }
                if (way == 0) {
                    g_fileThere = true;
                    Lists()[1].count = 79;
                    for (int i = 0; i < 3; ++i) {
                        if (original)
                            Original<void(__cdecl*)(const char*, int, const char*)>(0x004511A0)("last", 1, "t");
                        else
                            Pcm::AddTrackToPlaylist("last", 1, "t");
                        state(0x11A1);
                    }
                    Lists()[1].count = 9;
                }
            }

            // Getters.
            SetSound(0);
            Lists()[2].count = 4;
            std::strcpy(reinterpret_cast<char*>(Lists()[2].tracks[0]), "A\\B/C\\name");
            std::strcpy(reinterpret_cast<char*>(Lists()[2].tracks[1]), "plain");
            std::strcpy(reinterpret_cast<char*>(Lists()[2].tracks[2]), "end\\");
            std::strcpy(reinterpret_cast<char*>(Lists()[2].tracks[3]), "x/y");
            std::strcpy(reinterpret_cast<char*>(Lists()[2].tracks[4]), "plain"); // (one past the count)
            std::memset(Lists()[0].tracks[0], 'Q', 39);
            Lists()[0].tracks[0][39] = 0;
            std::strcpy(reinterpret_cast<char*>(0x0069806C), "plain");
            for (int way = 0; way < 4; ++way) {
                SetSound(way);
                for (int list = 0; list < 3; ++list) {
                    int n = original ? Original<int(__cdecl*)(int)>(0x004514C0)(list) : Pcm::GetNumTracks(list);
                    Record({0x14C0, static_cast<uint32_t>(n)});
                    for (int track = 0; track < 5; ++track) {
                        const char* name = original ? Original<const char*(__cdecl*)(int, int)>(0x00451500)(track, list)
                                                    : Pcm::GetTrackName(track, list);
                        Record({0x1500, reinterpret_cast<uint32_t>(name), Hash(name)});
                        for (int st = 1; st < 3; ++st) {
                            g_streamState = st;
                            bool playing = original ? Original<bool(__cdecl*)(int, int)>(0x00451450)(list, track)
                                                    : Pcm::IsTrackPlaying(list, track);
                            Record({0x1450, playing});
                        }
                    }
                    state(0x1501);
                }
                for (int h = 0; h < 2; ++h) {
                    GameVariable<uint8_t>(0x0067D7E0) = way == 3 ? 0 : 1;
                    bool loaded = original ? Original<bool(__cdecl*)(const char*)>(0x004515E0)("music")
                                           : Pcm::LoadMusicHeader("music");
                    Record({0x15E0, loaded});
                    state(0x15E1);
                    g_fileThere = !g_fileThere;
                }
                SetSound(way);
            }

            // ClearPlaylist, SkipMusicTrack, SetRandomMode, ApplyTrackVolume, SetMusicMode.
            for (int way = 0; way < 4; ++way) {
                for (int list = 0; list < 3; ++list) {
                    SetSound(way);
                    GameVariable<int>(0x0069BA9C) = list;
                    Lists()[list].off[0] = 0x1234;
                    Lists()[list].off[1] = 0x5678ull << 32;
                    Lists()[list].played[0] = 0x9ABCull << 40;
                    Lists()[list].played[1] = 0xDEF0;
                    GameVariable<int>(0x0069BAA4) = 0x55;
                    for (int mode : {0, 2, -3}) {
                        GameVariable<int>(0x00697700) = 400 + mode;
                        GameVariable<int>(0x0069BAC0) = 0x33;
                        if (original)
                            Original<void(__cdecl*)(int)>(0x00451380)(mode);
                        else
                            Pcm::SetRandomMode(mode);
                        state(0x1380);
                        Lists()[list].played[0] = 0x77;
                    }
                    if (original)
                        Original<void(__cdecl*)()>(0x00451350)();
                    else
                        Pcm::SkipMusicTrack();
                    state(0x1350);
                    for (int user = 0; user < 2; ++user)
                        for (int flag = 0; flag < 2; ++flag) {
                            GameVariable<uint8_t>(0x0069BAB0) = static_cast<uint8_t>(user);
                            g_scriptFlag = flag;
                            GameVariable<uint32_t>(0x0067D7E4) = Bits(61.0f + user);
                            if (original)
                                Original<void(__cdecl*)()>(0x00451400)();
                            else
                                Pcm::ApplyTrackVolume();
                            state(0x1400);
                        }
                    for (int to : {list, (list + 1) % 3}) {
                        if (original)
                            Original<void(__cdecl*)(int)>(0x004515A0)(to);
                        else
                            Pcm::SetMusicMode(to);
                        state(0x15A0);
                        GameVariable<int>(0x0069BA9C) = list;
                    }
                    if (original)
                        Original<void(__cdecl*)(int)>(0x004512D0)(list);
                    else
                        Pcm::ClearPlaylist(list);
                    state(0x12D0);
                }
            }

            // ChangeTrackState.
            for (int way = 0; way < 4; ++way) {
                SetSound(way);
                for (int count : {0, 3, 100}) {
                    for (int list = 0; list < 3; list += 2) {
                        Lists()[list].count = count;
                        Lists()[list].off[0] = ~0ull;
                        Lists()[list].off[1] = 0xFFFFFFFFull;
                        Lists()[list].unknownCA8 = 0x5A;
                        for (int track = 0; track < 100; track += 7)
                            std::strcpy(reinterpret_cast<char*>(Lists()[list].tracks[track]), track == 14 ? "plain" : "other");
                        std::strcpy(reinterpret_cast<char*>(0x0069806C), "plain");
                        for (int track : {14, 1, 63, 64, 70, 99, 127, 130, -1}) {
                            for (int off = 1; off >= 0; --off) {
                                g_streamState = 2 - (track & 1);
                                GameVariable<uint8_t>(0x0067D7F4) = 0;
                                if (original)
                                    Original<void(__cdecl*)(int, bool, int)>(0x00451C70)(track, off != 0, list);
                                else
                                    Pcm::ChangeTrackState(track, off != 0, list);
                                state(0x1C70);
                            }
                            if (original)
                                Original<void(__cdecl*)(int, bool, int)>(0x00451C70)(track, true, list);
                            else
                                Pcm::ChangeTrackState(track, true, list);
                            state(0x1C71);
                        }
                    }
                }
            }

            // PlayTrack.
            for (int way = 0; way < 4; ++way) {
                SetSound(way);
                for (int type = 0; type < 2; ++type)
                    for (int list = 0; list < 3; ++list)
                        for (int ok = 0; ok < 2; ++ok)
                            for (int debug = 0; debug < 2; ++debug)
                                for (int flag = 0; flag < 2; ++flag) {
                                    if (way != 0 && (type || list || debug || flag))
                                        continue;
                                    GameVariable<int>(0x0069BAAC) = type;
                                    GameVariable<int>(0x0069BA9C) = list;
                                    GameVariable<uint32_t>(0x0067D7E4) = Bits(42.0f);
                                    g_startOk = ok != 0;
                                    GameVariable<uint8_t>(0x00684FA8) = static_cast<uint8_t>(debug);
                                    g_scriptFlag = flag;
                                    Lists()[list].count = 20;
                                    Lists()[list].off[0] = 0;
                                    for (int track = 0; track < 20; ++track)
                                        std::strcpy(reinterpret_cast<char*>(Lists()[list].tracks[track]), track == 13 ? "song13" : "else");
                                    for (const char* name : {"song13", "missing", "else"}) {
                                        bool loop = name[0] == 'm';
                                        if (original)
                                            Original<void(__cdecl*)(const char*, bool)>(0x004520D0)(name, loop);
                                        else
                                            Pcm::PlayTrack(name, loop);
                                        state(0x20D0);
                                    }
                                }
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "music: the playlist commands (0x00450F70-0x004520D0)");
    Check(traces[0].size() > 3000, "music: playlist commands test ran its cases");
}

} // namespace

SELFTEST(Early, TestPlaylistCommands, RemoveStandIns);
