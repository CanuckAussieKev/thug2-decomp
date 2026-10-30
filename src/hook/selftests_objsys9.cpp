// Self-tests for the objsys agent's batch OS-9: the sound effects manager's positional sounds and the PC driver's
// voices (src/game/sfx/sfx_voices.cpp, 0x0047B8E0-0x0047D41E). Each runs the original and ours on the same state:
// - TestSfxPositional: the positional list, the playing sounds and wave tables with cameras, objects, the driver's
//   voices, the clock, script globals and the memory manager stood in for (the game's and ours).
// - TestSfxVoices: the voices over recording DirectSound objects (a device, sound buffers, 3D buffers, the
//   listener) with the cameras stood in for.
// - TestSfxWaveLoad: .snd files read from a scratch folder (the original's C runtime calls routed to ours).
// Every call, result and the whole state compared.

#include <windows.h>

#include <share.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/obj/sound_component.h"
#include "game/pcm/music_api.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/timer.h"
#include "game/gfx/viewport.h"
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
// A float's bits, every NaN as one (the x87 and SSE pick different NaNs from two operands).
uint32_t FB(float f)
{
    return std::isnan(f) ? 0x7FC00000u : Bits(f);
}
uint32_t FBAt(uint32_t address)
{
    return FB(*reinterpret_cast<const float*>(address));
}
// Pointers into the stack (the runs' locals) as one value.
uint32_t Rel(uint32_t word)
{
    return OnStack(word) ? 0x57AC0000u : word;
}

Lcg g_answers(1);

// --- cameras and objects --------------------------------------------------------------------------------------

// A camera: +0x4C where it was, +0x5C where it is (0x00485A00), +0x6C its matrix (0x00485A10).
alignas(16) uint8_t g_cameras[3][0xC0];
// An object: its position at +0x98.
alignas(16) uint8_t g_objects[4][0x100];

float RandomCoordinate(Lcg& random, int range)
{
    return static_cast<float>(static_cast<int>(random.Below(2 * range * 4 + 1)) - range * 4) / 4.0f;
}
void RandomVector(Lcg& random, float* v, int range)
{
    v[0] = RandomCoordinate(random, range);
    v[1] = RandomCoordinate(random, range);
    v[2] = RandomCoordinate(random, range);
    v[3] = random.Below(2) ? 1.0f : 0.0f;
}
void RandomCameras(Lcg& random)
{
    for (auto& camera : g_cameras) {
        std::memset(camera, 0, sizeof(camera));
        auto* at = reinterpret_cast<float*>(camera + 0x5C);
        RandomVector(random, at, 300);
        auto* was = reinterpret_cast<float*>(camera + 0x4C);
        for (int i = 0; i < 4; ++i)
            was[i] = at[i] + (random.Below(3) ? RandomCoordinate(random, 4) : RandomCoordinate(random, 200));
        auto* matrix = reinterpret_cast<float*>(camera + 0x6C);
        for (int row = 0; row < 4; ++row) {
            for (int i = 0; i < 4; ++i)
                matrix[4 * row + i] = static_cast<float>(static_cast<int>(random.Below(201)) - 100) / 100.0f;
        }
    }
}
void RandomObjects(Lcg& random)
{
    for (auto& object : g_objects) {
        std::memset(object, 0, sizeof(object));
        RandomVector(random, reinterpret_cast<float*>(object + 0x98), 600);
    }
}
uint32_t Camera(int i)
{
    return reinterpret_cast<uint32_t>(g_cameras[i]);
}
uint32_t Object(int i)
{
    return reinterpret_cast<uint32_t>(g_objects[i]);
}

void RecordVector(uint32_t tag, uint32_t address)
{
    Record({tag, FBAt(address), FBAt(address + 4), FBAt(address + 8)});
}

// 0x004A09E0 (cdecl: position, distance or null): the camera nearest a position (none sometimes).
void StandInCameras(StandIns& s, bool allowNone)
{
    s.At({0x004A09E0, AddressOf(Nx::GetClosestCamera)}, 0, [allowNone](Regs& r) {
        RecordVector(0xCA3E0000, r.args[0]);
        uint32_t pick = g_answers.Below(allowNone ? 4 : 3);
        if (r.args[1]) {
            static const float kDistances[] = {50.0f, 400.0f, 1019.0f, 1500.0f, 3000.0f, 299.75f, 790.0f};
            float distance = g_answers.Below(12) == 0 ? std::nanf("") : g_answers.Pick(kDistances);
            *reinterpret_cast<float*>(r.args[1]) = distance;
        }
        Record({0xCA3E0001, r.args[1] != 0, pick});
        return pick == 3 ? 0u : Camera(static_cast<int>(pick));
    });
}

// 0x00423680 (cdecl, the result in ST0): how far an object's sound reaches past where it's heard.
double __cdecl ExtentStandIn(const float* extent, const float* from)
{
    Record({0xE7E00000, FB(extent[0]), FB(extent[1]), FB(extent[2]), FB(extent[3]), FB(from[0]), FB(from[1])});
    static const double kExtents[] = {0.0, 50.5, 700.0, -20.0, 2500.0};
    return g_answers.Pick(kExtents);
}

// 0x00406460 (cdecl, the result in ST0): the angle (degrees) between two vectors.
double __cdecl AngleStandIn(const float* a, const float* b)
{
    Record({0xA9C1E000, FB(a[0]), FB(a[1]), FB(a[2]), FB(b[0]), FB(b[1]), FB(b[2])});
    static const double kAngles[] = {0.0, 30.0, -90.5, 180.0, 12.25};
    return g_answers.Pick(kAngles);
}

// --- the sound state ------------------------------------------------------------------------------------------

const std::initializer_list<std::pair<uint32_t, uint32_t>> kSfxGlobals = {
    {0x006F3668, 0x6F8A58 - 0x6F3668}, // positional pool, wave tables, playing sounds, the counts and lists, voices
    {0x0067DA34, 0x10},                // default dropoff, main volume, pitch, (+0x0C the frequency switch)
    {0x0067DB08, 8},                   // the voices' volume and pitch
    {0x0067DB10, 0x40},                // the file name buffer
    {0x00684FA8, 8},                   // quiet misses, the sfx volume setting
    {0x00684FC8, 4},                   // the slow motion
    {0x006FC49C, 1}, {0x006FA7D4, 1},  // the pause flags
    {0x007D6A38, 4}, {0x007CEFA8, 4},  // the DirectSound device, the platform
    {0x007CC979, 1}, {0x007D1800, 4},  // the memory manager
};

uint32_t g_voicePlaying[2]; // bit per voice
alignas(16) uint8_t g_managerMemory[0x10];
alignas(16) uint8_t g_instance[8];

void PushState(uint32_t from, uint32_t to)
{
    for (uint32_t a = from; a < to; a += 4)
        g_trace.push_back(Relocate(*reinterpret_cast<uint32_t*>(a)));
}

void RandomPositionalState(Lcg& random)
{
    std::memset(reinterpret_cast<void*>(0x006F3668), 0, 0x6F85C0 - 0x6F3668);
    // the wave tables
    for (uint32_t table : {0x006F4268u, 0x006F5E68u}) {
        auto* e = reinterpret_cast<uint8_t*>(table);
        for (int i = 0; i < 256; ++i, e += 0x1C) {
            auto* w = reinterpret_cast<uint32_t*>(e);
            w[0] = 0xC5000000 + random.Below(40);
            e[4] = static_cast<uint8_t>(random.Next());
            e[5] = static_cast<uint8_t>(random.Next());
            e[6] = static_cast<uint8_t>(random.Below(3));
            e[7] = static_cast<uint8_t>(random.Below(3));
            static const float kPercent[] = {100.0f, 100.0f, 50.0f, 120.5f};
            w[2] = Bits(random.Pick(kPercent));
            w[3] = Bits(random.Pick(kPercent));
            static const float kDropoff[] = {0.0f, 0.0f, 300.0f, 25.5f, 900.0f};
            w[4] = Bits(random.Pick(kDropoff));
            w[5] = 0x0A0E0000 + i;
            e[0x18] = static_cast<uint8_t>(random.Below(2));
            e[0x19] = static_cast<uint8_t>(random.Below(2));
        }
    }
    static const int kTableCounts[] = {0, 10, 40, 256};
    GameVariable<int>(0x006F85B4) = random.Pick(kTableCounts);
    GameVariable<int>(0x006F85B8) = random.Pick(kTableCounts);
    // the playing sounds
    auto* playing = reinterpret_cast<uint8_t*>(0x006F7A68);
    for (int i = 0; i < 48; ++i, playing += 0x3C) {
        auto* p = reinterpret_cast<uint32_t*>(playing);
        p[0] = random.Below(4) == 0 ? 0 : 0x1D000000 + random.Below(24);
        p[1] = 0xC5000000 + random.Below(40);
        p[2] = 0x006F4268 + 0x1C * random.Below(256);
        p[3] = p[1];
        static const float kDropoff[] = {0.0f, 0.0f, 300.0f, 25.5f, 2000.0f, 1300.0f};
        p[4] = random.Below(12) == 0 ? 0x7FC00000 : Bits(random.Pick(kDropoff));
        static const float kPercent[] = {100.0f, 100.0f, 80.0f, 133.0f};
        p[5] = Bits(random.Pick(kPercent));
        p[6] = Bits(random.Pick(kPercent));
        p[7] = p[5];
        p[8] = p[6];
        p[9] = p[5];
        p[10] = p[6];
        p[11] = random.Next();
        p[12] = random.Next();
        static const uint32_t kFalloffs[] = {0, 1, 2, 3, 7};
        p[13] = random.Pick(kFalloffs);
        static const uint32_t kTimes[] = {0, 4000, 5000, 6000, 0xFFFFFFF0};
        p[14] = random.Pick(kTimes);
    }
    g_voicePlaying[0] = random.Next() | random.Next() << 24;
    g_voicePlaying[1] = random.Next();
    // the positional list: some pool entries, linked in a random order
    auto* pool = reinterpret_cast<uint32_t*>(0x006F3668);
    uint32_t count = random.Below(14);
    uint32_t previous = 0;
    for (uint32_t n = 0; n < count; ++n) {
        uint32_t index = random.Below(128);
        uint32_t* e = pool + 6 * index;
        if (e[2] & 1)
            continue;
        e[0] = random.Below(5) == 0 ? 0x2E000000 + random.Below(3) : 0x1D000000 + random.Below(24);
        e[1] = 0xC5000000 + random.Below(40);
        e[2] = 1;
        e[3] = Object(static_cast<int>(random.Below(4)));
        if (previous && random.Below(2) == 0) { // (the same object and name as the one before)
            e[1] = reinterpret_cast<uint32_t*>(previous)[1];
            e[3] = reinterpret_cast<uint32_t*>(previous)[3];
        }
        e[4] = 0;
        e[5] = previous;
        if (previous)
            reinterpret_cast<uint32_t*>(previous)[4] = reinterpret_cast<uint32_t>(e);
        else
            GameVariable<uint32_t>(0x006F85B0) = reinterpret_cast<uint32_t>(e);
        previous = reinterpret_cast<uint32_t>(e);
    }
    GameVariable<int>(0x006F85BC) = static_cast<int>(count) + static_cast<int>(random.Below(3));
    // the manager: none, or one with any default volume type
    std::memset(g_instance, 0, sizeof(g_instance));
    g_instance[0] = static_cast<uint8_t>(random.Below(4) == 0);
    g_instance[4] = static_cast<uint8_t>(random.Below(4));
    GameVariable<uint32_t>(0x006F85A8) = random.Below(4) ? reinterpret_cast<uint32_t>(g_instance) : 0;
    static const uint32_t kReferences[] = {0, 1, 1, 2, 5};
    GameVariable<uint32_t>(0x006F85AC) = random.Pick(kReferences);
    // settings
    GameVariable<float>(0x0067DA34) = random.Below(2) ? 1020.0f : 500.0f;
    GameVariable<uint8_t>(0x00684FA8) = static_cast<uint8_t>(random.Below(2));
    GameVariable<uint8_t>(0x006FC49C) = static_cast<uint8_t>(random.Below(8) == 0);
    GameVariable<uint8_t>(0x006FA7D4) = static_cast<uint8_t>(random.Below(8) == 0);
    static const uint32_t kPlatforms[] = {0, 1, 2, 6, 0, 0, 0, 0};
    GameVariable<uint32_t>(0x007CEFA8) = random.Pick(kPlatforms);
    GameVariable<uint32_t>(0x007D6A38) = random.Below(8) ? 0x0D5D0000 : 0;
    static const float kSlomo[] = {1.0f, 0.5f, 2.0f, 0.1f};
    GameVariable<float>(0x00684FC8) = random.Pick(kSlomo);
}

// A volume passed on: its levels and type; its place and range unless it's a local (which the caller may not
// have set: the original passes them on uninitialised).
void RecordVolume(uint32_t volume)
{
    for (uint32_t i = 0; i < (OnStack(volume) ? 0x18u : 0x2Cu); i += 4)
        Record({FBAt(volume + i)});
}

void StandInPositionalWorld(StandIns& s)
{
    StandInCameras(s, true);
    s.Bytes(0x00423680, Branch(0xE9, 0x00423680, reinterpret_cast<void*>(&ExtentStandIn)));
    const uint32_t ourExtent = AddressOf(&Mth::GetDistance); // (called directly by ours)
    s.Bytes(ourExtent, Branch(0xE9, ourExtent, reinterpret_cast<void*>(&ExtentStandIn)));
    // the object's sound parts (thiscall)
    s.At({0x00432F10, AddressOf(&Obj::CSoundComponent::GetSoundExtent)}, 8, [](Regs& r) {
        uint32_t has = g_answers.Below(3) == 0;
        if (has) {
            auto* out = reinterpret_cast<float*>(r.args[1]);
            for (int i = 0; i < 4; ++i)
                out[i] = RandomCoordinate(g_answers, 100);
        }
        Record({0x432F1000, r.ecx, r.args[0], has});
        return has;
    });
    const std::pair<uint32_t, uint32_t> positions[] = {
        {0x00432E50u, AddressOf(&Obj::CSoundComponent::GetSoundPosition)},
        {0x00432EB0u, AddressOf(&Obj::CSoundComponent::GetPreviousSoundPosition)}};
    for (auto [address, ours] : positions) {
        s.At({address, ours}, 8, [address](Regs& r) {
            auto* out = reinterpret_cast<float*>(r.args[0]);
            const auto* at = reinterpret_cast<const float*>(r.ecx + 0x98);
            for (int i = 0; i < 4; ++i)
                out[i] = at[i] + (g_answers.Below(2) ? 0.0f : RandomCoordinate(g_answers, 30));
            Record({address, r.ecx, r.args[1]});
            return r.args[0];
        });
    }
    s.At({0x00432D40, AddressOf(&Obj::CSoundComponent::KeepLoopingSound)}, 8, [](Regs& r) {
        Record({0x432D4000, r.ecx, Rel(r.args[0]), r.args[1]});
        for (int i = 0; i < 0x2C; i += 4)
            Record({FBAt(r.args[0] + i)});
        return 0u;
    });
    s.At({0x00432F70, AddressOf(&Obj::CSoundComponent::UpdateProps)}, 4, [](Regs& r) {
        Record({0x432F7000, r.ecx, r.args[0]});
        return 0u;
    });
    // the clock
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        static const uint32_t kTimes[] = {1000, 4000, 5000, 9000, 0xFFFFFFF8};
        uint32_t time = g_answers.Pick(kTimes);
        Record({0x7130E000, time});
        return time;
    });
    // the driver
    s.At({0x0047C7E0, AddressOf(&Sfx::IsVoicePlaying)}, 0, [](Regs& r) {
        uint32_t voice = r.args[0];
        uint32_t playing = (g_voicePlaying[voice >> 5 & 1] >> (voice & 31)) & 1;
        Record({0x701CE000, voice, playing});
        return playing;
    });
    s.At({0x0047C7A0, AddressOf(&Sfx::StopVoice)}, 0, [](Regs& r) {
        Record({0x701CE001, r.args[0]});
        return 0u;
    });
    s.At({0x0047CD70, AddressOf(&Sfx::SetVoiceParameters)}, 0, [](Regs& r) {
        Record({0x701CE002, r.args[0], FB(FromBits(r.args[2]))});
        RecordVolume(r.args[1]);
        return 0u;
    });
    s.At({0x0047D130, AddressOf(&Sfx::PlayVoice)}, 0, [](Regs& r) {
        uint32_t voice = g_answers.Below(5) == 0 ? 0xFFFFFFFF : g_answers.Below(48);
        Record({0x701CE003, r.args[0], FB(FromBits(r.args[2])), voice});
        RecordVolume(r.args[1]);
        return voice;
    });
    s.At({0x0047CA50, AddressOf(&Sfx::InitVoices)}, 0, [](Regs& r) {
        Record({0x701CE004, r.args[0]});
        return 0u;
    });
    s.At({0x0047CC10, AddressOf(&Sfx::FreePermanentWaves)}, 0, [](Regs&) {
        Record({0x701CE005});
        return 0u;
    });
    // script globals (debug switches)
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        uint32_t value = g_answers.Below(2);
        Record({0x6E140000, r.args[0], value});
        return value;
    });
    // memory
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
         [](Regs& r) {
             uint32_t memory = g_answers.Below(4) == 0 ? 0 : reinterpret_cast<uint32_t>(g_managerMemory);
             if (memory)
                 std::memset(g_managerMemory, 0, sizeof(g_managerMemory));
             Record({0x3E3C0000, r.args[0], r.args[1], r.args[2], memory});
             return memory;
         });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0x3E3F0000, r.ecx, r.args[0]});
        return 0u;
    });
}

void TestSfxPositional()
{
    ObjsysTests::SaveGlobals(kSfxGlobals);
    StandIns& s = *new StandIns();
    StandInPositionalWorld(s);
    g_relocate = nullptr;
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<uint32_t>(0x007D1800) = 0x0E3E0000;
    alignas(16) static uint32_t volume[11];
    alignas(16) static float vectors[3][4];
    for (uint32_t seed = 1; seed <= 80; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x5F900000 + seed);
            g_answers = Lcg(0x5F9A0000 + seed);
            RandomPositionalState(random);
            RandomCameras(random);
            RandomObjects(random);
            alignas(4) static uint8_t manager[8];
            std::memcpy(manager, g_instance, sizeof(manager));
            void* m = manager;
            // every positional sound's props, by name and by unique id
            for (auto* e = GameVariable<const uint32_t*>(0x006F85B0); e; e = reinterpret_cast<const uint32_t*>(e[4])) {
                for (bool byChecksum : {false, true})
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, bool)>(original, 0x0047BA00)(
                        m, e[3], e[byChecksum ? 1 : 0], byChecksum)});
            }
            for (int step = 0; step < 25; ++step) {
                uint32_t id = random.Below(4) == 0 ? 0xC5000000 + random.Below(40) : 0x1D000000 + random.Below(26);
                uint32_t object = Object(static_cast<int>(random.Below(4)));
                auto randomVolume = [&] {
                    static const float kLevels[] = {0.0f, 0.0f, 100.0f, -100.0f, 50.0f, 33.25f, 7.0f, 120.0f};
                    bool silent = random.Below(3) == 0;
                    for (int i = 0; i < 5; ++i)
                        volume[i] = silent ? 0 : Bits(random.Pick(kLevels));
                    volume[5] = random.Below(4);
                    for (int i = 6; i < 11; ++i)
                        volume[i] = 0xEEEE0000 + i;
                };
                switch (random.Below(15)) {
                case 0: {
                    RandomVector(random, vectors[0], 300);
                    for (int i = 0; i < 4; ++i)
                        vectors[1][i] = vectors[0][i] + RandomCoordinate(random, random.Below(2) ? 3 : 100);
                    static const float kPitches[] = {100.0f, 80.0f, 0.0f};
                    static const float kFrames[] = {1.0f / 60.0f, 0.05f, 0.0f};
                    float pitch = random.Pick(kPitches);
                    uint32_t camera = random.Below(3) ? Camera(static_cast<int>(random.Below(3))) : 0;
                    Fn<void(__thiscall*)(void*, float*, const float*, const float*, float, uint32_t)>(original,
                                                                                                   0x0047B8E0)(
                        m, &pitch, vectors[0], vectors[1], random.Pick(kFrames), camera);
                    Record({FB(pitch)});
                    break;
                }
                case 1: {
                    bool byChecksum = random.Below(2) != 0;
                    uint32_t key = random.Below(2) ? id : 0xC5000000 + random.Below(40);
                    // (mostly an entry of the list: its object, and its name or unique id)
                    const auto* entry = GameVariable<const uint32_t*>(0x006F85B0);
                    for (uint32_t n = random.Below(14); entry && n > 0 && entry[4]; --n)
                        entry = reinterpret_cast<const uint32_t*>(entry[4]);
                    if (entry && random.Below(4)) {
                        object = entry[3];
                        key = entry[byChecksum ? 1 : 0];
                    }
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, bool)>(original, 0x0047BA00)(
                        m, object, key, byChecksum)});
                    break;
                }
                case 2:
                    if (random.Below(2))
                        Fn<void(__cdecl*)(bool)>(original, 0x0047BA60)(random.Below(3) != 0);
                    else
                        Fn<void(__cdecl*)()>(original, 0x0047C6C0)();
                    break;
                case 3:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047BAB0)(m, random.Below(6) ? id : 0) &
                                0xFF,
                            Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047BB20)(m, id)});
                    break;
                case 4:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0047BB50)(m, object);
                    break;
                case 5:
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x0047BC30)(
                        m, object, random.Below(3) ? 0xC5000000 + random.Below(40) : 0);
                    break;
                case 6: {
                    randomVolume();
                    uint32_t params[4] = {Bits(90.0f), Bits(70.0f), random.Below(2) ? 0 : Bits(55.0f),
                                          random.Below(4)};
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, void*, float, uint32_t, void*, int)>(
                        original, 0x0047BD20)(m, 0xC5000000 + random.Below(40), volume, random.Below(2) ? 100.0f : 65.0f,
                                              random.Below(2) ? 0 : 0x1D000000 + random.Below(26),
                                              random.Below(2) ? params : nullptr, 0)});
                    for (uint32_t v : volume)
                        Record({FB(FromBits(v))});
                    break;
                }
                case 7:
                case 8: {
                    randomVolume();
                    RandomVector(random, vectors[0], 900);
                    static const float kDropoffs[] = {0.0f, 300.0f, 1500.0f, 25.5f, 800.0f};
                    float dropoff = random.Pick(kDropoffs);
                    alignas(16) static float extent[4];
                    for (float& e : extent)
                        e = RandomCoordinate(random, 50);
                    uint32_t camera = random.Below(3) ? Camera(static_cast<int>(random.Below(3))) : 0;
                    if (camera && random.Below(4) == 0) // (at the camera: a level of exactly 100)
                        std::memcpy(vectors[0], reinterpret_cast<const void*>(camera + 0x5C), 16);
                    Record({Fn<uint32_t(__thiscall*)(void*, void*, const float*, float, int, uint32_t, void*)>(
                                original, 0x0047BE10)(m, volume, vectors[0], dropoff, static_cast<int>(random.Below(5)),
                                                      camera, random.Below(4) == 0 ? extent : nullptr) &
                            0xFF});
                    for (uint32_t v : volume)
                        Record({FB(FromBits(v))});
                    break;
                }
                case 9:
                case 10: {
                    static const float kDropoffs[] = {0.0f, 300.0f, 1500.0f};
                    uint32_t params[4] = {Bits(random.Below(2) ? 100.0f : 85.0f), Bits(random.Below(2) ? 100.0f : 60.0f),
                                          Bits(random.Pick(kDropoffs)), random.Below(5)};
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, void*, uint32_t, bool, bool)>(original,
                                                                                                    0x0047C140)(
                        m, 0xC5000000 + random.Below(40), params, object, random.Below(3) == 0, random.Below(4) == 0)});
                    break;
                }
                case 11:
                case 12: {
                    uint32_t sound = 0x006F7A68 + 0x3C * random.Below(48);
                    static const uint32_t kTimes[] = {0, 4000, 5000, 6000, 0xFFFFFFF0};
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x0047C360)(
                                m, object, sound, random.Pick(kTimes)) &
                            0xFF});
                    break;
                }
                default:
                    Fn<void(__thiscall*)(void*)>(original, 0x0047C560)(m);
                    break;
                }
            }
            PushState(0x006F3668, 0x006F85C0);
            PushState(0x0067DA34, 0x0067DA44);
            Record({GameVariable<uint32_t>(0x006F85A8) == reinterpret_cast<uint32_t>(g_managerMemory)});
            PushState(reinterpret_cast<uint32_t>(g_managerMemory), reinterpret_cast<uint32_t>(g_managerMemory) + 8);
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "sfx: positional sounds");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestSfxPositional, ObjsysTests::CleanUp);

// --- the voices -----------------------------------------------------------------------------------------------

// Argument counts (after `this`) of the DirectSound interfaces' methods, in vtable order.
const uint8_t kDsArgs[11] = {2, 0, 0, 3, 1, 2, 2, 0, 1, 1, 1};
const uint8_t kDsBufferArgs[21] = {2, 0, 0, 1, 2, 3, 1, 1, 1, 1, 2, 7, 3, 1, 1, 1, 1, 1, 0, 4, 0};
const uint8_t kDs3DBufferArgs[21] = {2, 0, 0, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 2, 2, 2, 2, 4, 4};
const uint8_t kListenerArgs[18] = {2, 0, 0, 1, 1, 1, 2, 1, 1, 1, 2, 2, 2, 7, 4, 2, 4, 0};

constexpr uint32_t kDeviceTag = 0xD5000000;
constexpr uint32_t kListenerTag = 0x15700000;
constexpr int kBufferCount = 8;
constexpr int k3DBufferCount = 6;
constexpr uint32_t kFailed = 0x80004005;

FakeCom* g_buffers[kBufferCount];
FakeCom* g_buffers3D[k3DBufferCount];
FakeCom* g_listener;
FakeCom* g_device;
int g_nextBuffer;
int g_next3D;

// The waves (PCWave) and their samples; slots like a wave table entry's (the wave, its looping flag); what Lock
// hands out.
alignas(16) uint8_t g_waves[4][0x44];
alignas(16) uint8_t g_waveData[4][0x400];
alignas(16) uint32_t g_slots[5][2];
alignas(16) uint8_t g_lock[2][0x400];
alignas(16) uint32_t g_permanentWaves[4][0x44 / 4];

void MakeSoundObjects(FakeComSet& com)
{
    g_device = com.Make(kDeviceTag, kDsArgs, 11);
    com.On(kDeviceTag, 3, [](Regs& r) { // CreateSoundBuffer
        const auto* desc = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0xC3EA0000, desc[0], desc[1], desc[2], desc[3], Rel(desc[4]), desc[5], desc[6], desc[7], desc[8]});
        if (desc[1] != 0x11 && g_answers.Below(7) == 0) // (the primary buffer never fails: unchecked)
            return kFailed;
        *reinterpret_cast<FakeCom**>(r.args[2]) = g_buffers[g_nextBuffer++ % kBufferCount];
        return 0u;
    });
    com.On(kDeviceTag, 4, [](Regs& r) { // GetCaps
        auto* caps = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0xCA950000, caps[0], caps[1], caps[0x20 / 4]});
        // (not over 0x80000003: a negative voice count would make the voices index past their array)
        static const uint32_t kMixing[] = {0, 8, 16, 17, 30, 60, 0x7FFFFFFF};
        static const uint32_t k3D[] = {0, 1, 5};
        caps[0x20 / 4] = g_answers.Pick(kMixing);
        caps[0x38 / 4] = g_answers.Pick(k3D);
        static const uint32_t kResults[] = {0, 0, 0, kFailed};
        return g_answers.Pick(kResults);
    });
    for (int i = 0; i < kBufferCount; ++i) {
        uint32_t tag = 0xB0000000 + (static_cast<uint32_t>(i) << 8);
        g_buffers[i] = com.Make(tag, kDsBufferArgs, 21);
        com.On(tag, 0, [](Regs& r) { // QueryInterface
            bool listener =
                std::memcmp(reinterpret_cast<const void*>(r.args[1]), reinterpret_cast<const void*>(0x00666B44), 16) == 0;
            Record({0x91D00000, *reinterpret_cast<const uint32_t*>(r.args[1]), listener});
            if (!listener && g_answers.Below(8) == 0) { // (the listener never fails: unchecked)
                *reinterpret_cast<void**>(r.args[2]) = nullptr;
                return 0x80004002u;
            }
            *reinterpret_cast<FakeCom**>(r.args[2]) = listener ? g_listener : g_buffers3D[g_next3D++ % k3DBufferCount];
            return 0u;
        });
        com.On(tag, 9, [](Regs& r) { // GetStatus
            static const uint32_t kStatus[] = {0, 1, 5, 4};
            *reinterpret_cast<uint32_t*>(r.args[1]) = g_answers.Pick(kStatus);
            static const uint32_t kResults[] = {0, 0, 0, kFailed};
            return g_answers.Pick(kResults);
        });
        com.On(tag, 11, [](Regs& r) { // Lock
            uint32_t bytes = r.args[2];
            uint32_t first = g_answers.Below(2) ? bytes : bytes / 2;
            *reinterpret_cast<void**>(r.args[3]) = g_lock[0];
            *reinterpret_cast<uint32_t*>(r.args[4]) = first;
            bool second = first < bytes && g_answers.Below(3) != 0;
            *reinterpret_cast<void**>(r.args[5]) = second ? g_lock[1] : nullptr;
            *reinterpret_cast<uint32_t*>(r.args[6]) = second ? bytes - first : 0;
            static const uint32_t kResults[] = {0, 0, 0, kFailed};
            return g_answers.Pick(kResults);
        });
        com.On(tag, 12, [](Regs&) { // Play
            static const uint32_t kResults[] = {0, 0, kFailed};
            return g_answers.Pick(kResults);
        });
    }
    for (int i = 0; i < k3DBufferCount; ++i)
        g_buffers3D[i] = com.Make(0xB3000000 + (static_cast<uint32_t>(i) << 8), kDs3DBufferArgs, 21);
    g_listener = com.Make(kListenerTag, kListenerArgs, 18);
}

void RandomVoiceState(Lcg& random)
{
    auto* voices = reinterpret_cast<uint32_t*>(0x006F85C8);
    for (int i = 0; i < 48; ++i) {
        uint32_t* v = voices + 6 * i;
        v[0] = random.Below(4) ? reinterpret_cast<uint32_t>(g_buffers[random.Below(kBufferCount)]) : 0;
        v[1] = random.Below(2) ? reinterpret_cast<uint32_t>(g_buffers3D[random.Below(k3DBufferCount)]) : 0;
        static const uint32_t kBase[] = {22050, 44100, 0xFFFFFFFB, 0, 11025};
        v[2] = random.Pick(kBase);
        static const uint32_t kFrequencies[] = {0, 22050, 44100, 100};
        v[3] = random.Pick(kFrequencies);
        v[4] = random.Below(2);
        // (a wave only with a buffer: the original would play a voice holding its wave without one)
        v[5] = random.Below(5) == 0 || !v[0] ? 0 : reinterpret_cast<uint32_t>(g_waves[random.Below(4)]);
    }
    GameVariable<int>(0x006F8A48) = static_cast<int>(random.Below(10));
    GameVariable<uint32_t>(0x006F8A4C) = random.Below(2);
    static const int kCounts[] = {48, 20, 1, 5};
    int count = random.Pick(kCounts);
    GameVariable<int>(0x006F8A50) = count;
    GameVariable<int>(0x006F8A54) = static_cast<int>(random.Below(static_cast<uint32_t>(count)));
    // (3D hardware always has its listener)
    GameVariable<uint32_t>(0x006F85C0) =
        GameVariable<uint32_t>(0x006F8A4C) || random.Below(2) ? reinterpret_cast<uint32_t>(g_listener) : 0;
    GameVariable<uint32_t>(0x007D6A38) = reinterpret_cast<uint32_t>(g_device);
    static const float kVolumes[] = {100.0f, 50.0f, 0.0f, 0.005f, -1.0f, 1000.0f, 73.5f};
    GameVariable<float>(0x0067DB08) = random.Pick(kVolumes);
    GameVariable<float>(0x0067DB0C) = random.Pick(kVolumes);
    GameVariable<uint8_t>(0x0067DA40) = static_cast<uint8_t>(random.Below(2));
    // the waves
    for (int i = 0; i < 4; ++i) {
        auto* w = reinterpret_cast<uint32_t*>(g_waves[i]);
        for (int k = 0; k < 0x44 / 4; ++k)
            w[k] = random.Next();
        static const uint32_t kRates[] = {22050, 44100, 0x80000001, 8000};
        w[1] = random.Pick(kRates);
        static const uint32_t kBytes[] = {0x400, 0x100, 0x33, 0};
        w[0x1C / 4] = random.Pick(kBytes);
        w[0x3C / 4] = reinterpret_cast<uint32_t>(g_waveData[i]);
        for (int k = 0; k < 0x400; ++k)
            g_waveData[i][k] = static_cast<uint8_t>(random.Next());
    }
    for (int i = 0; i < 5; ++i) {
        g_slots[i][0] = i < 4 ? reinterpret_cast<uint32_t>(g_waves[i]) : 0;
        g_slots[i][1] = random.Below(2) | random.Below(2) << 8;
    }
    std::memset(g_lock, 0, sizeof(g_lock));
    // the permanent waves
    std::memset(reinterpret_cast<void*>(0x006F4268), 0, 0x1C * 16);
    for (auto& wave : g_permanentWaves)
        wave[0x3C / 4] = random.Below(3) ? 0xDA7A0000 + random.Below(100) : 0;
    for (int i = 0; i < 16; ++i)
        GameVariable<uint32_t>(0x006F4268 + 0x1C * i + 0x14) =
            random.Below(4) ? reinterpret_cast<uint32_t>(g_permanentWaves[random.Below(4)]) : 0;
    static const int kPermanent[] = {0, 3, 16};
    GameVariable<int>(0x006F85B4) = random.Pick(kPermanent);
}

void StandInVoiceWorld(StandIns& s)
{
    StandInCameras(s, false);
    s.At({0x004A0950, AddressOf(Nx::GetCamera)}, 0, [](Regs& r) {
        uint32_t pick = g_answers.Below(4);
        Record({0x0EA09500, r.args[0], pick});
        return pick == 3 ? 0u : Camera(static_cast<int>(pick));
    });
    s.Bytes(0x00406460, Branch(0xE9, 0x00406460, reinterpret_cast<void*>(&AngleStandIn)));
    const uint32_t ourAngle = AddressOf(&Mth::GetAngle); // (called directly by ours)
    s.Bytes(ourAngle, Branch(0xE9, ourAngle, reinterpret_cast<void*>(&AngleStandIn)));
    s.At({0x00450650, AddressOf(&Pcm::StopMusic)}, 0, [](Regs&) {
        Record({0x9C350000});
        return 0u;
    });
    s.At({0x00450690, AddressOf(&Pcm::StopStreams)}, 0, [](Regs& r) {
        Record({0x9C350001, r.args[0]});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0x3E3F0000, r.ecx, r.args[0]});
        return 0u;
    });
}

void TestSfxVoices()
{
    ObjsysTests::SaveGlobals(kSfxGlobals);
    StandIns& s = *new StandIns();
    StandInVoiceWorld(s);
    static FakeComSet* com = nullptr;
    delete com;
    com = new FakeComSet(&s);
    MakeSoundObjects(*com);
    UseStandardRelocation(*com);
    GameVariable<uint32_t>(0x007D1800) = 0x0E3E0000;
    alignas(16) static uint32_t volume[11];
    alignas(4) static uint8_t manager[8];
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x5FA00000 + seed);
            g_answers = Lcg(0x5FAA0000 + seed);
            g_nextBuffer = 0;
            g_next3D = 0;
            RandomVoiceState(random);
            RandomCameras(random);
            for (int step = 0; step < 25; ++step) {
                int voice = static_cast<int>(random.Below(48));
                auto randomVolume = [&] {
                    static const float kLevels[] = {0.0f, 0.0f, 100.0f, -3.0f, 50.0f, 0.001f, 77.0f, 100.0f};
                    for (int i = 0; i < 5; ++i)
                        volume[i] = Bits(random.Pick(kLevels));
                    volume[5] = random.Below(4) ? 3 : random.Below(3);
                    RandomVector(random, reinterpret_cast<float*>(volume + 6), 500);
                    static const float kDropoffs[] = {1020.0f, 300.0f, 0.0f};
                    volume[10] = Bits(random.Pick(kDropoffs));
                };
                static const float kPitches[] = {0.0f, 100.0f, 50.0f, 150.0f, 1.0e9f, 0.0001f, 100.0f, 1000.0f, 0.5f};
                switch (random.Below(17)) {
                case 0: {
                    uint32_t wave =
                        random.Below(5) == 0 ? 0x0BAD0000 : reinterpret_cast<uint32_t>(g_waves[random.Below(4)]);
                    Record({static_cast<uint32_t>(Fn<int(__cdecl*)(uint32_t)>(original, 0x0047C720)(wave))});
                    break;
                }
                case 1:
                    Record({static_cast<uint32_t>(Fn<int(__cdecl*)()>(original, 0x0047C790)())});
                    Record({static_cast<uint32_t>(Fn<int(__cdecl*)(int)>(original, 0x0047C7E0)(voice))});
                    break;
                case 2:
                    Fn<void(__cdecl*)(int)>(original, 0x0047C7A0)(voice);
                    break;
                case 3:
                    Fn<void(__cdecl*)(float)>(original, 0x0047C7C0)(random.Pick(kPitches));
                    Fn<void(__cdecl*)(float)>(original, 0x0047C7D0)(random.Pick(kPitches));
                    break;
                case 4:
                    Fn<void(__cdecl*)()>(original, random.Below(2) ? 0x0047C810 : 0x0047B000)();
                    break;
                case 5:
                    std::memset(manager, 0xAB, sizeof(manager));
                    Fn<void(__cdecl*)(void*)>(original, 0x0047CA50)(random.Below(3) ? manager : nullptr);
                    PushState(reinterpret_cast<uint32_t>(manager), reinterpret_cast<uint32_t>(manager) + 8);
                    break;
                case 6:
                    Fn<void(__cdecl*)()>(original, 0x0047CBD0)();
                    break;
                case 7:
                    Fn<void(__cdecl*)()>(original, 0x0047CC10)();
                    break;
                case 8:
                case 9:
                case 10:
                    randomVolume();
                    Fn<void(__cdecl*)(int, void*, float)>(original, 0x0047CD70)(voice, volume, random.Pick(kPitches));
                    break;
                case 11:
                case 12:
                case 13:
                case 14:
                    randomVolume();
                    Record({static_cast<uint32_t>(Fn<int(__cdecl*)(void*, void*, float)>(original, 0x0047D130)(
                        g_slots[random.Below(5)], volume, random.Pick(kPitches)))});
                    break;
                default:
                    Fn<void(__cdecl*)()>(original, 0x0047D390)();
                    break;
                }
            }
            PushState(0x006F85C0, 0x006F8A58);
            PushState(0x0067DB08, 0x0067DB10);
            PushState(reinterpret_cast<uint32_t>(g_waves), reinterpret_cast<uint32_t>(g_waves) + sizeof(g_waves));
            PushState(0x006F4268, 0x006F4268 + 0x1C * 16);
            Record({GameVariable<uint32_t>(0x006F85B4), HashBytes(g_lock[0], sizeof(g_lock))});
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "sfx: voices");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    g_relocate = nullptr;
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestSfxVoices, ObjsysTests::CleanUp);

// --- loading waves --------------------------------------------------------------------------------------------

// The original's C runtime (not started offline): its calls done by ours (not recorded: ours calls the C runtime
// itself).
FILE* __cdecl FopenStandIn(const char* name, const char* mode)
{
    return _fsopen(name, mode, _SH_DENYNO);
}
size_t __cdecl FreadStandIn(void* buffer, size_t size, size_t count, FILE* file)
{
    return std::fread(buffer, size, count, file);
}
int __cdecl FseekStandIn(FILE* file, long offset, int origin)
{
    return std::fseek(file, offset, origin);
}
int __cdecl FcloseStandIn(FILE* file)
{
    return std::fclose(file);
}
void* __cdecl MallocStandIn(size_t size)
{
    return std::malloc(size);
}
void __cdecl FreeStandIn(void* memory)
{
    std::free(memory);
}

// The memory manager: blocks from one arena (the same addresses in both runs).
alignas(16) uint8_t g_arena[0x8000];
uint32_t g_arenaUsed;

char g_oldDirectory[MAX_PATH];
char g_scratch[MAX_PATH];

void WaveLoadCleanUp()
{
    if (g_oldDirectory[0])
        SetCurrentDirectoryA(g_oldDirectory);
    g_oldDirectory[0] = 0;
    ObjsysTests::CleanUp();
}

void Put32(std::string& out, uint32_t value)
{
    out.append(reinterpret_cast<const char*>(&value), 4);
}
void Put16(std::string& out, uint16_t value)
{
    out.append(reinterpret_cast<const char*>(&value), 2);
}

// A .wav file: its format, samples, maybe a sampler chunk (with or without loops) and others, in a random order;
// sometimes cut short.
std::string RandomWav(Lcg& random)
{
    std::vector<std::string> chunks;
    static const uint32_t kPerSecond[] = {64, 128, 100, 256};
    uint32_t perSecond = random.Pick(kPerSecond);
    { // (always: without one, the sample buffer's size is whatever the new wave held)
        std::string fmt = "fmt ";
        static const uint32_t kSizes[] = {16, 18, 20};
        uint32_t size = random.Pick(kSizes);
        Put32(fmt, size);
        Put16(fmt, static_cast<uint16_t>(random.Below(2) ? 0x11 : 1));
        Put16(fmt, static_cast<uint16_t>(1 + random.Below(2)));
        static const uint32_t kRates[] = {22050, 44100, 11025};
        Put32(fmt, random.Pick(kRates));
        Put32(fmt, perSecond);
        static const uint16_t kAligns[] = {0, 2, 4, 3, 36};
        Put16(fmt, random.Pick(kAligns));
        Put16(fmt, static_cast<uint16_t>(random.Below(2) ? 4 : 16));
        while (fmt.size() < 8 + size)
            fmt.push_back(static_cast<char>(random.Next()));
        chunks.push_back(fmt);
    }
    {
        std::string data = "data";
        uint32_t size = perSecond + (random.Below(2) ? 0 : 1 + random.Below(40));
        Put32(data, size);
        for (uint32_t i = 0; i < size; ++i)
            data.push_back(static_cast<char>(random.Next()));
        chunks.push_back(data);
    }
    if (random.Below(2)) {
        std::string smpl = "smpl";
        static const uint32_t kSizes[] = {0x24, 0x3C, 0x10, 0x25};
        uint32_t size = random.Pick(kSizes);
        Put32(smpl, size);
        for (uint32_t i = 0; i < size; i += 4)
            Put32(smpl, i == 0x1C ? random.Below(3) : random.Next());
        smpl.resize(8 + size);
        chunks.push_back(smpl);
    }
    if (random.Below(2)) {
        std::string list = "LIST";
        uint32_t size = random.Below(30);
        Put32(list, size);
        for (uint32_t i = 0; i < size; ++i)
            list.push_back(static_cast<char>(random.Next()));
        chunks.push_back(list);
    }
    for (size_t i = chunks.size(); i > 1; --i)
        std::swap(chunks[i - 1], chunks[random.Below(static_cast<uint32_t>(i))]);
    std::string file = "RIFF";
    Put32(file, 0);
    file += "WAVE";
    for (const std::string& chunk : chunks)
        file += chunk;
    if (random.Below(6) == 0 && chunks.back().compare(0, 4, "fmt ") != 0) // (cut short, but not the format)
        file.resize(file.size() - 1 - random.Below(10));
    return file;
}

void TestSfxWaveLoad()
{
    ObjsysTests::SaveGlobals(kSfxGlobals);
    StandIns& s = *new StandIns();
    const std::pair<uint32_t, void*> kRuntime[] = {
        {0x006241E8, reinterpret_cast<void*>(&FopenStandIn)},  {0x00623F3E, reinterpret_cast<void*>(&FreadStandIn)},
        {0x006240D2, reinterpret_cast<void*>(&FseekStandIn)},  {0x00623E04, reinterpret_cast<void*>(&FcloseStandIn)},
        {0x00624031, reinterpret_cast<void*>(&MallocStandIn)}, {0x0062411B, reinterpret_cast<void*>(&FreeStandIn)},
    };
    for (auto [address, to] : kRuntime)
        s.Bytes(address, Branch(0xE9, address, to));
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        uint32_t block = reinterpret_cast<uint32_t>(g_arena) + g_arenaUsed;
        g_arenaUsed += (r.args[0] + 15) & ~15u;
        Record({0x3E3A0000, r.args[0], r.args[1], r.args[2], block - reinterpret_cast<uint32_t>(g_arena)});
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0x3E3F0000, r.args[0] - reinterpret_cast<uint32_t>(g_arena)});
        return 0u;
    });
    StandInApi(s, 0x006450A4, reinterpret_cast<const void*>(&OutputDebugStringA), 4, [](Regs& r) {
        Record({0x0D0D0000, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<uint32_t>(0x007D1800) = 0x0E3E0000;

    // a scratch folder with data\sounds\ in it
    GetCurrentDirectoryA(MAX_PATH, g_oldDirectory);
    GetTempPathA(MAX_PATH, g_scratch);
    std::memcpy(g_scratch + std::strlen(g_scratch), "thug2_sfx_test", 15);
    CreateDirectoryA(g_scratch, nullptr);
    SetCurrentDirectoryA(g_scratch);
    CreateDirectoryA("data", nullptr);
    CreateDirectoryA("data\\sounds", nullptr);
    alignas(16) static uint8_t wave[0x48];
    alignas(16) static uint32_t slot[2];
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        Lcg files(0x5FB00000 + seed);
        for (int i = 0; i < 6; ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "data\\sounds\\w%d.snd", i);
            std::string bytes = RandomWav(files);
            FILE* file = _fsopen(name, "wb", _SH_DENYNO);
            if (file) {
                std::fwrite(bytes.data(), 1, bytes.size(), file);
                std::fclose(file);
            }
        }
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x5FBA0000 + seed);
            g_arenaUsed = 0;
            std::memset(g_arena, 0xA5, sizeof(g_arena));
            std::memset(reinterpret_cast<void*>(0x0067DB1C), 0x5A, 0x30);
            for (int step = 0; step < 6; ++step) {
                char path[16];
                int which = static_cast<int>(random.Below(7));
                std::snprintf(path, sizeof(path), which == 6 ? "missing" : "w%d", which);
                if (random.Below(3)) {
                    slot[0] = 0x51070000;
                    slot[1] = 0x51075151;
                    uint32_t loadType = random.Below(3);
                    Record({Fn<uint32_t(__cdecl*)(const char*, uint32_t, void*, int, int)>(original, 0x0047CC70)(
                                path, 0xC5000000 + step, slot, static_cast<int>(loadType), 7) &
                                0xFF,
                            slot[0] ? slot[0] - reinterpret_cast<uint32_t>(g_arena) : 0, slot[1]});
                } else {
                    std::memset(wave, 0xC3, sizeof(wave));
                    char name[64];
                    std::snprintf(name, sizeof(name), "data\\sounds\\%s.snd", path);
                    Record({Fn<uint32_t(__thiscall*)(void*, const char*, uint32_t)>(original, 0x0047C870)(
                                wave, name, random.Below(2) ? 0x400E0 : 0x400B0) &
                            0xFF});
                    for (int i = 0; i < 0x44; i += 4) {
                        uint32_t word = *reinterpret_cast<uint32_t*>(wave + i);
                        Record({word - reinterpret_cast<uint32_t>(wave) < 0x44 ? word - reinterpret_cast<uint32_t>(wave)
                                                                                : word});
                    }
                    uint32_t data = *reinterpret_cast<uint32_t*>(wave + 0x3C);
                    if (data - reinterpret_cast<uint32_t>(g_arena) < sizeof(g_arena))
                        Record({HashBytes(reinterpret_cast<const uint8_t*>(data), *reinterpret_cast<uint32_t*>(wave + 0x38))});
                }
            }
            PushState(0x0067DB10, 0x0067DB50);
            for (uint32_t i = 0; i < sizeof(g_arena); i += 4) {
                uint32_t word = *reinterpret_cast<uint32_t*>(g_arena + i);
                Record({word - reinterpret_cast<uint32_t>(g_arena) < sizeof(g_arena) ? word - reinterpret_cast<uint32_t>(g_arena) : word});
            }
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "sfx: loading waves");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    for (int i = 0; i < 6; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "data\\sounds\\w%d.snd", i);
        DeleteFileA(name);
    }
    RemoveDirectoryA("data\\sounds");
    RemoveDirectoryA("data");
    SetCurrentDirectoryA(g_oldDirectory);
    g_oldDirectory[0] = 0;
    RemoveDirectoryA(g_scratch);
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestSfxWaveLoad, WaveLoadCleanUp);

} // namespace
