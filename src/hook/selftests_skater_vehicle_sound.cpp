// Self-tests for the skater's vehicle sound component (game/skater/vehicle_sound.cpp): the originals against ours over
// random components, every call they make recorded by stand-ins (hook/standins.h). sVolume's own methods run for
// real (they're pure); the sound manager's and the script structures' are stood in for.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skater/vehicle_sound.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CSkaterVehicleSoundComponent;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    float Range(float low, float high) { return low + (high - low) * static_cast<float>(Below(10001)) / 10000.0f; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

alignas(16) uint8_t g_component[0xC8 + 0x40];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_core[0x1600];
alignas(16) uint8_t g_flags[0x40];
alignas(16) uint8_t g_input[0x200];
alignas(16) uint8_t g_soundComponent[0x20];
alignas(16) uint8_t g_sfx[0x400];          // the sound manager (+0x04 its default volume type)
alignas(16) uint8_t g_structs[8][0x10];    // stand-in structures: 0 all settings, 1 the rates, 2 settings, 3 extra...
alignas(16) uint8_t g_gearStructs[8][0x10];
alignas(16) uint8_t g_array[0xC];
uint32_t g_gearList[8];
alignas(16) uint8_t g_parkEditor[0x80];
alignas(16) uint8_t g_block[2][0x100];
int g_blockNext;

// The script values for this case.
float g_values[64];
bool g_checksumFound;
int g_clock;
int g_clockStep;
float g_channels[5];
bool g_silentVolume;
uint32_t g_nextSoundId;

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, size_t each, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        if (p >= b && p < b + size)
            return tag + static_cast<uint32_t>((p - b) / each) * 0x100 + static_cast<uint32_t>((p - b) % each);
        return 0;
    };
    if (!p)
        return 0;
    for (auto r : {in(g_structs, sizeof(g_structs), sizeof(g_structs[0]), 0x51000000),
                   in(g_gearStructs, sizeof(g_gearStructs), sizeof(g_gearStructs[0]), 0x52000000),
                   in(g_component, sizeof(g_component), sizeof(g_component), 0x53000000),
                   in(g_object, sizeof(g_object), sizeof(g_object), 0x54000000),
                   in(g_sfx, sizeof(g_sfx), sizeof(g_sfx), 0x55000000),
                   in(g_array, sizeof(g_array), sizeof(g_array), 0x56000000),
                   in(g_core, sizeof(g_core), sizeof(g_core), 0x57000000),
                   in(g_input, sizeof(g_input), sizeof(g_input), 0x58000000)})
        if (r)
            return r;
    return 0xBAD00000;
}

// A float's value: by its name (the same name in any structure gives the same value; names are hashed into the
// table, and a structure's id varies it).
struct Override {
    uint32_t name;
    float value;
};
std::vector<Override> g_overrides; // (some names' values set for the edge cases)
float ValueOf(uint32_t structure, uint32_t name)
{
    for (const Override& o : g_overrides)
        if (o.name == name)
            return o.value;
    return g_values[(name * 2654435761u + Id(structure)) >> 26];
}

float __fastcall StandInDropoff(void* manager, void*, uint32_t checksum)
{
    Record({0xD50F, Id(reinterpret_cast<uint32_t>(manager)), checksum});
    return g_values[checksum & 63] * 40.0f;
}

void StandInAll(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x6E57, r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] == 0x3C2DA7F5 ? g_structs[0] : g_structs[1]);
    });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x6F10, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<float*>(r.args[1]) = ValueOf(r.ecx, r.args[0]);
             return 1u;
         });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [](Regs& r) {
             Record({0x6F57, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_structs[2 + (r.args[0] & 3)]);
             return 1u;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6FC5, Id(r.ecx), r.args[0], r.args[2]});
             if (g_checksumFound)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = r.args[0] * 3 + 0x11;
             return static_cast<uint32_t>(g_checksumFound);
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))}, 12,
         [](Regs& r) {
             Record({0x6FA7, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_array);
             return 1u;
         });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0x6E1E, Id(r.ecx), r.args[0]});
        return g_gearList[r.args[0] & 7];
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        g_clock += g_clockStep;
        Record({0xC10C, static_cast<uint32_t>(g_clock)});
        return static_cast<uint32_t>(g_clock);
    });
    for (uint32_t address : {0x0047B760u, AddressOf(&Sfx::CSfxManager::GetDropoffDistance)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInDropoff)));
    s.At({0x0047BE10, AddressOf(&Sfx::CSfxManager::GetVolume)}, 0x18, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[0]);
        const uint32_t* pos = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0x6E70, Id(r.ecx), Id(r.args[0]), Id(r.args[1]), pos[0], pos[1], pos[2], r.args[2], r.args[3], r.args[4],
                r.args[5], static_cast<uint32_t>(volume->m_type)});
        for (int i = 0; i < 5; ++i)
            volume->m_channels[i] = g_silentVolume ? 0.0f : g_channels[i];
        return 1u;
    });
    s.At({0x0047BD20, AddressOf(&Sfx::CSfxManager::PlaySound)}, 0x18, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[1]);
        Record({0x9147, Id(r.ecx), r.args[0], Id(r.args[1]), r.args[2], r.args[3], r.args[4], r.args[5]});
        for (float c : volume->m_channels)
            Record({Bits(c)});
        Record({static_cast<uint32_t>(volume->m_type)});
        return ++g_nextSoundId;
    });
    s.At({0x0047B7C0, AddressOf(&Sfx::CSfxManager::SetSoundParamsScaled)}, 12, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[1]);
        Record({0x5E75, Id(r.ecx), r.args[0], Id(r.args[1]), r.args[2]});
        for (float c : volume->m_channels)
            Record({Bits(c)});
        return 1u;
    });
    s.At({0x0047B4E0, AddressOf(&Sfx::CSfxManager::StopSound)}, 4, [](Regs& r) {
        Record({0x5709, Id(r.ecx), r.args[0]});
        return 1u;
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0x4E6, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
}

void Build(Random& r)
{
    for (size_t i = 0; i < sizeof(g_component); ++i)
        g_component[i] = static_cast<uint8_t>(r.Next());
    std::memset(g_object, 0, sizeof(g_object));
    std::memset(g_core, 0, sizeof(g_core));
    std::memset(g_flags, 0, sizeof(g_flags));
    std::memset(g_input, 0, sizeof(g_input));
    auto* c = reinterpret_cast<CSkaterVehicleSoundComponent*>(g_component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    c->m_corePhysics = reinterpret_cast<Obj::CBaseComponent*>(g_core);
    c->m_sound = reinterpret_cast<Obj::CBaseComponent*>(g_soundComponent);
    c->m_input = g_input;
    c->m_settings = reinterpret_cast<Script::CStruct*>(g_structs[2]);
    float pos[4] = {r.Range(-500.0f, 500.0f), r.Range(-50.0f, 50.0f), r.Range(-500.0f, 500.0f), 1.0f};
    std::memcpy(g_object + 0x4C, pos, 16);
    float vel[4] = {r.Range(-1200.0f, 1200.0f), r.Range(-100.0f, 100.0f), r.Range(-1200.0f, 1200.0f), 0.0f};
    if (r.OneIn(6))
        vel[0] = vel[1] = vel[2] = 0.0f;
    std::memcpy(g_object + 0xAC, vel, 16);
    g_core[0x2ED] = !r.OneIn(5);
    At<uint8_t*>(g_core, 0x1528) = g_flags;
    At<int>(g_flags, 0x18) = r.OneIn(3) ? 0 : 1;
    At<int>(g_core, 0x380) = static_cast<int>(r.Below(4000)) - 1000;
    g_input[0x40] = r.OneIn(3);
    g_input[0x1C0] = r.Bit();
    c->m_silenced = r.OneIn(6);
    if (r.OneIn(8))
        c->m_settings = nullptr;
    c->m_gearCount = 1 + static_cast<int>(r.Below(6));
    float shift = 0.0f;
    for (int i = 0; i < 6; ++i) {
        shift += r.Range(5.0f, 25.0f);
        c->m_gears[i] = {shift, shift - r.Range(1.0f, 10.0f), r.Range(0.0f, 3000.0f), r.Range(3000.0f, 7000.0f)};
    }
    c->m_gear = static_cast<int>(r.Below(static_cast<uint32_t>(c->m_gearCount)));
    c->m_speed = r.Range(0.0f, 100.0f);
    c->m_rpm = r.Range(0.0f, 8000.0f);
    c->m_shiftTime = static_cast<int>(r.Below(5000));
    c->m_skid = r.Range(0.0f, 2000.0f);
    c->m_engineSound = r.OneIn(2) ? 0 : 0x500 + r.Below(5);
    c->m_skidSound = r.OneIn(2) ? 0 : 0x600 + r.Below(5);
    c->m_parkEditorLevel = r.OneIn(3);
    for (float& v : g_values)
        v = r.Range(-0.5f, 1.5f);
    for (int i = 0; i < 64; i += 3)
        g_values[i] = r.Range(0.0f, 20.0f);
    for (int i = 1; i < 64; i += 5)
        g_values[i] = r.Range(500.0f, 6000.0f);
    if (r.OneIn(5))
        for (float& v : g_values)
            v = static_cast<float>(r.Below(4)) * 0.5f; // round values: ties at the comparisons
    g_checksumFound = r.Bit();
    g_clock = static_cast<int>(r.Below(6000)) - 500;
    if (r.OneIn(8))
        g_clock = -2000000000;
    g_clockStep = static_cast<int>(r.Below(40));
    for (float& ch : g_channels)
        ch = r.OneIn(4) ? 0.0f : r.Range(0.0f, 100.0f);
    g_silentVolume = r.OneIn(4);
    At<int>(g_sfx, 0x04) = r.OneIn(4) ? 2 : (r.OneIn(6) ? 3 : 0);
    // The gears' array of structures.
    std::memset(g_array, 0, sizeof(g_array));
    reinterpret_cast<Script::CArray*>(g_array)->m_size = 1 + r.Below(6);
    for (int i = 0; i < 8; ++i)
        g_gearList[i] = reinterpret_cast<uint32_t>(g_gearStructs[i]);
    GameVariable<int>(0x00696D60) = static_cast<int>(r.Below(3)) * 1000;
    GameVariable<float>(0x00684FC8) = r.OneIn(4) ? 0.5f : 1.0f; // slow motion
    At<uint32_t>(g_parkEditor, 0x40) = r.OneIn(2) ? 0xD443A2BC : 0x1234;
    g_nextSoundId = 0x700;
    g_overrides.clear();
    if (r.OneIn(4)) {
        // Edge cases: a standing skater whose eased speed rounds to 0 from just below it, a gear shift point at
        // exactly that speed, the shift's recovery at exactly full, the engine's speed just over the top of its range.
        float* still = reinterpret_cast<float*>(g_object + 0xAC);
        still[0] = still[1] = still[2] = 0.0f;
        At<int>(g_flags, 0x18) = 1;
        auto* c2 = reinterpret_cast<CSkaterVehicleSoundComponent*>(g_component);
        c2->m_speed = r.Bit() ? -1e-30f : 0.0f;
        g_overrides.push_back({0x02A7095B, r.Bit() ? 1e-20f : 0.0f});
        g_overrides.push_back({0x77C8E7C8, 0.0f});
        g_overrides.push_back({0xCA5EFD91, 0.0f});
        if (c2->m_gear < c2->m_gearCount - 1)
            c2->m_gears[c2->m_gear].upAt = 0.0f;
        g_clockStep = 0;
        g_overrides.push_back({0xFB6BD20E, 100.0f});
        g_overrides.push_back({0xBFA32092, 0.25f});
        c2->m_shiftTime = g_clock + GameVariable<int>(0x00696D60) - 100 + static_cast<int>(r.Below(3)) - 1;
        g_overrides.push_back({0x3DD9C040, 0.0f});
        g_overrides.push_back({0x05AC84A3, 0.0f});
        g_overrides.push_back({0x95DF9449, c2->m_rpm / (r.Bit() ? 1.005f : 0.995f)});
    }
}

void Trace(std::vector<uint32_t>& trace)
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_component);
    for (size_t i = 0; i < 0xC8 / 4; ++i)
        trace.push_back(Id(w[i]) == 0xBAD00000 ? w[i] : Id(w[i]));
}

std::vector<Saved> SavedGlobals()
{
    return {{0x006F85A8, 4, {}}, {0x00696D60, 4, {}}, {0x00684FC8, 4, {}}, {0x007CE5D8, 4, {}}, {0x007D1800, 4, {}}};
}

void TestVehicleSound()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int played = 0;
    {
        auto* s = new StandIns;
        StandInAll(*s);
        s->At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
            Record({0xCA11, r.args[0]});
            return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
        });
        s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
            Record({0xF5EE, Id(r.args[0]) == 0xBAD00000 && r.args[0] == reinterpret_cast<uint32_t>(g_block[0])});
            return 0u;
        });
        s->At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
            Record({0xD7, At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
            return 0u;
        });
        GameVariable<void*>(0x006F85A8) = g_sfx;
        GameVariable<void*>(0x007CE5D8) = g_parkEditor;
        GameVariable<void*>(0x007D1800) = g_block[1];
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{1859};
            for (int n = 0; n < 6000; ++n) {
                Build(r);
                auto* c = reinterpret_cast<CSkaterVehicleSoundComponent*>(g_component);
                int which = static_cast<int>(r.Below(10));
                Record({0xCA5E, static_cast<uint32_t>(which)});
                size_t before = g_trace.size();
                switch (which) {
                case 0:
                case 1:
                case 2:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051B920)(c);
                    else
                        c->Update();
                    break;
                case 3:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051AE80)(c);
                    else
                        c->UpdateEngine();
                    break;
                case 4:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051B3E0)(c);
                    else
                        c->UpdateSkid();
                    break;
                case 5:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051B670)(c);
                    else
                        c->ReadSettings();
                    break;
                case 6: {
                    // Finalize: the object's components found (some missing).
                    uint8_t* list[3] = {g_core, g_soundComponent, g_input};
                    uint32_t types[3] = {0x5BD63B29, 0x07713C7B, 0x27D7CD28};
                    uint8_t* previous = nullptr;
                    for (int i = 0; i < 3; ++i) {
                        At<uint32_t>(list[i], 4) = r.OneIn(5) ? 0x12345 : types[i];
                        At<uint8_t*>(list[i], 0x10) = previous;
                        previous = list[i];
                    }
                    At<uint8_t*>(g_object, 0x100) = previous;
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051B8E0)(c);
                    else
                        c->Finalize();
                    break;
                }
                case 7: {
                    bool result;
                    int member = static_cast<int>(r.Below(3));
                    if (original) {
                        static const uint32_t members[] = {0x0051B7C0, 0x0051B820, 0x0051B860};
                        result = Original<bool(__thiscall*)(void*, void*, void*)>(members[member])(c, g_structs[5], nullptr);
                    } else if (member == 0) {
                        result = c->SetSkaterVehicleSounds(reinterpret_cast<Script::CStruct*>(g_structs[5]), nullptr);
                    } else if (member == 1) {
                        result = c->NoSkaterVehicleSounds(reinterpret_cast<Script::CStruct*>(g_structs[5]), nullptr);
                    } else {
                        result = c->StopSkaterVehicleSounds(reinterpret_cast<Script::CStruct*>(g_structs[5]), nullptr);
                    }
                    Record({0x3E5, result});
                    break;
                }
                case 8: {
                    // Create / Construct / Destroy / InitFromStructure.
                    std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                    g_blockNext = r.OneIn(4) ? -1 : 0;
                    void* made = original ? Original<void*(__cdecl*)()>(0x0051B880)() : CSkaterVehicleSoundComponent::Create();
                    Record({made == g_block[0]});
                    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_block[0]);
                    g_trace.insert(g_trace.end(), w, w + 0xC8 / 4);
                    auto* block = reinterpret_cast<CSkaterVehicleSoundComponent*>(g_block[0]);
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0051AE60)(block, nullptr);
                    else
                        block->InitFromStructure(nullptr);
                    Record({g_block[0][0x28]});
                    unsigned flags = r.Below(2);
                    void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x0051AE30)(block, flags)
                                            : block->Destroy(flags);
                    Record({result == g_block[0], At<uint32_t>(g_block[0], 0)});
                    break;
                }
                default: {
                    int result;
                    if (original) {
                        Original<void(__cdecl*)()>(0x0051AD90)();
                        Original<void(__thiscall*)(void*)>(0x0051B3B0)(c);
                        result = Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x0051ADC0)(c, 0x1234, nullptr, nullptr);
                    } else {
                        CSkaterVehicleSoundComponent::RegisterMemberFunctions();
                        c->UpdateUnused();
                        result = reinterpret_cast<Obj::ComponentWithoutMemberFunctions*>(c)->CallMemberFunction(0x1234, nullptr, nullptr);
                    }
                    Record({static_cast<uint32_t>(result)});
                    break;
                }
                }
                if (original)
                    for (size_t k = before; k < g_trace.size(); ++k)
                        played += g_trace[k] == 0x9147 || g_trace[k] == 0x5E75;
                Trace(g_trace);
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Log("  vehicle sound: %d sounds played or updated by the original", played);
    Check(played > 1000, "skater vehicle sound: the cases reach the sounds");
    Compare(traces[0], traces[1], "skater vehicle sound: engine (gears, rpm), skid, settings, member functions");
}

} // namespace

SELFTEST(Early, TestVehicleSound, RemoveStandIns);
