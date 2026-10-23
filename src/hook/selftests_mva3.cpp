// Self-tests for the music and streams front end (game/pcm/music_api): the original against ours on identical
// state, with the platform layer and everything else they call stood in for by recording code (standins.h) whose
// results come from a seeded sequence.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/stubs.h"
#include "game/file/async.h"
#include "game/pcm/music.h"
#include "game/obj/stream_component.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

struct Originals {
    std::vector<std::unique_ptr<OriginalCode>> guards;
    Originals(bool original, std::initializer_list<uint32_t> addresses)
    {
        if (original) {
            for (uint32_t address : addresses)
                guards.push_back(std::make_unique<OriginalCode>(address));
        }
    }
};

template <typename F>
void* Pick(bool original, uint32_t address, F ours)
{
    return original ? OriginalFunction(address) : reinterpret_cast<void*>(AddressOf(ours));
}

uint32_t g_rng;
uint32_t Next()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

alignas(16) uint8_t g_owners[3][0x40];
alignas(16) uint8_t g_object[0x40];
alignas(16) uint8_t g_volume[0x20];
alignas(16) uint8_t g_file[0x40];
uint32_t Rel(uint32_t value)
{
    auto in = [&](const void* base, size_t size) {
        auto b = reinterpret_cast<uint32_t>(base);
        return value >= b && value < b + size;
    };
    if (in(g_owners, sizeof(g_owners)))
        return 0xA0000000 + (value - reinterpret_cast<uint32_t>(g_owners));
    if (in(g_object, sizeof(g_object)))
        return 0xB0000000 + (value - reinterpret_cast<uint32_t>(g_object));
    if (in(g_volume, sizeof(g_volume)))
        return 0xC0000000 + (value - reinterpret_cast<uint32_t>(g_volume));
    if (in(g_file, sizeof(g_file)))
        return 0xD0000000 + (value - reinterpret_cast<uint32_t>(g_file));
    return value;
}

// The globals the code reads and writes (saved around the test, randomised for each case).
struct Global {
    uint32_t address;
    uint32_t size;
};
const Global kGlobals[] = {
    {0x0069BAA1, 1}, {0x0069BAAC, 4}, {0x0067D7F0, 4}, {0x0067D7E4, 8}, {0x0069BA98, 4},
    {0x0069B938, 0x15C}, {0x0069BAB0, 0x18}, {0x00697700, 0x40}, {0x0069BA9C, 4}, {0x00698098, 0x2610},
};
uint32_t StateHash()
{
    uint32_t h = 0;
    for (const Global& g : kGlobals)
        h = h * 31 + HashBytes(reinterpret_cast<uint8_t*>(g.address), g.size);
    h = h * 31 + HashBytes(reinterpret_cast<uint8_t*>(g_owners), sizeof(g_owners));
    for (int i = 0; i < 3; ++i)
        h = h * 31 + Rel(reinterpret_cast<uint32_t*>(0x00698060)[i]);
    h = h * 31 + HashBytes(reinterpret_cast<uint8_t*>(0x0069BAC8), 0x1008);
    return h;
}

void Randomise(uint32_t seed)
{
    g_rng = seed * 2654435761u + 99;
    std::memset(reinterpret_cast<void*>(0x0069BAC8), 0x3C, 0x1008);
    for (const Global& g : kGlobals) {
        auto* p = reinterpret_cast<uint8_t*>(g.address);
        for (uint32_t i = 0; i < g.size; ++i)
            p[i] = static_cast<uint8_t>(Next());
    }
    for (auto& owner : g_owners)
        for (uint8_t& b : owner)
            b = static_cast<uint8_t>(Next());
    // The stream fields with meaning: ids small (so owners match), queues of up to 6, flags.
    for (int i = 0; i < 3; ++i) {
        auto* s = reinterpret_cast<uint32_t*>(0x0069B938 + i * 0x74);
        s[0] = 0x100 + Next() % 4;       // name
        s[1] = 0x200 + Next() % 4;       // id
        uint32_t volumes[] = {0x42C80000, 0x42480000, 0x3F800000, 0x00000000, 0x80000000};
        s[2] = volumes[Next() % 5];
        s[4] = Next() % 4;                // channel
        s[6] = static_cast<uint32_t>(static_cast<int>(Next() % 5) - 2); // priority
        s[7] = static_cast<uint32_t>(static_cast<int>(Next() % 7) - 2); // queue position
        s[8] = static_cast<uint32_t>(static_cast<int>(Next() % 8) - 1); // queue count
        reinterpret_cast<uint8_t*>(s)[0x28] = Next() % 2;
        for (int e = 0; e < 8; ++e)
            s[13 + e * 2] = 0x300 + Next() % 3;
        auto* ids = reinterpret_cast<uint32_t*>(g_owners[i] + 0x18);
        for (int k = 0; k < 3; ++k)
            ids[k] = Next() % 3 ? 0x200 + Next() % 5 : 0;
        *reinterpret_cast<uint8_t**>(g_owners[i] + 0xC) = g_object;
    }
    for (int i = 0; i < 3; ++i)
        GameVariable<uint8_t*>(0x00698060 + 4 * i) = Next() % 4 ? g_owners[i] : nullptr;
    GameVariable<bool>(0x0069BAB0) = Next() % 2;
    GameVariable<bool>(0x0069BABC) = Next() % 2;
    GameVariable<uint32_t>(0x0069BAB8) = Next() % 5;
    GameVariable<uint32_t>(0x0069BAC0) = Next() % 5;
    GameVariable<int>(0x0069BA9C) = Next() % 3;
    for (int i = 0; i < 16; ++i)
        reinterpret_cast<uint32_t*>(0x00697700)[i] = Next() % 7;
    // The switches.
    static uint8_t soundSystem;
    GameVariable<void*>(0x007D6A38) = Next() % 5 ? &soundSystem : nullptr;
    GameVariable<int>(0x007CEFA8) = Next() % 5 ? 1 : 2;
    GameVariable<bool>(0x0067D7E0) = Next() % 4 != 0;
    GameVariable<bool>(0x0067D7E1) = Next() % 4 != 0;
    GameVariable<bool>(0x00684FA8) = Next() % 2;
}

// Each stand-in records its call and returns the next number of the sequence, shaped by `make`.
template <typename Make>
void Standin(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, int args, Make make)
{
    s.At(addresses, pop, [tag, args, make](Regs& r) {
        Record({tag});
        for (int i = 0; i < args; ++i)
            Record({Rel(r.args[i])});
        uint32_t value = Next();
        Record({value});
        return make(value, r);
    });
}

using Fn0 = uint32_t(__cdecl*)();
using Fn1 = uint32_t(__cdecl*)(uint32_t);
using Fn2 = uint32_t(__cdecl*)(uint32_t, uint32_t);
using Fn3 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using Fn4 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);

std::vector<uint32_t> RunMusicApi(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x004505F0, 0x00450620, 0x00450500, 0x00450520, 0x00450530, 0x00450650, 0x00450820,
                                0x00450EF0, 0x004509E0, 0x00450A50, 0x00450A80, 0x00450690, 0x004506D0, 0x00450780,
                                0x004507D0, 0x00450940, 0x00450980, 0x00450AE0, 0x00450BE0, 0x00450D60, 0x00450EB0,
                                0x00450540});
    const float volumes[] = {50.0f, -1.0f, 0.0f, -0.0f, 100.0f, 7.25f};
    auto bits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };
    auto result = [](uint32_t tag, uint32_t value, bool isBool) {
        Record({tag, isBool ? (value & 0xFF) : value, StateHash()});
    };
    for (uint32_t seed = 1; seed <= 2200; ++seed) {
        Randomise(seed);
        uint32_t a = 0x100 + Next() % 4 + (Next() % 2 ? 0x100 : 0); // a name or an id
        float volume = volumes[Next() % 6];
        // (PlayTrackByName's user soundtrack and RemoveStream's queue get extra cases: 22-25)
        int which = static_cast<int>(seed % 26);
        if (which >= 22)
            which = which < 24 ? 5 : 16 + (which & 1);
        switch (which) {
        case 0: result(0x05F0, reinterpret_cast<Fn0>(Pick(original, 0x004505F0, &Pcm::NoMusicPlease))(), true); break;
        case 1: result(0x0620, reinterpret_cast<Fn0>(Pick(original, 0x00450620, &Pcm::StreamsDisabled))(), true); break;
        case 2: result(0x0500, reinterpret_cast<Fn1>(Pick(original, 0x00450500, static_cast<bool(__cdecl*)(uint32_t)>(&Pcm::StreamExists)))(a), true); break;
        case 3:
            result(0x0520, reinterpret_cast<Fn0>(Pick(original, 0x00450520, &Pcm::GetCurrentTrack))(), false);
            reinterpret_cast<Fn1>(Pick(original, 0x00450530, &Pcm::SetCurrentTrack))(seed * 3);
            result(0x0530, 0, false);
            break;
        case 4: reinterpret_cast<Fn0>(Pick(original, 0x00450650, &Pcm::StopMusic))(); result(0x0650, 0, false); break;
        case 5:
            if (seed % 26 >= 22) { // the user soundtrack (sound and music on, not playlist 1), its order's end near
                GameVariable<bool>(0x0067D7E0) = true;
                GameVariable<int>(0x007CEFA8) = 1;
                static uint8_t soundSystem;
                GameVariable<void*>(0x007D6A38) = &soundSystem;
                GameVariable<bool>(0x0069BAB0) = true;
                GameVariable<int>(0x0069BA9C) = 0;
            }
            result(0x0820, reinterpret_cast<Fn2>(Pick(original, 0x00450820, &Pcm::PlayTrackByName))(0xC0FFEE, bits(volume)), true);
            break;
        case 6:
            reinterpret_cast<Fn2>(Pick(original, 0x00450EF0, &Pcm::PlayMusicStream))(a, bits(volume));
            result(0x0EF0, 0, false);
            break;
        case 7: result(0x09E0, reinterpret_cast<Fn1>(Pick(original, 0x004509E0, &Pcm::PreLoadMusicStream))(a), true); break;
        case 8: result(0x0A50, reinterpret_cast<Fn0>(Pick(original, 0x00450A50, &Pcm::PreLoadMusicStreamDone))(), true); break;
        case 9:
            result(0x0A80, reinterpret_cast<Fn1>(Pick(original, 0x00450A80, &Pcm::StartPreLoadedMusicStream))(bits(volume)), true);
            break;
        case 10:
            reinterpret_cast<Fn1>(Pick(original, 0x00450690, static_cast<void(__cdecl*)(int)>(&Pcm::StopStreams)))(Next() % 3 ? static_cast<uint32_t>(-1 - static_cast<int>(Next() % 2)) : Next() % 3);
            result(0x0690, 0, false);
            break;
        case 11: reinterpret_cast<Fn1>(Pick(original, 0x004506D0, static_cast<void(__cdecl*)(uint32_t)>(&Pcm::StopStream)))(a); result(0x06D0, 0, false); break;
        case 12:
            result(0x0780, reinterpret_cast<Fn2>(Pick(original, 0x00450780, static_cast<bool(__cdecl*)(uint32_t, void*)>(&Pcm::SetStreamVolume)))(a, reinterpret_cast<uint32_t>(g_volume)), true);
            break;
        case 13:
            result(0x07D0, reinterpret_cast<Fn2>(Pick(original, 0x004507D0, &Pcm::SetStreamPitch))(a, bits(volume)), true);
            break;
        case 14: result(0x0940, reinterpret_cast<Fn1>(Pick(original, 0x00450940, &Pcm::PreLoadStreamDone))(a), true); break;
        case 15:
            result(0x0980, reinterpret_cast<Fn4>(Pick(original, 0x00450980, &Pcm::StartPreLoadedStream))(
                               a, reinterpret_cast<uint32_t>(g_volume), bits(volume), seed), true);
            break;
        case 16:
        case 17: {
            uint32_t owner = which == 16 ? 0 : reinterpret_cast<uint32_t>(g_owners[Next() % 3]);
            uint32_t entry = 0x300 + Next() % 3;
            if (seed % 26 >= 22) { // queues at their first position, headed by `entry`
                for (int i = 0; i < 3; ++i) {
                    auto* q = reinterpret_cast<uint32_t*>(0x0069B938 + i * 0x74);
                    q[7] = Next() % 4 ? 0 : static_cast<uint32_t>(-1);
                    q[13] = entry;
                }
            }
            result(0x0AE0, reinterpret_cast<Fn3>(Pick(original, 0x00450AE0, &Pcm::RemoveStream))(a, entry, owner), true);
            break;
        }
        case 18:
        case 19: {
            int slot = Next() % 3;
            GameVariable<uint8_t*>(0x00698060 + 4 * slot) = g_owners[slot]; // (never null: the caller checks)
            if (Next() % 4) // (mostly a stream's id, so the volume is worked out)
                reinterpret_cast<uint32_t*>(g_owners[slot] + 0x18)[slot] = reinterpret_cast<uint32_t*>(0x0069B93C + (Next() % 3) * 0x74)[0];
            result(0x0BE0, reinterpret_cast<Fn1>(Pick(original, 0x00450BE0, &Pcm::UpdateStreamVolume))(slot), true);
            break;
        }
        case 20:
            reinterpret_cast<Fn0>(Pick(original, 0x00450D60, &Pcm::Init))();
            result(0x0D60, 0, false);
            result(0x0EB0, reinterpret_cast<Fn0>(Pick(original, 0x00450EB0, &Pcm::UsingCD))(), true);
            break;
        case 21: {
            uint8_t* fam = reinterpret_cast<uint8_t*>(0x0069BAC8);
            using Load = uint32_t(__thiscall*)(void*, uint32_t);
            result(0x0540, reinterpret_cast<Load>(Pick(original, 0x00450540, &Pcm::FamData::Load))(fam, 0xABC00000 + seed), true);
            break;
        }
        }
    }
    return g_trace;
}

uint32_t g_fileSize;

void TestMusicApi()
{
    std::vector<Saved> saved = {{0x007D6A38, 4, {}}, {0x007CEFA8, 4, {}}, {0x0067D7E0, 2, {}}, {0x00698060, 12, {}},
                                {0x00684FA8, 1, {}}, {0x006F85A8, 4, {}}, {0x0069BAC8, 0x1008, {}}};
    for (const Global& g : kGlobals)
        saved.push_back({g.address, g.size, {}});
    SaveAll(saved);
    GameVariable<void*>(0x006F85A8) = reinterpret_cast<void*>(0x5F5F5F5F);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns();
        auto none = [](uint32_t, Regs&) { return 0u; };
        auto flag = [](uint32_t v, Regs&) { return (v & 1) | 0xAB00u; }; // (only AL counts)
        // (the music's stop: the original passes it 1 or 0, unused - not recorded: ours passes nothing)
        Standin(*s, {0x00453AA0, AddressOf(&Pcm::PlatformStopMusic)}, 0, 0x3AA0, 0, none);
        Standin(*s, {0x00453BD0, AddressOf(static_cast<void(__cdecl*)()>(&Pcm::StopStreams))}, 0, 0x3BD0, 0, none);
        Standin(*s, {0x00453B30, AddressOf(static_cast<void(__cdecl*)(int, int)>(&Pcm::StopStream))}, 0, 0x3B30, 2, none);
        Standin(*s, {0x00453400, AddressOf(&Pcm::PlatformSetMusicVolume)}, 0, 0x3400, 1, none);
        Standin(*s, {0x00453DF0, AddressOf(static_cast<bool(__cdecl*)(const Pcm::StreamVolume*, int)>(&Pcm::SetStreamVolume))}, 0, 0x3DF0, 0, [](uint32_t, Regs& r) {
            Record({HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 24), r.args[1]});
            return 0u;
        });
        Standin(*s, {0x00453270, AddressOf(&Pcm::StreamStatus)}, 0, 0x3270, 1, [](uint32_t v, Regs&) { return 1 + v % 2; });
        Standin(*s, {0x00454320, AddressOf(&Pcm::UserSoundtrackTracks)}, 0, 0x4320, 1, [](uint32_t v, Regs&) { return v % 5; });
        Standin(*s, {0x00453D70, AddressOf(&Pcm::LoadMusicTrackByName)}, 0, 0x3D70, 1, flag);
        Standin(*s, {0x00453850, AddressOf(&Pcm::PlayUserTrack)}, 0, 0x3850, 2, flag);
        Standin(*s, {0x00450310, AddressOf(&Pcm::ShuffleUserSoundtrack)}, 0, 0x0310, 0, none);
        Standin(*s, {0x00453DB0, AddressOf(&Pcm::PlayMusicTrack)}, 0, 0x3DB0, 1, flag);
        Standin(*s, {0x00453220, AddressOf(&Pcm::MusicPreloadDone)}, 0, 0x3220, 0, flag);
        Standin(*s, {0x00453BF0, AddressOf(&Pcm::StartPreloadedMusic)}, 0, 0x3BF0, 0, flag);
        Standin(*s, {0x004531D0, AddressOf(&Pcm::StreamPreloadDone)}, 0, 0x31D0, 1, flag);
        // (the original also passes the pitch, which the platform's start doesn't take: not recorded)
        Standin(*s, {0x00454270, AddressOf(&Pcm::StartPreloadedStream)}, 0, 0x4270, 2, flag);
        Standin(*s, {0x004533E0, AddressOf(static_cast<uint32_t(__cdecl*)(uint32_t, int)>(&Pcm::StreamExists))}, 0, 0x33E0, 2,
                [](uint32_t v, Regs&) { return v % 3 ? v : 0; });
        Standin(*s, {0x005B9760, AddressOf(&Stubs::ReturnTrue)}, 0, 0x9760, 0, flag);
        Standin(*s, {0x00450260, AddressOf(&Pcm::FindPlayingStream)}, 0, 0x0260, 1, // (the second argument, 1, is unused)
                [](uint32_t v, Regs&) { return static_cast<uint32_t>(static_cast<int>(v % 5) - 2); });
        Standin(*s, {0x0047B030, AddressOf(&Sfx::sVolume::Init)}, 0, 0xB030, 0, [](uint32_t v, Regs& r) {
            auto* p = reinterpret_cast<uint32_t*>(r.ecx);
            for (int i = 0; i < 6; ++i)
                p[i] = v + i;
            return r.ecx;
        });
        Standin(*s, {0x0047AD60, AddressOf(&Sfx::sVolume::SetSilent)}, 0, 0xAD60, 0, [](uint32_t, Regs& r) {
            std::memset(reinterpret_cast<void*>(r.ecx), 0, 20);
            return 0u;
        });
        Standin(*s, {0x0047AC50, AddressOf(&Sfx::sVolume::PercentageAdjustment)}, 4, 0xAC50, 1, [](uint32_t, Regs& r) {
            Record({HashBytes(reinterpret_cast<uint8_t*>(r.ecx), 24)});
            return 0u;
        });
        Standin(*s, {0x004A09E0}, 0, 0x09E0, 2,
                [](uint32_t v, Regs&) { return v % 3 ? reinterpret_cast<uint32_t>(g_object) : 0u; });
        Standin(*s, {0x00433D30, AddressOf(&Obj::CStreamComponent::GetStreamExtent)}, 8, 0x3D30, 1, [](uint32_t v, Regs& r) {
            Record({Rel(r.ecx)});
            std::memset(reinterpret_cast<void*>(r.args[1]), 0x11, 16);
            return v & 1;
        });
        Standin(*s, {0x00433CD0, AddressOf(&Obj::CStreamComponent::GetStreamPosition)}, 8, 0x3CD0, 0, [](uint32_t, Regs& r) {
            Record({Rel(r.ecx), Rel(r.args[1])});
            std::memset(reinterpret_cast<void*>(r.args[0]), 0x22, 16);
            return r.args[0];
        });
        Standin(*s, {0x0047BE10, AddressOf(&Sfx::CSfxManager::GetVolume)}, 24, 0xBE10, 0, [](uint32_t, Regs& r) {
            Record({r.ecx, r.args[2], r.args[3], Rel(r.args[4]), r.args[5] != 0, HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 24), HashBytes(reinterpret_cast<uint8_t*>(r.args[1]), 16)});
            if (r.args[5])
                Record({HashBytes(reinterpret_cast<uint8_t*>(r.args[5]), 16)});
            std::memset(reinterpret_cast<void*>(r.args[0]), 0x5E, 0x2C); // (the whole Sfx::sVolume, as the original)
            return 1u;
        });
        Standin(*s, {0x00453670, AddressOf(&Pcm::InitPlatform)}, 0, 0x3670, 0, none);
        Standin(*s, {0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, 0x8F60, 1,
                [](uint32_t v, Regs&) { return v % 3 ? v % 200 : 0; });
        // The file functions.
        s->At({0x006234FE}, 0, StandInSprintf); // (the game's C runtime isn't started offline)
        Standin(*s, {0x005B6C60, AddressOf(static_cast<File::AsyncFileHandle* (*)(const char*, bool, int)>(&File::AsyncOpen))},
                0, 0x6C60, 0, [](uint32_t v, Regs& r) {
                    Record({Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2]});
                    return v % 4 ? reinterpret_cast<uint32_t>(g_file) : 0u;
                });
        Standin(*s, {0x005B63A0, AddressOf(&File::AsyncFileHandle::GetFileSize)}, 0, 0x63A0, 0, [](uint32_t v, Regs& r) {
            Record({Rel(r.ecx)});
            const uint32_t sizes[] = {0, 1, 0xFFF, 0x1000, 0x1001, 0x80000000, 0x40};
            g_fileSize = sizes[v % 7];
            return g_fileSize;
        });
        Standin(*s, {0x005B6740, AddressOf(&File::AsyncFileHandle::Read)}, 12, 0x6740, 3, [](uint32_t v, Regs& r) {
            Record({Rel(r.ecx)});
            std::memset(reinterpret_cast<void*>(r.args[0]), static_cast<int>(v), r.args[2] < 0x1000 ? r.args[2] : 0);
            return r.args[2];
        });
        Standin(*s, {0x005B68A0, AddressOf(&File::AsyncClose)}, 0, 0x68A0, 1, flag);
        traces[0] = RunMusicApi(true);
        traces[1] = RunMusicApi(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "Pcm: the music and streams front end");
}
SELFTEST(Early, TestMusicApi, RemoveStandIns);

} // namespace
