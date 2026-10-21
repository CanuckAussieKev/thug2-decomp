// Self-tests for the VehicleSound component (game/obj/vehicle_sound.cpp): the originals against ours over random
// components and vehicles, every call they make recorded by stand-ins (hook/standins.h). sVolume's own methods run
// for real (they're pure); the sound manager's and the script structures' are stood in for.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/object.h"
#include "game/obj/vehicle_component.h"
#include "game/obj/vehicle_sound.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CVehicleComponent;
using Obj::CVehicleSoundComponent;

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

alignas(16) uint8_t g_component[0xBC + 0x44];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_vehicle[sizeof(CVehicleComponent)];
alignas(16) uint8_t g_wheels[4][sizeof(CVehicleComponent::Wheel)];
alignas(16) uint8_t g_skater[0x40];
alignas(16) uint8_t g_soundComponent[0x20];
alignas(16) uint8_t g_sfx[0x400];
alignas(16) uint8_t g_structs[8][0x10];
alignas(16) uint8_t g_gearStructs[8][0x10];
alignas(16) uint8_t g_array[0xC];
alignas(16) uint8_t g_event[0x10];
alignas(16) uint8_t g_pool[0x10];
uint32_t g_gearList[8];
alignas(16) uint8_t g_parkEditor[0x80];
alignas(16) uint8_t g_block[2][0x100];
int g_blockNext;

float g_values[64];
bool g_checksumFound;
int g_clock, g_clockStep;
double g_frame;
float g_channels[5];
bool g_silentVolume, g_eventFails;
uint32_t g_nextSoundId;

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, size_t each, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        if (p >= b && p < b + size)
            return tag + static_cast<uint32_t>((p - b) / each) * 0x1000 + static_cast<uint32_t>((p - b) % each);
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
                   in(g_vehicle, sizeof(g_vehicle), sizeof(g_vehicle), 0x57000000),
                   in(g_wheels, sizeof(g_wheels), sizeof(g_wheels[0]), 0x58000000),
                   in(g_skater, sizeof(g_skater), sizeof(g_skater), 0x59000000),
                   in(g_event, sizeof(g_event), sizeof(g_event), 0x5A000000),
                   in(g_pool, sizeof(g_pool), sizeof(g_pool), 0x5B000000),
                   in(g_soundComponent, sizeof(g_soundComponent), sizeof(g_soundComponent), 0x5C000000),
                   in(g_block, sizeof(g_block), sizeof(g_block[0]), 0x5D000000)})
        if (r)
            return r;
    return 0xBAD00000;
}

struct Override {
    uint32_t name;
    float value;
};
std::vector<Override> g_overrides;
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
double __cdecl StandInFrame()
{
    Record({0xF7A3});
    return g_frame;
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
    s.At({0x0044ECE0, AddressOf(Tmr::GameClockElapsed)}, 0, [](Regs&) {
        g_clock += g_clockStep;
        Record({0xC10C, static_cast<uint32_t>(g_clock)});
        return static_cast<uint32_t>(g_clock);
    });
    for (uint32_t address : {0x005BEAA0u, AddressOf(Tmr::GetSlomoFrameSeconds)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInFrame)));
    for (uint32_t address : {0x0047B760u, AddressOf(&Sfx::CSfxManager::GetDropoffDistance)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInDropoff)));
    s.At({0x0047BE10, AddressOf(&Sfx::CSfxManager::GetVolume)}, 0x18, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[0]);
        const uint32_t* pos = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0x6E70, Id(r.ecx), Id(r.args[0]) == 0xBAD00000 ? 1u : Id(r.args[0]), Id(r.args[1]), pos[0], pos[1],
                pos[2], r.args[2], r.args[3], r.args[4], r.args[5], static_cast<uint32_t>(volume->m_type)});
        for (int i = 0; i < 5; ++i)
            volume->m_channels[i] = g_silentVolume ? 0.0f : g_channels[i];
        return 1u;
    });
    s.At({0x0047BD20, AddressOf(&Sfx::CSfxManager::PlaySound)}, 0x18, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[1]);
        Record({0x9147, Id(r.ecx), r.args[0], r.args[2], r.args[3], r.args[4], r.args[5]});
        for (float c : volume->m_channels)
            Record({Bits(c)});
        Record({static_cast<uint32_t>(volume->m_type)});
        return ++g_nextSoundId;
    });
    s.At({0x0047B7C0, AddressOf(&Sfx::CSfxManager::SetSoundParamsScaled)}, 12, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[1]);
        Record({0x5E75, Id(r.ecx), r.args[0], r.args[2]});
        for (float c : volume->m_channels)
            Record({Bits(c)});
        return 1u;
    });
    s.At({0x0047B4E0, AddressOf(&Sfx::CSfxManager::StopSound)}, 4, [](Regs& r) {
        Record({0x5709, Id(r.ecx), r.args[0]});
        return 1u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0xADDC, Id(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8,
         [](Regs& r) {
             Record({0xADD5, Id(r.ecx), r.args[0], Id(r.args[1])});
             return 0u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0xADDF, Id(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00433240}, 4, [](Regs& r) {
        Record({0x3324, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA110, Id(r.ecx)});
        return g_eventFails ? 0u : reinterpret_cast<uint32_t>(g_event);
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1417, Id(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA, Id(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0xDE15, Id(r.args[0])});
        return 0u;
    });
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x5E1F, Id(r.ecx), r.args[0], Id(r.args[1])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Id(r.ecx), r.args[0]});
        if (r.args[0] == 0xE47F1B79)
            return reinterpret_cast<uint32_t>(g_vehicle);
        return r.args[0] == 0x07713C7B ? reinterpret_cast<uint32_t>(g_soundComponent) : 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA11, r.args[0]});
        return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF5EE, Id(r.args[0])});
        return 0u;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD7, Id(r.ecx), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
        return 0u;
    });
}

void Build(Random& r)
{
    for (uint8_t& b : g_component)
        b = static_cast<uint8_t>(r.Next());
    std::memset(g_object, 0, sizeof(g_object));
    std::memset(g_vehicle, 0, sizeof(g_vehicle));
    auto* c = reinterpret_cast<CVehicleSoundComponent*>(g_component);
    auto* v = reinterpret_cast<CVehicleComponent*>(g_vehicle);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    c->mp_vehicle = v;
    c->mp_sound = reinterpret_cast<Obj::CBaseComponent*>(g_soundComponent);
    c->m_settings = reinterpret_cast<Script::CStruct*>(g_structs[2]);
    c->m_collideSound = reinterpret_cast<Script::CStruct*>(g_structs[6]);
    float pos[4] = {r.Range(-500.0f, 500.0f), r.Range(-50.0f, 50.0f), r.Range(-500.0f, 500.0f), 1.0f};
    std::memcpy(g_object + 0x4C, pos, 16);
    v->m_wheels = reinterpret_cast<CVehicleComponent::Wheel*>(g_wheels);
    for (auto& raw : g_wheels) {
        for (uint8_t& b : raw)
            b = static_cast<uint8_t>(r.Next());
        auto& wheel = *reinterpret_cast<CVehicleComponent::Wheel*>(raw);
        wheel.drive = !r.OneIn(3);
        wheel.spin = r.Range(-80.0f, 80.0f);
        if (r.OneIn(6))
            wheel.spin = 0.0f;
        wheel.radius = r.Range(10.0f, 20.0f);
        wheel.state = static_cast<int>(r.Below(8));
        wheel.slip = r.Range(0.0f, 600.0f);
        wheel.maxStaticGripVel = r.Range(0.0f, 600.0f);
        if (r.OneIn(5))
            wheel.maxStaticGripVel = wheel.slip;
    }
    // Some wheels' slips equal (a tie for the fastest).
    if (r.OneIn(4))
        reinterpret_cast<CVehicleComponent::Wheel*>(g_wheels[1])->slip =
            reinterpret_cast<CVehicleComponent::Wheel*>(g_wheels[2])->slip;
    v->m_throttle = r.Bit();
    v->m_reverse = r.OneIn(3);
    v->m_brake = r.OneIn(3);
    v->m_handbrake = r.OneIn(3);
    v->m_wheelsInContact = r.OneIn(3) ? 0 : static_cast<int>(r.Below(5));
    v->m_airTime = r.Range(0.0f, 3.0f);
    v->m_collisionImpulse = r.OneIn(3) ? 0.0f : r.Range(0.0f, 4000.0f);
    v->m_soundsName = 0x1CA1FF20 + r.Below(3);
    v->mp_skater = reinterpret_cast<Obj::CCompositeObject*>(g_skater);
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
    c->m_collideTime = static_cast<int>(r.Below(5000));
    c->m_skid = r.Range(0.0f, 2000.0f);
    c->m_engineSound = r.OneIn(2) ? 0 : 0x500 + r.Below(5);
    c->m_skidSound = r.OneIn(2) ? 0 : 0x600 + r.Below(5);
    c->m_parkEditorLevel = r.OneIn(3);
    for (float& value : g_values)
        value = r.Range(-0.5f, 1.5f);
    for (int i = 0; i < 64; i += 3)
        g_values[i] = r.Range(0.0f, 20.0f);
    for (int i = 1; i < 64; i += 5)
        g_values[i] = r.Range(500.0f, 6000.0f);
    if (r.OneIn(5))
        for (float& value : g_values)
            value = static_cast<float>(r.Below(4)) * 0.5f; // round values: ties at the comparisons
    g_checksumFound = r.Bit();
    g_clock = static_cast<int>(r.Below(6000)) - 500;
    if (r.OneIn(8))
        g_clock = -2000000000;
    g_clockStep = static_cast<int>(r.Below(40));
    g_frame = r.OneIn(3) ? 1.0 / 60.0 : r.Range(0.0f, 0.05f);
    for (float& ch : g_channels)
        ch = r.OneIn(4) ? 0.0f : r.Range(0.0f, 100.0f);
    g_silentVolume = r.OneIn(4);
    g_eventFails = r.OneIn(8);
    At<int>(g_sfx, 0x04) = r.OneIn(4) ? 2 : (r.OneIn(6) ? 3 : 0);
    std::memset(g_array, 0, sizeof(g_array));
    reinterpret_cast<Script::CArray*>(g_array)->m_size = 1 + r.Below(6);
    for (int i = 0; i < 8; ++i)
        g_gearList[i] = reinterpret_cast<uint32_t>(g_gearStructs[i]);
    GameVariable<int>(0x00696D60) = static_cast<int>(r.Below(3)) * 1000;
    At<uint32_t>(g_parkEditor, 0x40) = r.OneIn(2) ? 0xD443A2BC : 0x1234;
    g_nextSoundId = 0x700;
    g_overrides.clear();
    if (r.OneIn(4)) {
        // Edge cases: a standing car whose eased speed rounds to 0 from just below it, a gear shift point at exactly
        // that speed, the shift's recovery at exactly full, the engine's speed just over the top of its range, the
        // crash exactly at the cutoff and the mute delay exactly over.
        for (auto& raw : g_wheels)
            reinterpret_cast<CVehicleComponent::Wheel*>(raw)->spin = 0.0f;
        v->m_wheelsInContact = r.Bit() ? 2 : 0;
        c->m_speed = r.Bit() ? -1e-30f : 0.0f;
        if (r.Bit()) // (a shift down at exactly that speed)
            c->m_gears[c->m_gear].downAt = 0.0f;
        g_overrides.push_back({0x02A7095B, r.Bit() ? 1e-20f : 0.0f});
        g_overrides.push_back({0x77C8E7C8, 0.0f});
        g_overrides.push_back({0xCA5EFD91, 0.0f});
        if (c->m_gear < c->m_gearCount - 1)
            c->m_gears[c->m_gear].upAt = 0.0f;
        g_clockStep = 0;
        g_overrides.push_back({0xFB6BD20E, 100.0f});
        g_overrides.push_back({0xBFA32092, 0.25f});
        c->m_shiftTime = g_clock + GameVariable<int>(0x00696D60) - 100 + static_cast<int>(r.Below(3)) - 1;
        g_overrides.push_back({0x3DD9C040, 0.0f});
        g_overrides.push_back({0x05AC84A3, 0.0f});
        g_overrides.push_back({0x95DF9449, c->m_rpm / (r.Bit() ? 1.005f : 0.995f)});
        g_overrides.push_back({0x71B4B9FE, 250.0f});
        c->m_collideTime = g_clock + GameVariable<int>(0x00696D60) - 250 + static_cast<int>(r.Below(3)) - 1;
        g_overrides.push_back({0xCEA59D0B, 1000.0f});
        v->m_collisionImpulse = r.Bit() ? 500.0f : 1500.0f;
        g_overrides.push_back({0xF6981837, r.Bit() ? 0.5f : 1.5f});
        g_overrides.push_back({0xC0F451D4, v->m_airTime});
    }
}

void Trace()
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_component);
    for (size_t i = 0; i < 0xBC / 4; ++i)
        g_trace.push_back(Id(w[i]) == 0xBAD00000 ? w[i] : Id(w[i]));
}

std::vector<Saved> SavedGlobals()
{
    return {{0x006F85A8, 4, {}}, {0x00696D60, 4, {}}, {0x007CE5D8, 4, {}}, {0x007D1800, 4, {}}, {0x006F3614, 16, {}},
            {0x007CC979, 1, {}}};
}

void TestVehicleSoundComponent()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int played = 0, crashes = 0;
    {
        auto* s = new StandIns;
        StandInAll(*s);
        GameVariable<void*>(0x006F85A8) = g_sfx;
        GameVariable<void*>(0x007CE5D8) = g_parkEditor;
        GameVariable<void*>(0x007D1800) = g_block[1];
        GameVariable<bool>(0x007CC979) = true;
        auto& structPools = GameVariable<Mem::PoolPair>(0x006F3614);
        structPools.current = 0;
        structPools.pools[0] = reinterpret_cast<Mem::Pool*>(g_pool);
        structPools.pools[1] = nullptr;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x3C690};
            for (int n = 0; n < 6000; ++n) {
                Build(r);
                auto* c = reinterpret_cast<CVehicleSoundComponent*>(g_component);
                int which = static_cast<int>(r.Below(12));
                Record({0xCA5E, static_cast<uint32_t>(which)});
                size_t before = g_trace.size();
                switch (which) {
                case 0:
                case 1:
                    if (r.OneIn(6))
                        c->m_settings = nullptr;
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043D290)(c);
                    else
                        c->Update();
                    break;
                case 2:
                case 3:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043C690)(c);
                    else
                        c->UpdateEngine();
                    break;
                case 4:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043CBC0)(c);
                    else
                        c->UpdateSkid();
                    break;
                case 5:
                case 6:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043D090)(c);
                    else
                        c->UpdateCollideSound();
                    break;
                case 7:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043CE70)(c);
                    else
                        c->ReadSettings();
                    break;
                case 8:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0043D050)(c);
                    else
                        c->Finalize();
                    break;
                case 9:
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0043C540)(c, g_structs[7]);
                    else
                        c->GetDebugInfo(reinterpret_cast<Script::CStruct*>(g_structs[7]));
                    break;
                case 10: {
                    // Create / Construct over dirty memory, InitFromStructure, the destructors.
                    std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                    g_blockNext = r.OneIn(4) ? -1 : 0;
                    void* made = original ? Original<void*(__cdecl*)()>(0x0043CFC0)() : CVehicleSoundComponent::Create();
                    Record({Id(reinterpret_cast<uint32_t>(made))});
                    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_block[0]);
                    g_trace.insert(g_trace.end(), w, w + 0xBC / 4);
                    auto* block = reinterpret_cast<CVehicleSoundComponent*>(g_block[0]);
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0043C670)(block, nullptr);
                    else
                        block->InitFromStructure(nullptr);
                    Record({g_block[0][0x1C]});
                    break;
                }
                default: {
                    unsigned flags = r.Below(4);
                    bool body = r.Bit();
                    void* result = c;
                    if (original) {
                        if (body)
                            Original<void(__thiscall*)(void*)>(0x0043C5F0)(c);
                        else
                            result = Original<void*(__thiscall*)(void*, unsigned)>(0x0043D020)(c, flags);
                    } else if (body) {
                        c->DestroyBody();
                    } else {
                        result = c->Destroy(flags);
                    }
                    Record({Id(reinterpret_cast<uint32_t>(result))});
                    break;
                }
                }
                if (original)
                    for (size_t k = before; k < g_trace.size(); ++k) {
                        played += g_trace[k] == 0x9147 || g_trace[k] == 0x5E75;
                        crashes += g_trace[k] == 0x3324;
                    }
                Trace();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Log("  vehicle sound component: %d sounds played or updated, %d crash sounds by the original", played, crashes);
    Check(played > 1000 && crashes > 100, "vehicle sound component: the cases reach the sounds");
    Compare(traces[0], traces[1], "vehicle sound component: engine, tyres, crash, settings, create / destroy");
}

} // namespace

SELFTEST(Early, TestVehicleSoundComponent, RemoveStandIns);
