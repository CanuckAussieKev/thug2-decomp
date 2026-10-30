// Self-tests for the music's playlists and track commands (Pcm, 0x00450F70-0x004520D0), its streams and the PC's
// Bink stream file cache. Each runs the original and ours against recording stand-ins (standins.h) for the
// platform's music calls and compares the call traces and the memory they write.

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into stand-in data)

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/stubs.h"
#include "game/obj/stream_component.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream.h"
#include "game/pcm/stream_commands.h"
#include "game/sfx/sfx.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/gfx/viewport.h"
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
Pcm::Stream* Streams() { return reinterpret_cast<Pcm::Stream*>(0x0069B938); }

int g_streamState;    // 0x00453250's result (1: none playing)
bool g_startOk;       // 0x00450820's
bool g_fileThere;     // 0x005B9760's
int g_scriptFlag;     // Script::GetInteger(0x450BAE2D)

void StandInPlatformMusic(StandIns& s)
{
    s.At({0x00453400, AddressOf(&Pcm::PlatformSetMusicVolume)}, 0, [](Regs& r) {
        Record({0x3400, r.args[0]});
        return 1u;
    });
    s.At({0x004532C0, AddressOf(&Pcm::PauseStreams)}, 0, [](Regs& r) {
        Record({0x32C0, r.args[0] & 0xFF, r.args[1]});
        return 0u;
    });
    s.At({0x00450650, AddressOf(&Pcm::StopMusic)}, 0, [](Regs&) {
        Record({0x0650});
        return 0u;
    });
    s.At({0x00453250, AddressOf(&Pcm::MusicStatus)}, 0, [](Regs&) {
        Record({0x3250});
        return static_cast<uint32_t>(g_streamState);
    });
    s.At({0x00450820, AddressOf(&Pcm::PlayTrackByName)}, 0, [](Regs& r) {
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

// --- Pcm: the streams ----------------------------------------------------------------------------------------

std::vector<Saved> StreamGlobals()
{
    return {{0x00698060, 12, {}},  {0x0069B938, 0x15C, {}}, {0x0069BA94, 0x34, {}}, {0x0067D7E0, 0x1C, {}},
            {0x0069BAC8, 0x1008 + 0xDC, {}}, {0x007D6A38, 4, {}}, {0x007CEFA8, 4, {}}, {0x006F85A8, 4, {}}};
}
uint32_t StreamState()
{
    uint32_t h = HashBytes(reinterpret_cast<const uint8_t*>(0x00698060), 12);
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0069B938), 0x15C) * 3;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0069BA94), 0x34) * 5;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0067D7E0), 0x1C) * 7;
    h ^= HashBytes(reinterpret_cast<const uint8_t*>(0x0069BAC8), 0x1008 + 0xDC) * 11;
    return h;
}

bool g_channelBusy[4];  // 0x00453270 says 2 (playing) for these channels
bool g_platformOk;      // 0x00454080 / 0x00454200's
bool g_famOk;           // 0x00450540's
uint32_t g_frame;       // Tmr::GetRenderFrame's
bool g_noSound;         // 0x0047B340's
uint32_t g_emitter;     // 0x004A09E0's
bool g_direction;       // 0x00433D30's
uint32_t g_posVolume[5]; // what 0x0047BE10 writes into the volume
bool g_posOk;           // 0x0047BE10's
alignas(16) uint8_t g_soundManager[16];
alignas(16) uint8_t g_component[0x40];
alignas(16) uint8_t g_object[0x60];
alignas(16) uint8_t g_otherOwner[0x40];
alignas(16) float g_soundPos[4] = {1.5f, -2.0f, 3.25f, 1.0f};

void StandInStreams(StandIns& s)
{
    s.At({0x00453270, AddressOf(&Pcm::StreamStatus)}, 0, [](Regs& r) {
        Record({0x3270, r.args[0]});
        return r.args[0] < 4 && g_channelBusy[r.args[0]] ? 2u : 1u;
    });
    s.At({0x004532C0, AddressOf(&Pcm::PauseStreams)}, 0, [](Regs& r) {
        Record({0x32C0, r.args[0] & 0xFF, r.args[1]});
        return 0u;
    });
    s.At({0x00453B30, AddressOf(static_cast<void(__cdecl*)(int, int)>(&Pcm::StopStream))}, 0, [](Regs& r) {
        Record({0x3B30, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00454200, AddressOf(static_cast<bool(__cdecl*)(uint32_t, int, int)>(&Pcm::PlayStream))}, 0, [](Regs& r) {
        Record({0x4200, r.args[0], r.args[1], r.args[2]});
        return g_platformOk ? 1u : 0u;
    });
    s.At({0x00454080, AddressOf(&Pcm::LoadStream)}, 0, [](Regs& r) {
        const uint32_t* volume = reinterpret_cast<const uint32_t*>(r.args[2]);
        Record({0x4080, r.args[0], r.args[1], volume[0], volume[1], volume[2], volume[5], r.args[3], r.args[4],
                r.args[5] & 0xFF, r.args[6]});
        return g_platformOk ? 1u : 0u;
    });
    s.At({0x00450540, AddressOf(&Pcm::FamData::Load)}, 4, [](Regs& r) {
        Record({0x0540, r.ecx, r.args[0]});
        if (g_famOk) {
            reinterpret_cast<uint32_t*>(r.ecx)[0] = r.args[0];
            reinterpret_cast<uint32_t*>(r.ecx)[1] = 0x123;
        }
        return g_famOk ? 1u : 0u;
    });
    s.At({0x005BE8D0, AddressOf(&Tmr::GetRenderFrame)}, 0, [](Regs&) {
        Record({0xE8D0});
        return g_frame;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 0x77u;
    });
    s.At({0x005B9760, AddressOf(&Stubs::ReturnTrue)}, 0, [](Regs&) {
        Record({0x9760});
        return g_fileThere ? 1u : 0u;
    });
    s.At({0x0047B340, AddressOf(&Sfx::CSfxManager::IsPaused)}, 0, [](Regs& r) {
        Record({0xB340, r.ecx == reinterpret_cast<uint32_t>(g_soundManager)});
        return g_noSound ? 1u : 0u;
    });
    s.At({0x004A09E0, AddressOf(&Nx::GetClosestCamera)}, 0, [](Regs& r) {
        Record({0x09E0, r.args[0] - reinterpret_cast<uint32_t>(g_object), r.args[1]});
        return g_emitter;
    });
    s.At({0x00433D30, AddressOf(&Obj::CStreamComponent::GetStreamExtent)}, 8, [](Regs& r) {
        Record({0x3D30, r.ecx == reinterpret_cast<uint32_t>(g_component), r.args[0]});
        if (g_direction)
            std::memcpy(reinterpret_cast<void*>(r.args[1]), g_soundPos, 16);
        return g_direction ? 1u : 0u;
    });
    s.At({0x00433CD0, AddressOf(&Obj::CStreamComponent::GetStreamPosition)}, 8, [](Regs& r) {
        Record({0x3CD0, r.ecx == reinterpret_cast<uint32_t>(g_component), r.args[1]});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_soundPos, 16);
        return r.args[0];
    });
    s.At({0x0047BE10, AddressOf(&Sfx::CSfxManager::GetVolume)}, 0x18, [](Regs& r) {
        const float* pos = reinterpret_cast<const float*>(r.args[1]);
        const float* direction = reinterpret_cast<const float*>(r.args[5]);
        Record({0xBE10, r.ecx == reinterpret_cast<uint32_t>(g_soundManager), Bits(pos[0]), Bits(pos[2]), r.args[2],
                r.args[3], r.args[4], direction ? Bits(direction[1]) : 0xDEADu});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_posVolume, 20);
        return g_posOk ? 1u : 0u;
    });
}

void ResetStreams(int variant = 0)
{
    for (int i = 0; i < 3; ++i) {
        uint8_t* stream = reinterpret_cast<uint8_t*>(0x0069B938 + 0x74 * i);
        std::memset(stream, 0, 0x74);
        *reinterpret_cast<uint32_t*>(stream + 0) = 0x100 + i;   // name
        *reinterpret_cast<uint32_t*>(stream + 4) = 0x200 + i;   // id
        *reinterpret_cast<int*>(stream + 0x10) = i;            // channel
        *reinterpret_cast<int*>(stream + 0x14) = 50 + (i == 1 ? -10 : 0) + (i == 2 ? -10 : 0); // priorities 50 40 40
        *reinterpret_cast<uint32_t*>(stream + 0x24) = 1000 - 10 * i; // start frames 1000 990 980
        stream[0x28] = 0xCC; // (usePosInfo)
    }
    uint32_t* frame1 = reinterpret_cast<uint32_t*>(0x0069B938 + 0x74 * 1 + 0x24);
    uint32_t* frame2 = reinterpret_cast<uint32_t*>(0x0069B938 + 0x74 * 2 + 0x24);
    *frame2 = 995; // stream 2 newer than stream 1
    if (variant == 1)
        *frame2 = 990; // as old: the first stays
    if (variant == 2) {
        *frame1 = 0x90000000u; // (unsigned: stream 2 is older)
        *frame2 = 5;
    }
}

// The stream commands, with streamed sound off and on: PauseStream as PauseMusic; the header; IsStreamPlaying;
// the frame amplitude slot (load into a free slot or none, get, free, checksum 0); StartStream through PlayStream /
// PreLoadStream with free channels (each), all busy (lowest priority, oldest of equals - the start frames unsigned
// - priority too low, equal), the platform failing, blocked and paused; AddStream by owner and by id / name (playing
// or not, a second and third name); PlayStreamFromObject with no sound, positional (no emitter, an emitter whose
// volume is loud, quiet but audible, quiet and not; with and without a direction) or not, volumes 100 / 50 / NaN,
// an owner before or none.
void TestStreamCommands()
{
    std::vector<Saved> saved = StreamGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInStreams(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            for (Saved& sv : saved)
                std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
            GameVariable<void*>(0x006F85A8) = g_soundManager;
            *reinterpret_cast<uint32_t*>(g_soundManager + 4) = 4; // the sVolume kind its constructor copies
            auto state = [&](uint32_t tag) { Record({tag, StreamState()}); };
            auto setSound = [](int way) {
                GameVariable<void*>(0x007D6A38) = way == 1 ? nullptr : reinterpret_cast<void*>(0xD50D50);
                GameVariable<int>(0x007CEFA8) = way == 2 ? 2 : 1;
                GameVariable<bool>(0x0067D7E1) = way != 3;
            };

            // PauseStream, the header, IsStreamPlaying.
            const int pauses[] = {-1, 0, 1, 2, -1, 1, 1, -1, 0, -1, 5, -2};
            for (int way = 0; way < 4; ++way) {
                setSound(way);
                for (int before = 0; before < 2; ++before) {
                    GameVariable<uint8_t>(0x0069CAD1) = static_cast<uint8_t>(before);
                    for (int p : pauses) {
                        if (original)
                            Original<void(__cdecl*)(int)>(0x00451010)(p);
                        else
                            Pcm::PauseStream(p);
                        Record({GameVariable<uint8_t>(0x0069BAA9), GameVariable<uint8_t>(0x0069CAD1)});
                    }
                }
                ResetStreams();
                for (int busy = 0; busy < 3; ++busy) {
                    for (int i = 0; i < 4; ++i)
                        g_channelBusy[i] = i == busy;
                    for (uint32_t id : {0x100u, 0x201u, 0x102u, 0x5u}) {
                        bool playing = original ? Original<bool(__cdecl*)(uint32_t)>(0x00451660)(id) : Pcm::IsStreamPlaying(id);
                        Record({0x1660, playing});
                    }
                }
                for (int h = 0; h < 2; ++h) {
                    g_fileThere = h == 1;
                    GameVariable<bool>(0x0067D7E1) = way != 3;
                    bool loaded = original ? Original<bool(__cdecl*)(const char*)>(0x00451620)("streams")
                                           : Pcm::LoadStreamHeader("streams");
                    Record({0x1620, loaded, GameVariable<uint8_t>(0x0067D7E1)});
                }
            }

            // The frame amplitude slot.
            for (int ok = 0; ok < 2; ++ok) {
                g_famOk = ok != 0;
                for (uint32_t c : {0x11u, 0x22u, 0u, 0x11u}) {
                    Pcm::FrameAmp* got = original ? Original<Pcm::FrameAmp*(__cdecl*)(uint32_t)>(0x004516A0)(c)
                                                  : Pcm::LoadStreamFrameAmp(c);
                    Record({0x16A0, reinterpret_cast<uint32_t>(got)});
                    state(0x16A1);
                    for (uint32_t g : {0x11u, 0x22u, 0u}) {
                        got = original ? Original<Pcm::FrameAmp*(__cdecl*)(uint32_t)>(0x004516E0)(g) : Pcm::GetStreamFrameAmp(g);
                        Record({0x16E0, reinterpret_cast<uint32_t>(got)});
                    }
                }
                for (uint32_t c : {0x22u, 0u, 0x11u, 0x11u}) {
                    bool freed = original ? Original<bool(__cdecl*)(uint32_t)>(0x00451720)(c) : Pcm::FreeStreamFrameAmp(c);
                    Record({0x1720, freed});
                    state(0x1721);
                }
            }
            GameVariable<uint32_t>(0x0069BAC8) = 0x77; // stream 0x77's amplitudes loaded

            // PlayStream / PreLoadStream (and StartStream through them).
            struct Busy {
                bool channels[3];
            };
            const Busy busies[] = {{{false, false, false}}, {{true, false, false}}, {{true, true, false}},
                                   {{true, true, true}}};
            for (int way = 0; way < 4; ++way) {
                for (const Busy& b : busies) {
                    for (int priority : {39, 40, 41, 60}) {
                        for (int ok = 0; ok < 2; ++ok) {
                            if (way != 0 && (priority != 40 || !ok))
                                continue;
                            for (int call = 0; call < 5; ++call) {
                                setSound(way);
                                ResetStreams(call >= 3 ? call - 2 : 0);
                                for (int i = 0; i < 3; ++i)
                                    g_channelBusy[i] = b.channels[i];
                                g_platformOk = ok != 0;
                                g_frame = 4000 + call;
                                GameVariable<uint8_t>(0x0069BAAA) = 0;
                                GameVariable<uint8_t>(0x0069BAA9) = 0;
                                GameVariable<int>(0x0069BA94) = 0x66;
                                uint32_t volume[6] = {Bits(80.0f), Bits(70.0f), Bits(60.0f), 0, 0, 2};
                                uint32_t id;
                                if (call == 2 || call == 4) {
                                    id = original ? Original<uint32_t(__cdecl*)(uint32_t, int)>(0x00451A50)(0x77, priority)
                                                  : Pcm::PreLoadStream(0x77, priority);
                                } else {
                                    uint32_t want = call == 0 ? 0u : 0x201u;
                                    id = original ? Original<uint32_t(__cdecl*)(uint32_t, const void*, float, int, int, uint32_t)>(0x00451960)(
                                                        0x77, volume, 1.25f, 3, priority, want)
                                                  : Pcm::PlayStream(0x77, volume, 1.25f, 3, priority, want);
                                }
                                Record({0x1960, id});
                                state(0x1961);
                            }
                        }
                    }
                }
                // Blocked, paused; StartStream directly.
                setSound(way);
                ResetStreams();
                g_platformOk = true;
                for (int block = 1; block < 3; ++block) {
                    GameVariable<uint8_t>(0x0069BAAA) = block == 1;
                    GameVariable<uint8_t>(0x0069BAA9) = block == 2;
                    uint32_t volume[6] = {};
                    uint32_t id = original ? Original<uint32_t(__cdecl*)(uint32_t, const void*, float, int, int, uint32_t)>(0x00451960)(
                                                 0x77, volume, 1.0f, 0, 99, 0)
                                           : Pcm::PlayStream(0x77, volume, 1.0f, 0, 99, 0);
                    Record({0x1962, id});
                }
                GameVariable<uint8_t>(0x0069BAAA) = 0;
                GameVariable<uint8_t>(0x0069BAA9) = 0;
                for (int preload = 0; preload < 2; ++preload) {
                    uint32_t volume[6] = {Bits(1.0f), Bits(2.0f), 0, 0, 0, 1};
                    int channel = original ? Original<int(__cdecl*)(uint32_t, const void*, float, int, int, uint32_t, bool)>(0x00451770)(
                                                 0x33, volume, 0.5f, 7, 45, 0x44, preload != 0)
                                           : Pcm::StartStream(0x33, volume, 0.5f, 7, 45, 0x44, preload != 0);
                    Record({0x1770, static_cast<uint32_t>(channel)});
                    state(0x1771);
                }
            }

            // AddStream.
            setSound(0);
            for (int busy = 0; busy < 2; ++busy) {
                ResetStreams();
                for (int i = 0; i < 3; ++i)
                    g_channelBusy[i] = busy != 0 || i == 1;
                GameVariable<void*>(0x00698060) = g_otherOwner;
                GameVariable<void*>(0x00698064) = g_component;
                GameVariable<void*>(0x00698068) = g_component;
                for (void* owner : {static_cast<void*>(nullptr), static_cast<void*>(g_component)}) {
                    for (uint32_t stream : {0x100u, 0x101u, 0x102u, 0x201u, 0x77u}) {
                        for (int k = 0; k < 2; ++k) {
                            for (int i = 0; i < 3; ++i)
                                Streams()[i].nameCount = 1 + k + i;
                            bool added = original ? Original<bool(__cdecl*)(uint32_t, uint32_t, void*)>(0x00451B80)(stream, 0x77u - k, owner)
                                                  : Pcm::AddStream(stream, 0x77u - k, owner);
                            Record({0x1B80, added});
                            state(0x1B81);
                        }
                    }
                }
                setSound(3);
                bool added = original ? Original<bool(__cdecl*)(uint32_t, uint32_t, void*)>(0x00451B80)(0x100, 1, nullptr)
                                      : Pcm::AddStream(0x100, 1, nullptr);
                Record({0x1B82, added});
                setSound(0);
            }

            // PlayStreamFromObject.
            *reinterpret_cast<void**>(g_component + 0xC) = g_object;
            const uint32_t loud[5] = {Bits(10.0f), Bits(3.0f), 0, 0, 0};
            const uint32_t quiet[5] = {Bits(1.5f), Bits(-1.75f), 0, 0, 0};
            const uint32_t negLoud[5] = {Bits(0.5f), Bits(-2.0f), 0, 0, 0};
            const uint32_t* posVolumes[] = {loud, quiet, negLoud};
            int n = 0;
            for (int way = 0; way < 5; ++way) {
                for (int pos = 0; pos < 2; ++pos)
                    for (int emitter = 0; emitter < 2; ++emitter)
                        for (int pv = 0; pv < 3; ++pv)
                            for (int audible = 0; audible < 2; ++audible)
                                for (uint32_t vol : {Bits(100.0f), Bits(33.3f), 0x7FC00000u, Bits(77.7f)}) {
                                    if (way != 0 && (pos || emitter || pv || audible || vol != Bits(100.0f)))
                                        continue;
                                    if (!pos && (emitter || pv || audible))
                                        continue;
                                    ++n;
                                    setSound(way == 4 ? 0 : way);
                                    g_noSound = way == 4;
                                    ResetStreams();
                                    for (int i = 0; i < 3; ++i)
                                        g_channelBusy[i] = i != (n % 3);
                                    g_platformOk = n % 7 != 3;
                                    GameVariable<uint8_t>(0x0069BAAA) = 0;
                                    GameVariable<uint8_t>(0x0069BAA9) = 0;
                                    g_emitter = emitter ? 0xE111u : 0;
                                    g_direction = n & 1;
                                    std::memcpy(g_posVolume, posVolumes[pv], 20);
                                    g_posOk = audible != 0;
                                    std::memset(g_component, 0x11, 0x18);
                                    std::memset(g_component + 0x18, 0, 12);
                                    std::memset(g_otherOwner, 0x22, sizeof(g_otherOwner));
                                    for (int i = 0; i < 3; ++i)
                                        GameVariable<void*>(0x00698060 + 4 * i) = (n & 2) ? g_otherOwner : nullptr;
                                    uint32_t id = original
                                        ? Original<uint32_t(__cdecl*)(void*, uint32_t, float, float, float, int, int, int, int, uint32_t)>(0x00451E20)(
                                              g_component, 0x77, 640.0f, Float(vol), 0.75f, 2, 60, pos, 3, (n & 4) ? 0x500u : 0u)
                                        : Pcm::PlayStreamFromObject(g_component, 0x77, 640.0f, Float(vol), 0.75f, 2, 60, pos, 3,
                                                                    (n & 4) ? 0x500u : 0u);
                                    Record({0x1E20, id, HashBytes(g_component, sizeof(g_component)),
                                            HashBytes(g_otherOwner, sizeof(g_otherOwner))});
                                    state(0x1E21);
                                }
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "music: the stream commands (0x00451010-0x00451E20)");
    Check(traces[0].size() > 2000, "music: stream commands test ran its cases");
}

// --- Pcm: the Bink stream files -------------------------------------------------------------------------------

alignas(16) uint8_t g_heap[16][64];
int g_heapUsed;
bool g_fopenOk;
bool g_binkOk;
int g_soundSystemOk;
alignas(16) uint8_t g_bink[16];
alignas(16) uint8_t g_binkStream[0x1C0];

uint32_t HeapIndex(uint32_t p)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_heap);
    return p >= base && p < base + sizeof(g_heap) ? (p - base) / 64 : 0xEEEE0000u ^ p;
}

int __stdcall PmSoundSystem(void* open, void* param)
{
    Record({0x5355, reinterpret_cast<uint32_t>(open), reinterpret_cast<uint32_t>(param)});
    return g_soundSystemOk;
}
void* __stdcall PmBinkOpen(const void* source, uint32_t flags)
{
    Record({0xB10, HeapIndex(reinterpret_cast<uint32_t>(source)), flags});
    return g_binkOk ? g_bink : nullptr;
}
int __stdcall PmVideoOnOff(void* bink, int on)
{
    Record({0x0F0F, bink == g_bink, static_cast<uint32_t>(on)});
    return 0;
}

// The cache: first use (all slots set up), hits (users counted), misses into free slots (the last free one), then
// with all 12 full the one unused longest among those with no users (users set by hand; equal ages: the first),
// none (all in use), the file missing; Bink opens of new and cached files, the sound system set once (failing:
// the message), Bink failing, the file not read.
void TestBinkStreamFiles()
{
    std::vector<Saved> saved = {{0x0069CAD0, 0xD4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        s->At({0x006241E8}, 0, [](Regs& r) {
            Record({0xF0, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
            return g_fopenOk ? 0xF11Eu : 0u;
        });
        s->At({0x0062411B}, 0, [](Regs& r) {
            Record({0xF4EE, HeapIndex(r.args[0])});
            return 0u;
        });
        s->At({0x006240D2}, 0, [](Regs& r) {
            Record({0x5EE, r.args[0], r.args[1], r.args[2]});
            return 0u;
        });
        s->At({0x00624031}, 0, [](Regs& r) {
            Record({0xA110, r.args[0]});
            uint8_t* block = g_heap[g_heapUsed++ % 16];
            return reinterpret_cast<uint32_t>(block);
        });
        s->At({0x00623F3E}, 0, [](Regs& r) {
            Record({0x4EAD, HeapIndex(r.args[0]), r.args[1], r.args[2], r.args[3]});
            std::memcpy(reinterpret_cast<void*>(r.args[0]), "BIKi", 4);
            reinterpret_cast<uint8_t*>(r.args[0])[3] = static_cast<uint8_t>(r.args[1]);
            return 1u;
        });
        s->At({0x00623E04}, 0, [](Regs& r) {
            Record({0xC105, r.args[0]});
            return 0u;
        });
        s->At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
            Record({0x9F, Hash(reinterpret_cast<const char*>(r.args[0]))});
            return 0u;
        });
        s->At({0x004535E0, AddressOf(&Pcm::NewOverlapped)}, 0, [](Regs&) {
            Record({0x35E0});
            return 0xBBBBu;
        });
        void* soundSystem = s->Thunk(8, [](Regs& r) { return static_cast<uint32_t>(PmSoundSystem(reinterpret_cast<void*>(r.args[0]), reinterpret_cast<void*>(r.args[1]))); });
        void* open = s->Thunk(8, [](Regs& r) { return reinterpret_cast<uint32_t>(PmBinkOpen(reinterpret_cast<void*>(r.args[0]), r.args[1])); });
        void* video = s->Thunk(8, [](Regs& r) { return static_cast<uint32_t>(PmVideoOnOff(reinterpret_cast<void*>(r.args[0]), static_cast<int>(r.args[1]))); });
        s->Bytes(0x00645304, Dword(reinterpret_cast<uint32_t>(soundSystem)));
        s->Bytes(0x006452E8, Dword(reinterpret_cast<uint32_t>(open)));
        s->Bytes(0x006452FC, Dword(reinterpret_cast<uint32_t>(video)));
        uint32_t savedDsoundOpen = GameVariable<uint32_t>(0x00645300);
        (void)savedDsoundOpen;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            std::memset(reinterpret_cast<void*>(0x0069CAD0), 0x5C, 0xD4);
            GameVariable<uint8_t>(0x0069CBA0) = 0;
            GameVariable<uint8_t>(0x0069CB99) = 0;
            g_heapUsed = 0;
            g_fopenOk = true;
            auto load = [&](uint32_t key, long offset) {
                int slot = original ? Original<int(__cdecl*)(uint32_t, const char*, long, size_t)>(0x00452A90)(key, "pcm.wad", offset, 32 + key % 7)
                                    : Pcm::LoadStreamFile(key, "pcm.wad", offset, 32 + key % 7);
                Record({0x2A90, static_cast<uint32_t>(slot), HashBytes(reinterpret_cast<const uint8_t*>(0x0069CAD8), 0xC0),
                        GameVariable<uint32_t>(0x0069CB9C)});
            };
            // Fill slots with a few hits between.
            for (uint32_t key = 1; key <= 12; ++key) {
                load(key, 100 * key);
                if (key % 4 == 0)
                    load(key - 2, 5);
            }
            // All full: users of the slots set by hand.
            auto users = [](int slot) -> int& { return GameVariable<int>(0x0069CAD8 + 16 * slot + 12); };
            for (int i = 0; i < 12; ++i)
                users(i) = (i % 3 == 0) ? 0 : 1;
            GameVariable<uint32_t>(0x0069CAD8 + 16 * 3 + 8) = 1; // slots 3 and 6 unused as long: the first goes
            GameVariable<uint32_t>(0x0069CAD8 + 16 * 6 + 8) = 1;
            for (uint32_t key = 20; key < 26; ++key)
                load(key, 7);
            for (int i = 0; i < 12; ++i)
                users(i) = 2;
            load(30, 1); // none free
            users(5) = 0;
            GameVariable<uint32_t>(0x0069CAD8 + 16 * 5 + 8) = GameVariable<uint32_t>(0x0069CB9C) + 5; // "newer" than now
            users(9) = 0;
            load(31, 2);
            users(9) = 0;
            g_fopenOk = false;
            load(32, 3);
            g_fopenOk = true;
            // A slot emptied by hand: it's free.
            GameVariable<uint32_t>(0x0069CAD8 + 16 * 2 + 4) = 0;
            GameVariable<uint32_t>(0x0069CAD8 + 16 * 7 + 4) = 0;
            load(33, 4);

            // The Bink stream.
            for (int i = 0; i < 6; ++i) {
                std::memset(g_binkStream, 0xA0 + i, sizeof(g_binkStream));
                bool flag = i & 1;
                Pcm::BinkStreamView* view = reinterpret_cast<Pcm::BinkStreamView*>(g_binkStream);
                void* got = original ? Original<void*(__thiscall*)(void*, bool, int)>(0x00452C00)(g_binkStream, flag, 0x700 + i)
                                     : view->Construct(flag, 0x700 + i);
                Record({0x2C00, got == g_binkStream, HashBytes(g_binkStream, sizeof(g_binkStream))});
                g_soundSystemOk = i == 2 ? 1 : 0;
                g_binkOk = i != 3;
                g_fopenOk = i != 4;
                if (i == 1)
                    GameVariable<uint8_t>(0x0069CB99) = 0;
                for (int k = 0; k < 12; ++k)
                    users(k) = 0;
                uint32_t key = i < 2 ? 33u : 40u + i;
                long result = original ? Original<long(__thiscall*)(void*, uint32_t, const char*, long, size_t)>(0x00452C60)(g_binkStream, key, "s.wad", 60 + i, 40)
                                       : view->Open(key, "s.wad", 60 + i, 40);
                Record({0x2C60, static_cast<uint32_t>(result), HashBytes(g_binkStream, sizeof(g_binkStream)),
                        GameVariable<uint8_t>(0x0069CB99), HashBytes(reinterpret_cast<const uint8_t*>(0x0069CAD8), 0xC0)});
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "music: the Bink stream files (0x00452A90-0x00452C60)");
    Check(traces[0].size() > 200, "music: Bink stream files test ran its cases");
}

} // namespace

SELFTEST(Early, TestPlaylistCommands, RemoveStandIns);
SELFTEST(Early, TestStreamCommands, RemoveStandIns);
SELFTEST(Early, TestBinkStreamFiles, RemoveStandIns);
