// Self-tests for the skater's local network logic component's remaining functions and the graffiti steal handler
// (src/game/skater/local_net_logic.cpp) and the looping sound component (looping_sound.cpp, 0x00513860-0x00513FBE):
// the original and ours on the same stand-in skater, every call they make recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/net_dispatcher.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sfx/terrain.h"
#include "game/skater/local_net_logic.h"
#include "game/skater/looping_sound.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0200, kState = 0x0600, kCore = 0x0800, kControl = 0x0A00,
                   kSkaterA = 0x0C00, kSkaterB = 0x0E00, kStructs = 0x1000, kMessage = 0x1400, kContext = 0x1500,
                   kTags = 0x1600, kTable = 0x1700, kHeap = 0x4000;

struct Answers {
    uint32_t calloc, heap;
    bool multiplayer, localPlayer, haveA, haveB, terrainFound, haveCore, silentVolume;
    uint8_t runCount;
    uint32_t soundId;
    float dropoff, slomo, terrainVolume;
};
Answers g_a;
void* g_vtable[16];

uint32_t Allocate(uint32_t size)
{
    uint32_t at = g_a.heap;
    g_a.heap += (size + 15) & ~15u;
    std::memset(At(at), 0xCD, size);
    return Addr(at);
}

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    StandInStructGetters(s);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        switch (r.args[0]) {
        case 0x43686585: return Addr(kState);
        case 0x5BD63B29: return g_a.haveCore ? Addr(kCore) : 0u;
        case 0xD9151D4D: return Addr(kControl);
        default: return 0u;
        }
    });
    Rec(s, {0x0045D760, AddressOf(&Obj::CBaseComponent::Suspend)}, 1, 0xD760);
    Rec(s, {0x005651F0}, 0, 0x51F0, [](Regs&) { return g_a.multiplayer ? 1u : 0u; });
    Rec(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return g_a.localPlayer ? 0x9999u : 0u; });
    Rec(s, {0x005131A0, AddressOf(&Obj::CSkaterLocalNetLogicComponent::SendUpdate)}, 0, 0x31A0);
    Rec(s, {0x00533070, AddressOf(&GameNet::Manager::GetGameId)}, 0, 0x3070, [](Regs&) { return static_cast<uint32_t>(g_a.runCount); });
    Rec(s, {0x00566600}, 1, 0x6600, [](Regs& r) {
        if (r.args[0] == 0xA1)
            return g_a.haveA ? Addr(kSkaterA) : 0u;
        if (r.args[0] == 0xB2)
            return g_a.haveB ? Addr(kSkaterB) : 0u;
        return 0u;
    });
    Rec(s, {0x00409030, AddressOf(&Script::NewStruct)}, 0, 0x9030, [](Regs&) { return Allocate(0x20); }, false, true);
    Rec(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0, 0x6410, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00477090, 0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, 0x7130);
    Rec(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1, 0x9070, nullptr, false, true);
    s.At({0x00478390, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t, const char*)>(
                          &Script::CStruct::AddString))},
         12, [](Regs& r) {
             Record({0x8390, Rel(r.ecx), r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
             return 0u;
         });
    Rec(s, {0x00581580, AddressOf(&Obj::CSkater::GetDisplayName)}, 0, 0x1580,
        [](Regs& r) { return reinterpret_cast<uint32_t>(r.ecx == Addr(kSkaterA) ? "Aaron" : "Bea"); });
    Rec(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                            &Script::RunScript))},
        5, 0x5790, nullptr, false, true);
    Rec(s, {0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 3, 0xF060, nullptr, false, true);
    // The volumes: recorded; Init gives 0.25 / 0.5, the others scale / clear / test them.
    using Sfx::sVolume;
    s.At({0x0047B030, AddressOf(&sVolume::Init)}, 0, [](Regs& r) {
        Record({0xB030, Rel(r.ecx)});
        auto* v = reinterpret_cast<sVolume*>(r.ecx);
        std::memset(v, 0, sizeof(sVolume));
        v->m_channels[0] = 0.25f;
        v->m_channels[1] = 0.5f;
        return r.ecx;
    });
    s.At({0x0047AC50, AddressOf(&sVolume::PercentageAdjustment)}, 4, [](Regs& r) {
        Record({0xAC50, Rel(r.ecx), r.args[0]});
        auto* v = reinterpret_cast<sVolume*>(r.ecx);
        v->m_channels[0] *= Float(r.args[0]);
        return 0u;
    });
    Rec(s, {0x0047AD60, AddressOf(&sVolume::SetSilent)}, 0, 0xAD60, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x14);
        return 0u;
    });
    Rec(s, {0x0047ACF0, AddressOf(&sVolume::IsSilent)}, 0, 0xACF0, [](Regs& r) {
        auto* v = reinterpret_cast<sVolume*>(r.ecx);
        return v->m_channels[0] == 0.0f && v->m_channels[1] == 0.0f ? 1u : 0u;
    });
    using Sfx::CSfxManager;
    Rec(s, {0x0047B4E0, AddressOf(&CSfxManager::StopSound)}, 1, 0xB4E0, [](Regs&) { return 1u; });
    RecFloat(s, {0x0047B760, AddressOf(&CSfxManager::GetDropoffDistance)}, 1, 0xB760, [](Regs&) { return g_a.dropoff; });
    s.At({0x0047BE10, AddressOf(&CSfxManager::GetVolume)}, 24, [](Regs& r) {
        auto* v = reinterpret_cast<sVolume*>(r.args[0]);
        const auto* p = reinterpret_cast<const float*>(r.args[1]);
        Record({0xBE10, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1]), r.args[2], r.args[3], r.args[4], r.args[5]});
        if (g_a.silentVolume) {
            v->m_channels[0] = v->m_channels[1] = 0.0f;
        } else {
            v->m_channels[0] = p[0] / 64.0f;
            v->m_channels[1] = p[2] / 32.0f;
        }
        return 1u;
    });
    Rec(s, {0x0047BD20, AddressOf(&CSfxManager::PlaySound)}, 6, 0xBD20, [](Regs&) { return g_a.soundId; });
    Rec(s, {0x0047B7C0, AddressOf(&CSfxManager::SetSoundParamsScaled)}, 3, 0xB7C0, [](Regs&) { return 1u; });
    RecFloat(s, {0x005BEAA0, AddressOf(&Tmr::GetSlomoFrameSeconds)}, 0, 0xEAA0, [](Regs&) { return g_a.slomo; }, false);
    Rec(s, {0x004443B0, AddressOf(&Terrain::GetTableIndex)}, 1, 0x43B0, [](Regs&) { return 0x2Bu; }, false, true); // (a table index: a byte)
    Rec(s, {0x00444B10, AddressOf(&Terrain::GetTerrainSound)}, 4, 0x4B10, [](Regs& r) {
        auto* entry = reinterpret_cast<uint8_t*>(r.args[0]);
        if (g_a.terrainFound) {
            for (int i = 0; i < 0x84; ++i)
                entry[i] = static_cast<uint8_t>(r.args[1] * 7 + i);
            F<uint32_t>(entry, 0x68) = 0x50D00000 + r.args[1];
            F<float>(entry, 0x6C) = 120.0f + static_cast<float>(r.args[1]);
            F<float>(entry, 0x70) = 60.0f;
        }
        return g_a.terrainFound ? 1u : 0u;
    }, false, true);
    RecFloat(s, {0x00444710, AddressOf(&Terrain::GetScaledVolume)}, 3, 0x4710, [](Regs&) { return g_a.terrainVolume; }, false, true);
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    g_a.heap = kHeap;
    g_structs.clear();
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kControl + 0x2C) = Addr(kState);
}

// The local network logic: made (and out of memory), destroyed, finalized, updated in each case; the graffiti steal
// handler: another run, a skater missing, this skater the victim / the thief / neither.
void TestSkaterLocalNetLogic()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory globals({{0x007CCE14, 4}, {0x007CE478, 4}});
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CCE14) = 0xAE7000 + seed;
            GameVariable<uint32_t>(0x007CE478) = 0x5CA7E000 + seed;
            Originals guard(original, {0x00513630, 0x00512E30, 0x00512E40, 0x00512E70, 0x005136C0, 0x005136F0});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00513630, 0x00512E30, 0x00512E40, kComponent, 0x44, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00512E70))(c);
            for (int k = 0; k < 4; ++k) {
                g_a.multiplayer = (k & 1) != 0;
                g_a.localPlayer = (k & 2) != 0;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005136C0))(c);
            }
            auto* context = reinterpret_cast<Net::MsgHandlerContext*>(At(kContext));
            context->m_Msg = reinterpret_cast<char*>(At(kMessage));
            context->m_data = At(kObject);
            F<uint32_t>(kMessage + 4) = 0xA1;
            F<uint32_t>(kMessage + 8) = 0xB2;
            for (int k = 0; k < 8; ++k) {
                F<uint8_t>(kMessage) = static_cast<uint8_t>(0x40 + seed);
                g_a.runCount = static_cast<uint8_t>(k == 0 ? 0x41 + seed : 0x40 + seed);
                g_a.haveA = k != 1;
                g_a.haveB = k != 2;
                F<uint32_t>(kObject + 0x14) = k % 3 == 0 ? 0xA1u : k % 3 == 1 ? 0xB2u : 0xC3u;
                trace.push_back(static_cast<uint32_t>(
                    reinterpret_cast<int(__cdecl*)(void*)>(Code(original, 0x005136F0))(context)));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater local net logic: made, destroyed, updated, the graffiti steal message");
    }
    delete &s;
}

// The looping sound: made, destroyed (playing or not), registered, initialised, finalized, its speed fraction, its
// suspend; the member functions; the update in every state and case.
void TestSkaterLoopingSound()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory globals({{0x006F85A8, 4}});
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x006F85A8) = 0x5FC00000 + seed;
            Originals guard(original, {0x00513AD0, 0x00513860, 0x005138E0, 0x00513B30, 0x00513FA0, 0x00513880,
                                       0x005138B0, 0x00513970, 0x00513F60, 0x005139E0, 0x00513BB0, 0x00513AB0,
                                       0x00513990});
            std::vector<uint32_t> trace;
            for (uint32_t id : {0u, 0x77u + seed}) {
                F<uint32_t>(kComponent + 0x18) = id;
                TestLifecycle(original, 0x00513AD0, 0x00513B30, 0x00513FA0, kComponent, 0x110, g_a.calloc, trace);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
            }
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x00513860))();
            // Made again for the rest.
            g_a.calloc = Addr(kComponent);
            reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x00513AD0))();
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            SetComponent(At(kStructs), 0xF1A99B27, seed & 1 ? Bits(0.75f) : 0u);
            if (seed & 2)
                g_structs[Addr(kStructs)].erase(0xF1A99B27);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00513880))(c, At(kStructs));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00513970))(c);
            for (float f : {0.5f, 1.0f, 1.5f, -2.0f, std::numeric_limits<float>::quiet_NaN(), 0.875f}) {
                reinterpret_cast<void(__thiscall*)(void*, float)>(Code(original, 0x005138B0))(c, f);
                trace.push_back(F<uint32_t>(kComponent + 0x104));
            }
            for (int k = 0; k < 4; ++k) {
                F<uint32_t>(kComponent + 0x18) = k & 2 ? 0x123u : 0u;
                F<uint8_t>(kComponent + 0xFD) = 0;
                reinterpret_cast<void(__thiscall*)(void*, bool)>(Code(original, 0x00513F60))(c, (k & 1) != 0);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
                trace.push_back(F<uint8_t>(kComponent + 0xFD));
            }
            // The member functions (Suspend through the recording vtable).
            F<uint32_t>(kComponent) = reinterpret_cast<uint32_t>(g_vtable);
            SetComponent(At(kTags), 0x5DB4115F, Addr(kTags + 0x40));
            SetComponent(At(kTags + 0x40), 0x910D77C1, 0);
            for (int k = 0; k < 24; ++k) {
                uint32_t names[] = {0xEDE3935F, 0x9731E193, 0x0B1E7291, 0x12345678};
                F<uint8_t>(kComponent + 0x10C) = static_cast<uint8_t>(k % 3);
                F<uint32_t>(kObject + 0x20) = k & 4 ? Addr(kTags) : 0u;
                F<uint8_t>(kState + 0x118) = k % 5 == 1 ? 1 : 0;
                F<int>(kState + 0x1C) = k % 8 == 6 ? 1 : 0;
                if (k == 12) // (once without the flag)
                    g_structs[Addr(kTags + 0x40)].clear();
                if (k == 13)
                    SetComponent(At(kTags + 0x40), 0x910D77C1, 0);
                trace.push_back(static_cast<uint32_t>(reinterpret_cast<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x005139E0))(c, names[k % 4], At(kStructs), At(kStructs + 0x80))));
                trace.push_back(F<uint8_t>(kComponent + 0x10C));
            }
            // The update: off; each state, sound changed or not, found or not, playing or not, moved or not,
            // the air pitch above or below its floor, the core physics' flag, the speed, silent volumes, the
            // playing sound's volume or pitch changed little or much.
            Lcg random(seed * 1009);
            for (int k = 0; k < 64; ++k) {
                F<uint8_t>(kComponent + 0x10C) = k % 13 == 5 ? 0 : 1;
                F<int>(kComponent + 0x74) = static_cast<int>(random.Below(6));
                F<uint8_t>(kComponent + 0xFD) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kComponent + 0xFC) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kComponent + 0xFE) = static_cast<uint8_t>(random.Below(4) == 0);
                F<uint8_t>(kComponent + 0xFF) = static_cast<uint8_t>(random.Below(3) == 0);
                F<uint8_t>(kComponent + 0x100) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kComponent + 0x60) = static_cast<uint8_t>(random.Below(5));
                g_a.terrainFound = random.Below(4) != 0;
                if (random.Below(3) == 0)
                    F<uint32_t>(kComponent + 0x18) = 0;
                if (random.Below(4) == 0)
                    F<uint32_t>(kComponent + 0x1C) = F<uint32_t>(kComponent + 0x78 + 0x68);
                if (random.Below(3) == 0)
                    std::memcpy(At(kComponent + 0x64), At(kObject + 0x4C), 16);
                else
                    F<float>(kObject + 0x4C) = static_cast<float>(random.Below(400)) - 200.0f;
                F<float>(kComponent + 0x20) = random.Below(4) == 0 ? 45.0f : static_cast<float>(random.Below(100));
                g_a.slomo = 1.0f / 60.0f * static_cast<float>(1 + random.Below(3));
                g_a.haveCore = random.Below(3) != 0;
                F<uint8_t>(kCore + 0x2EC) = static_cast<uint8_t>(random.Below(4) == 0);
                F<float>(kComponent + 0x104) = static_cast<float>(random.Below(5)) / 4.0f - 0.25f;
                g_a.silentVolume = random.Below(6) == 0;
                g_a.terrainVolume = random.Below(2) ? 1.0f : static_cast<float>(random.Below(150));
                g_a.dropoff = 500.0f + static_cast<float>(seed);
                g_a.soundId = random.Below(4) == 0 ? 0u : 0x5000u + static_cast<uint32_t>(k);
                if (random.Below(2)) { // (the last given volume and pitch near what comes now, or not)
                    const float offsets[] = {0.25f, 0.55f, 0.75f};
                    F<float>(kComponent + 0x28) = F<float>(kObject + 0x4C) / 64.0f + offsets[random.Below(3)];
                    F<float>(kComponent + 0x24) = static_cast<float>(random.Below(130));
                }
                if (k % 8 == 3) { // (the air sound playing on: its pitch falls)
                    F<uint8_t>(kComponent + 0x10C) = 1;
                    F<int>(kComponent + 0x74) = 1;
                    F<uint8_t>(kComponent + 0xFD) = 0;
                    F<uint8_t>(kComponent + 0xFC) = 1;
                    F<uint32_t>(kComponent + 0x18) = 0x4400;
                    F<uint32_t>(kComponent + 0x1C) = F<uint32_t>(kComponent + 0x78 + 0x68);
                    F<float>(kObject + 0x4C) = F<float>(kComponent + 0x64) + 1.0f;
                    F<float>(kComponent + 0x20) = 50.0f + static_cast<float>(k);
                    g_a.silentVolume = false;
                }
                if (k % 8 == 5) { // (playing on the ground, the volume and pitch near the last given)
                    F<uint8_t>(kComponent + 0x10C) = 1;
                    F<int>(kComponent + 0x74) = 0;
                    F<uint8_t>(kComponent + 0xFD) = 0;
                    F<uint8_t>(kComponent + 0xFC) = 1;
                    F<uint8_t>(kComponent + 0xFE) = k % 16 == 5 ? 1 : 0;
                    F<uint32_t>(kComponent + 0x18) = 0x4500;
                    F<uint32_t>(kComponent + 0x1C) = F<uint32_t>(kComponent + 0x78 + 0x68);
                    F<float>(kComponent + 0x78 + 0x6C) = 80.0f;
                    F<float>(kComponent + 0x78 + 0x70) = 80.0f;
                    F<float>(kComponent + 0x24) = 80.25f;
                    F<float>(kObject + 0x4C) = F<float>(kComponent + 0x64) + 2.0f;
                    F<float>(kComponent + 0x28) = F<float>(kObject + 0x4C) / 64.0f + (k % 16 == 5 ? 0.125f : 0.55f);
                    F<float>(kComponent + 0x104) = 0.5f;
                    g_a.haveCore = false;
                    g_a.terrainVolume = 1.0f;
                    g_a.silentVolume = false;
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00513BB0))(c);
                for (uint32_t at : {0x18u, 0x1Cu, 0x20u, 0x24u, 0x28u, 0x2Cu, 0xFCu, 0x104u})
                    trace.push_back(F<uint32_t>(kComponent + at));
            }
            for (uint32_t id : {0u, 0x31u}) {
                F<uint32_t>(kComponent + 0x18) = id;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00513AB0))(c);
            }
            uint8_t other[0x2C];
            for (int i = 0; i < 0x2C; ++i)
                other[i] = static_cast<uint8_t>(i * 3 + seed);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, const void*)>(Code(original, 0x00513990))(
                At(kComponent + 0x28), other)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater looping sound: made, destroyed, initialised, suspended, member functions, "
                                  "updated");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterLocalNetLogic, RemoveStandIns);
SELFTEST(Early, TestSkaterLoopingSound, RemoveStandIns);
