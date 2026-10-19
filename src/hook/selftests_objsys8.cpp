// Self-tests for the objsys agent's batch OS-8: the sound effects manager's tables, volumes and parameters
// (src/game/sfx/sfx.cpp, 0x0047AC10-0x0047B8DB). The original and ours on the same sound state (the wave tables,
// the playing sounds, the positional list, the settings - random but consistent), with the driver's voices, the
// wave loader, script globals and the soundtrack stood in for; every call, result and the whole state compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/pcm/music.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/objsys_testkit.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float FromBits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// The sound state's globals.
const std::initializer_list<std::pair<uint32_t, uint32_t>> kSfxGlobals = {
    {0x006F3668, 0x6F85C0 - 0x6F3668}, // positional pool, wave tables, playing sounds, the counts and lists
    {0x0067DA34, 0x10},                // default dropoff, main volume, pitch, (+0x40 the frequency switch)
    {0x0067DB08, 8},                   // the voices' volume and pitch
    {0x00684FA8, 8},                   // quiet misses, the sfx volume setting
    {0x006FC49C, 1}, {0x006FA7D4, 1},  // the pause flags
    {0x007D6A38, 4}, {0x007CEFA8, 4},  // the DirectSound device, the platform
};

Lcg g_sfxAnswers(1);
uint32_t g_voicePlaying; // bit per voice

void StandInSfxWorld(StandIns& s)
{
    s.At({0x0047C7E0}, 0, [](Regs& r) {
        uint32_t playing = (g_voicePlaying >> (r.args[0] & 31)) & 1;
        Record({0x701CE000, r.args[0], playing});
        return playing;
    });
    s.At({0x0047C7A0}, 0, [](Regs& r) {
        Record({0x701CE001, r.args[0]});
        return 0u;
    });
    s.At({0x0047CD70}, 0, [](Regs& r) {
        auto* volume = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0x701CE002, r.args[0], volume[0], volume[1], volume[2], volume[3], volume[4], volume[5], r.args[2]});
        return 0u;
    });
    s.At({0x0047CBD0}, 0, [](Regs&) {
        Record({0x701CE003});
        return 0u;
    });
    s.At({0x0047CC10}, 0, [](Regs&) {
        Record({0x701CE004});
        return 0u;
    });
    s.At({0x0047CA50}, 0, [](Regs& r) {
        Record({0x701CE005, r.args[0]});
        return 0u;
    });
    s.At({0x0047D390}, 0, [](Regs&) {
        Record({0x701CE006});
        return 0u;
    });
    s.At({0x0047CC70}, 0, [](Regs& r) {
        uint32_t ok = g_sfxAnswers.Below(4) != 0;
        auto* slot = reinterpret_cast<uint32_t*>(r.args[2]);
        if (ok) {
            slot[0] = 0x0A0E0000 + g_sfxAnswers.Below(100);
            reinterpret_cast<uint8_t*>(slot)[4] = static_cast<uint8_t>(g_sfxAnswers.Below(2));
            reinterpret_cast<uint8_t*>(slot)[5] = r.args[3] == 0;
        }
        Record({0x701CE007, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[3], r.args[4], ok});
        return ok;
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        uint32_t value = g_sfxAnswers.Below(2);
        Record({0x6E140000, r.args[0], value});
        return value;
    });
    s.At({0x004504E0, AddressOf(&Pcm::SetUserSoundtrackVolume)}, 0, [](Regs& r) {
        Record({0x5A770000, r.args[0]});
        return 0u;
    });
}

void RandomState(Lcg& random)
{
    // the positional pool: some in use, linked
    auto* pool = reinterpret_cast<uint32_t*>(0x006F3668);
    for (int i = 0; i < 128; ++i) {
        uint32_t* e = pool + 6 * i;
        e[0] = 0x0D000000 + i;
        e[1] = 0x0C000000 + i;
        e[2] = random.Below(3) == 0 ? 1 : (random.Below(5) == 0 ? 2 : 0);
        e[3] = 0x0B000000 + i;
        e[4] = 0;
        e[5] = 0;
    }
    GameVariable<uint32_t>(0x006F85B0) = random.Below(2) ? reinterpret_cast<uint32_t>(pool + 6 * 5) : 0;
    static const int kCounts[] = {0, 3, 127, 128};
    GameVariable<int>(0x006F85BC) = random.Pick(kCounts);
    if (random.Below(4) == 0) // (every entry in use)
        for (int i = 0; i < 128; ++i)
            pool[6 * i + 2] = 1;
    // the wave tables
    for (uint32_t table : {0x006F4268u, 0x006F5E68u}) {
        auto* e = reinterpret_cast<uint8_t*>(table);
        for (int i = 0; i < 256; ++i, e += 0x1C) {
            auto* w = reinterpret_cast<uint32_t*>(e);
            w[0] = 0xC5000000 + random.Below(40); // (duplicates: the first wins)
            e[4] = static_cast<uint8_t>(random.Next());
            e[5] = static_cast<uint8_t>(random.Next());
            e[6] = static_cast<uint8_t>(random.Below(3));
            e[7] = static_cast<uint8_t>(random.Below(3));
            static const float kPercent[] = {100.0f, 100.0f, 50.0f, 120.5f};
            w[2] = Bits(random.Pick(kPercent));
            w[3] = Bits(random.Pick(kPercent));
            static const float kDropoff[] = {0.0f, 0.0f, 300.0f, 25.5f};
            w[4] = Bits(random.Pick(kDropoff));
            w[5] = 0x0A0E0000 + i;
            e[0x18] = static_cast<uint8_t>(random.Below(2));
            e[0x19] = static_cast<uint8_t>(random.Below(2));
        }
    }
    static const int kTableCounts[] = {0, 1, 10, 40, 255, 256};
    GameVariable<int>(0x006F85B4) = random.Pick(kTableCounts);
    GameVariable<int>(0x006F85B8) = random.Pick(kTableCounts);
    // the playing sounds: ids, names, entries, props
    auto* playing = reinterpret_cast<uint8_t*>(0x006F7A68);
    for (int i = 0; i < 48; ++i, playing += 0x3C) {
        auto* p = reinterpret_cast<uint32_t*>(playing);
        p[0] = random.Below(3) == 0 ? 0 : 0x1D000000 + random.Below(20);
        p[1] = 0xC5000000 + random.Below(40);
        p[2] = random.Below(5) == 0 ? 0 : 0x006F4268 + 0x1C * random.Below(256);
        static const float kPercent[] = {100.0f, 100.0f, 80.0f, 133.0f};
        p[5] = Bits(random.Pick(kPercent));
        p[6] = Bits(random.Pick(kPercent));
    }
    g_voicePlaying = random.Next() | random.Next() << 24;
    // the manager instance: none, or one whose default volume type is any of them
    alignas(4) static uint32_t instance[2];
    instance[0] = 0;
    instance[1] = random.Below(4);
    GameVariable<uint32_t>(0x006F85A8) = random.Below(3) ? reinterpret_cast<uint32_t>(instance) : 0;
    // settings
    GameVariable<float>(0x0067DA34) = 1020.0f;
    GameVariable<float>(0x0067DA38) = 100.0f;
    GameVariable<float>(0x0067DA3C) = 1.0f;
    GameVariable<uint8_t>(0x00684FA8) = static_cast<uint8_t>(random.Below(2));
    GameVariable<float>(0x00684FAC) = random.Below(2) ? 100.0f : 37.5f;
    GameVariable<uint8_t>(0x006FC49C) = static_cast<uint8_t>(random.Below(2));
    GameVariable<uint8_t>(0x006FA7D4) = static_cast<uint8_t>(random.Below(2));
    static const uint32_t kPlatforms[] = {0, 1, 2, 6, 0, 0};
    GameVariable<uint32_t>(0x007CEFA8) = random.Pick(kPlatforms);
    GameVariable<uint32_t>(0x007D6A38) = random.Below(6) ? 0x0D5D0000 : 0;
}

void RandomVolume(Lcg& random, uint32_t* volume)
{
    static const float kLevels[] = {0.0f, 0.0f, 100.0f, -100.0f, -50.0f, 33.25f, 7.0f};
    bool silent = random.Below(4) == 0;
    for (int i = 0; i < 5; ++i)
        volume[i] = silent ? 0 : Bits(random.Pick(kLevels));
    static const int kTypes[] = {0, 1, 2, 3};
    volume[5] = random.Pick(kTypes);
}

void PushState()
{
    for (auto [address, size] : kSfxGlobals) {
        for (uint32_t i = 0; i + 4 <= size; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(address + i));
        if (size & 3)
            g_trace.push_back(*reinterpret_cast<uint8_t*>(address + (size & ~3u)));
    }
}

void TestSfxManager()
{
    ObjsysTests::SaveGlobals(kSfxGlobals);
    StandIns& s = *new StandIns();
    StandInSfxWorld(s);
    alignas(16) static uint8_t manager[8];
    alignas(16) static uint32_t volume[6];
    alignas(16) static uint8_t props[0x30];
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x5F000000 + seed);
            g_sfxAnswers = Lcg(0x5F0A0000 + seed);
            RandomState(random);
            std::memset(manager, 0, sizeof(manager));
            manager[0] = static_cast<uint8_t>(random.Below(3) == 0);
            void* m = manager;
            for (int step = 0; step < 30; ++step) {
                uint32_t id = random.Below(4) == 0 ? 0xC5000000 + random.Below(40) : 0x1D000000 + random.Below(22);
                switch (random.Below(24)) {
                case 0: {
                    static const uint32_t kNames[] = {0xF221474C, 0x9706104B, 0x0CA64C41, 0xEF082878, 0x12345678};
                    Record({Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0047AC10)(random.Pick(kNames))});
                    break;
                }
                case 1: {
                    RandomVolume(random, volume);
                    static const float kPercent[] = {50.0f, 100.0f, 7.5f, 0.0f};
                    Fn<void(__thiscall*)(void*, float)>(original, 0x0047AC50)(volume, random.Pick(kPercent));
                    Record({volume[0], volume[1], volume[2], volume[3], volume[4]});
                    break;
                }
                case 2:
                    RandomVolume(random, volume);
                    Record({Fn<uint32_t(__thiscall*)(void*)>(original, 0x0047ACF0)(volume) & 0xFF});
                    Fn<void(__thiscall*)(void*)>(original, 0x0047AD60)(volume);
                    Record({volume[0], volume[4], volume[5]});
                    break;
                case 3:
                    Record({static_cast<uint32_t>(
                        Fn<int(__thiscall*)(void*, uint32_t)>(original, 0x0047AD80)(m, id))});
                    break;
                case 4:
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x0047AE20)(
                        m, random.Below(4) == 0 ? 0 : id, 0xC5000001, 0x0B0B0B0B);
                    break;
                case 5:
                    Fn<void(__thiscall*)(void*, int)>(original, 0x0047AE90)(m, random.Below(3));
                    Fn<void(__thiscall*)(void*, int)>(original, 0x0047AEA0)(m, random.Below(3));
                    Fn<void(__thiscall*)(void*)>(original, 0x0047AF10)(m);
                    break;
                case 6: {
                    std::memset(props, 0xCD, sizeof(props));
                    uint32_t params[4] = {Bits(80.0f), Bits(90.0f), random.Below(2) ? 0 : Bits(55.0f), 3};
                    uint32_t entry = 0x006F4268 + 0x1C * random.Below(256);
                    Fn<void(__thiscall*)(void*, void*, uint32_t, void*)>(original, 0x0047AF40)(
                        m, props, entry, random.Below(3) ? params : nullptr);
                    for (int i = 0; i < 0x2C; i += 4)
                        Record({*reinterpret_cast<uint32_t*>(props + i)});
                    break;
                }
                case 7:
                    Record({Bits(Fn<float(__cdecl*)()>(original, 0x0047AFD0)()),
                            static_cast<uint32_t>(Fn<int(__cdecl*)()>(original, 0x0047AFE0)()),
                            Fn<uint32_t(__cdecl*)()>(original, 0x0047B010)() & 0xFF});
                    break;
                case 8:
                    RandomVolume(random, volume);
                    Record({reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x0047B030)(volume)) ==
                            reinterpret_cast<uint32_t>(volume)});
                    Record({volume[0], volume[5]});
                    Fn<void*(__thiscall*)(void*, int)>(original, 0x0047B070)(volume, random.Below(4));
                    RandomVolume(random, volume);
                    Record({Bits(Fn<float(__thiscall*)(void*)>(original, 0x0047B080)(volume))});
                    break;
                case 9:
                    Record({reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x0047B160)(m)) ==
                            reinterpret_cast<uint32_t>(m)});
                    break;
                case 10:
                    Fn<void(__thiscall*)(void*)>(original, random.Below(2) ? 0x0047B1B0 : 0x0047B240)(m);
                    break;
                case 11:
                    Fn<void(__thiscall*)(void*, float)>(original, 0x0047B210)(m, 500.0f);
                    Fn<void(__thiscall*)(void*)>(original, 0x0047B2A0)(m);
                    break;
                case 12:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047B2C0)(m, id)});
                    break;
                case 13:
                    Record({Fn<uint32_t(__thiscall*)(void*)>(original, 0x0047B340)(m) & 0xFF});
                    break;
                case 14: {
                    static const char* const kPaths[] = {"sounds\\a.wav", "x/y/b", "c", "dir\\", "Skater\\land"};
                    static const float kValues[] = {0.0f, 100.0f, 250.0f};
                    Record({Fn<uint32_t(__thiscall*)(void*, const char*, uint32_t, int, int, float, float, float)>(
                                original, 0x0047B360)(m, random.Pick(kPaths), random.Next() & 0xFFFF,
                                                      static_cast<int>(random.Below(3)),
                                                      static_cast<int>(random.Below(3)), random.Pick(kValues),
                                                      random.Pick(kValues), random.Pick(kValues)) &
                            0xFF});
                    break;
                }
                case 15: {
                    RandomVolume(random, volume);
                    float pitch = 90.0f;
                    Fn<void(__thiscall*)(void*, void*, float*, uint32_t)>(original, 0x0047B480)(
                        m, volume, &pitch, 0x006F4268 + 0x1C * random.Below(256));
                    Record({volume[0], volume[1], volume[4], Bits(pitch)});
                    break;
                }
                case 16:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047B4E0)(m, id) & 0xFF});
                    break;
                case 17:
                    RandomVolume(random, volume);
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, void*, float)>(original, 0x0047B520)(
                                m, id, volume, 110.0f) &
                            0xFF});
                    break;
                case 18: {
                    static const float kVolumes[] = {100.0f, 0.05f, 0.11f, 0.0f, 50.0f};
                    Fn<void(__thiscall*)(void*, float)>(original, 0x0047B5D0)(m, random.Pick(kVolumes));
                    break;
                }
                case 19: {
                    static const float kPitches[] = {1.0f, 0.01f, 0.009f, 0.0f, 2.5f, -1.0f};
                    Fn<void(__thiscall*)(void*, float, int, int)>(original, 0x0047B650)(m, random.Pick(kPitches),
                                                                                       7, 8);
                    Fn<void(__thiscall*)(void*, int, int, int)>(original, 0x0047B6B0)(m, 1, 2, 3);
                    break;
                }
                case 20: {
                    int index = -7;
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, int*)>(original, 0x0047B6F0)(
                                m, random.Below(5) == 0 ? 0 : id, random.Below(2) ? &index : nullptr) &
                                0xFF,
                            static_cast<uint32_t>(index)});
                    break;
                }
                case 21:
                    Record({Bits(Fn<float(__thiscall*)(void*, uint32_t)>(original, 0x0047B760)(m, id))});
                    break;
                default:
                    RandomVolume(random, volume);
                    if (random.Below(4)) // (mostly a sound that's in the table)
                        id = reinterpret_cast<const uint32_t*>(0x006F7A68)[15 * random.Below(48)];
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, void*, float)>(
                                original, random.Below(2) ? 0x0047B7C0 : 0x0047B520)(m, id, volume, 95.0f) &
                            0xFF});
                    break;
                }
            }
            PushState();
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "sfx manager: tables, volumes, parameters");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestSfxManager, ObjsysTests::CleanUp);

} // namespace
