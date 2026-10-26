// Self-tests for the skater's sound, stance panel and state components (src/game/skater/skater_sound.cpp,
// stance_panel.cpp, skater_state.cpp; 0x00518C80-0x005197EF): the original and ours on the same stand-in skater, every
// call they make recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/sprite_element.h"
#include "game/mth/vector.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/terrain.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/skater/skater_state.h"
#include "game/skater/stance_panel.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0200, kCore = 0x0600, kState = 0x0800, kElement = 0x0A00,
                   kScript = 0x0C00, kParams = 0x0D00, kResults = 0x0E00;

struct Answers {
    uint32_t calloc;
    float length;
    bool haveElement;
};
Answers g_a;
void* g_vtable[16];

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    StandInStructGetters(s);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs&) { return Addr(kCore); });
    // The sound's.
    Rec(s, {0x004443B0, AddressOf(&Terrain::GetTableIndex)}, 1, 0x43B0, [](Regs& r) { return 0x7AB00000u ^ r.args[0]; }, false, true);
    s.At({0x00444BB0, AddressOf(&Terrain::PlayTerrainSound)}, 0, [](Regs& r) {
        // (the table and the terrain: only their bytes count - the original passes stale upper bytes)
        Record({0x4BB0, r.args[0] & 0xFF, r.args[1] & 0xFF, Rel(r.args[2]), r.args[3], r.args[4], r.args[5], r.args[6]});
        return 0u;
    });
    RecFloat(s, {0x004023C0, AddressOf(&Mth::Vector::Length)}, 0, 0x23C0, [](Regs&) { return g_a.length; });
    // The stance panel's.
    Rec(s, {0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 3, 0xAE20, [](Regs& r) {
        reinterpret_cast<uint32_t*>(r.args[0])[0] = g_a.haveElement ? Addr(kElement) : 0u;
        return r.args[0];
    });
    Rec(s, {0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 1, 0x37E0);
    Rec(s, {0x0057FEC0, AddressOf(&Obj::CSkater::GetSkaterNumber)}, 0, 0xFEC0, [](Regs&) { return 3u; });
    Rec(s, {0x004A8630, AddressOf(&Front::CScreenElement::SetAlpha)}, 2, 0x8630);
    Rec(s, {0x004AF130, AddressOf(&Front::CSpriteElement::SetTexture)}, 1, 0xF130);
    Rec(s, {0x00460A20, AddressOf(&Obj::CObject::BroadcastEvent)}, 3, 0x0A20);
    // The state's.
    Rec(s, {0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 2, 0x09F0);
    Rec(s, {0x00477B80, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))},
        2, 0x7B80);
    Rec(s, {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
        2, 0x7D40);
    Rec(s, {0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 3, 0xF060, nullptr, false, true);
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    g_structs.clear();
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kScript + 0x14) = Addr(kResults);
    F<uint32_t>(kCore + 0x0C) = Addr(kObject);
    F<uint32_t>(kCore + 0x1528) = Addr(kState);
}

using Cmf = int(__thiscall*)(void*, uint32_t, void*, void*);

// The sound: made, destroyed, initialised (volume_mult or not); each play function and member function in each
// state, terrain and slide flag.
void TestSkaterSound()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x00518E20, 0x00518C80, 0x00518C90, 0x00518CC0, 0x00518FD0, 0x00518CF0,
                                       0x00518D60, 0x00518DD0, 0x00518EA0, 0x00518FA0, 0x00518EE0, 0x00518F00,
                                       0x00518F50});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00518E20, 0x00518C80, 0x00518C90, kComponent, 0x28, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            for (int k = 0; k < 2; ++k) {
                g_structs.clear();
                if (k)
                    SetComponent(At(kParams), 0xF1A99B27, Bits(0.5f + static_cast<float>(seed)));
                reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00518CC0))(c, At(kParams));
                trace.push_back(F<uint32_t>(kComponent + 0x24));
            }
            Lcg random(seed * 17);
            const uint32_t names[] = {0x0E338369, 0x0069E457, 0x2C779F22, 0x6F5E9124, 0x12345678};
            for (int k = 0; k < 80; ++k) {
                F<int>(kComponent + 0x18) = static_cast<int>(random.Below(7)) - 1;
                F<uint8_t>(kComponent + 0x1C) = static_cast<uint8_t>(random.Below(16));
                F<uint8_t>(kComponent + 0x1D) = static_cast<uint8_t>(random.Below(16));
                F<uint8_t>(kComponent + 0x1E) = static_cast<uint8_t>(random.Below(2));
                F<float>(kComponent + 0x20) = 1.0f + static_cast<float>(random.Below(8)) / 2.0f;
                for (uint32_t at : {0xACu, 0xB0u, 0xB4u})
                    F<float>(kObject + at) = static_cast<float>(static_cast<int>(random.Below(81)) - 40) / 3.0f;
                g_a.length = static_cast<float>(random.Below(50)) / 3.0f;
                float speed = static_cast<float>(random.Below(40)) / 7.0f;
                uint32_t terrain = random.Below(16) | 0x5500;
                switch (k % 10) {
                case 0: reinterpret_cast<void(__thiscall*)(void*, float, uint32_t)>(Code(original, 0x00518CF0))(c, speed, terrain); break;
                case 1: reinterpret_cast<void(__thiscall*)(void*, float, uint32_t, int)>(Code(original, 0x00518D60))(c, speed, terrain, static_cast<int>(random.Below(6))); break;
                case 2: reinterpret_cast<void(__thiscall*)(void*, float, uint32_t)>(Code(original, 0x00518DD0))(c, speed, terrain); break;
                case 3: reinterpret_cast<void(__thiscall*)(void*, float)>(Code(original, 0x00518EA0))(c, speed); break;
                case 4: reinterpret_cast<void(__thiscall*)(void*, float)>(Code(original, 0x00518FA0))(c, speed); break;
                case 5: reinterpret_cast<void(__thiscall*)(void*, float, uint32_t)>(Code(original, 0x00518EE0))(c, speed, terrain); break;
                case 6: reinterpret_cast<void(__thiscall*)(void*, float)>(Code(original, 0x00518F00))(c, speed); break;
                case 7: reinterpret_cast<void(__thiscall*)(void*, float)>(Code(original, 0x00518F50))(c, speed); break;
                default:
                    trace.push_back(static_cast<uint32_t>(reinterpret_cast<Cmf>(Code(original, 0x00518FD0))(
                        c, names[random.Below(5)], At(kParams), At(kScript))));
                    break;
                }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater sound: made, destroyed, initialised, every sound and member function");
    }
    delete &s;
}

// The stance panel: made, destroyed, initialised, finalized; the stance in every case; updates (changed or not, an
// element or none, hidden or a texture); the member functions on each flag.
void TestSkaterStancePanel()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory manager({{0x00701440, 4}});
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x00701440) = 0x5E000000 + seed;
            Originals guard(original, {0x005193B0, 0x00519130, 0x005191E0, 0x00519140, 0x00519210, 0x00519230,
                                       0x005192C0, 0x00519150});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x005193B0, 0x00519130, 0x005191E0, kComponent, 0x24, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00519140))(c, At(kParams));
            trace.push_back(F<uint32_t>(kComponent + 0x18));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00519210))(c);
            Lcg random(seed * 19);
            const uint32_t names[] = {0x41EB710B, 0x1EB61DCE, 0x0A23D710, 0x71B57DD6, 0xFB9B7C9C, 0x9FAB9D0B,
                                      0x12345678};
            for (int k = 0; k < 64; ++k) {
                F<uint8_t>(kComponent + 0x1D) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kComponent + 0x1E) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kObject + 0x110) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kState + 0x28) = static_cast<uint8_t>(random.Below(3));
                if (random.Below(3) == 0)
                    F<int>(kComponent + 0x18) = static_cast<int>(random.Below(6));
                g_a.haveElement = random.Below(4) != 0;
                trace.push_back(static_cast<uint32_t>(
                    reinterpret_cast<int(__thiscall*)(void*)>(Code(original, 0x00519150))(c)));
                if (g_a.haveElement || F<int>(kComponent + 0x18) == 99) // (a missing element is used: a crash)
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00519230))(c);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
                trace.push_back(static_cast<uint32_t>(reinterpret_cast<Cmf>(Code(original, 0x005192C0))(
                    c, names[k % 7], At(kParams), At(kScript))));
                trace.push_back(F<uint32_t>(kComponent + 0x1C));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater stance panel: made, destroyed, the stance, updated, member functions");
    }
    delete &s;
}

// The state: made, destroyed, registered, initialised; its setters (changed or not); every member function; the
// state history's three.
void TestSkaterState()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x00519740, 0x00519430, 0x00519450, 0x00519620, 0x00519460, 0x00519660,
                                       0x00519680, 0x005196A0, 0x00519470, 0x00519650, 0x00519480, 0x00519490,
                                       0x005194A0, 0x005194B0, 0x005194C0, 0x005196D0, 0x005194D0, 0x005194F0,
                                       0x00519510, 0x00519520, 0x00519530, 0x005196E0, 0x00519540, 0x005195B0,
                                       0x005196F0, 0x00519720, 0x00519600, 0x005197D0, 0x005197E0, 0x005197C0});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00519740, 0x00519450, 0x00519620, kComponent, 0x128, g_a.calloc, trace);
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x00519430))();
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00519460))(c, At(kParams));
            trace.push_back(F<uint32_t>(kComponent + 0x18));
            Lcg random(seed * 23);
            const uint32_t members[] = {0x00519480, 0x00519490, 0x005194A0, 0x005194B0, 0x005194C0, 0x005196D0,
                                        0x005194D0, 0x005194F0, 0x00519510, 0x00519520, 0x00519530, 0x005196E0,
                                        0x00519540, 0x005195B0, 0x005196F0, 0x00519720, 0x00519600};
            for (int k = 0; k < 120; ++k) {
                F<int>(kComponent + 0x18) = static_cast<int>(random.Below(7)) - 1;
                F<int>(kComponent + 0x1C) = static_cast<int>(random.Below(3)) - 1;
                F<uint8_t>(kComponent + 0x70) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kComponent + 0xE0) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kComponent + 0xE1) = static_cast<uint8_t>(random.Below(4));
                F<uint8_t>(kComponent + 0x118) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kComponent + 0x120) = static_cast<uint8_t>(random.Below(3));
                g_structs.clear();
                if (random.Below(4))
                    SetComponent(At(kParams), 0, random.Below(2) ? GameVariable<uint32_t>(0x0067F09C) : 0x12345u);
                switch (k % 4) {
                case 0: reinterpret_cast<void(__thiscall*)(void*, int)>(Code(original, 0x00519660))(c, static_cast<int>(random.Below(6))); break;
                case 1: reinterpret_cast<void(__thiscall*)(void*, int)>(Code(original, 0x00519680))(c, static_cast<int>(random.Below(3)) - 1); break;
                case 2: reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(Code(original, 0x005196A0))(c, 0x7700 + random.Below(4)); break;
                default: reinterpret_cast<void(__thiscall*)(void*)>(Code(original, k & 4 ? 0x00519470 : 0x00519650))(c); break;
                }
                trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*)>(
                                    Code(original, members[k % 17]))(c, At(kParams), At(kScript))
                                    ? 1u
                                    : 0u);
                for (uint32_t at : {0x18u, 0x1Cu, 0x70u, 0xE0u, 0x118u, 0x120u, 0x124u})
                    trace.push_back(F<uint32_t>(kComponent + at));
            }
            // The state history's frame time and destructor body.
            uint8_t* frame = At(0x2000);
            for (uint32_t time : {0x12345678u, 0xFFFF0001u, 0u}) {
                reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(Code(original, 0x005197E0))(frame, time);
                trace.push_back(F<uint32_t>(0x2078));
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x005197D0))(frame));
            }
            F<uint32_t>(0x2100) = 0x0BADF00D;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005197C0))(At(0x2100));
            trace.push_back(Rel(F<uint32_t>(0x2100)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater state: made, destroyed, set, every member function; state history frames");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterSound, RemoveStandIns);
SELFTEST(Early, TestSkaterStancePanel, RemoveStandIns);
SELFTEST(Early, TestSkaterState, RemoveStandIns);
