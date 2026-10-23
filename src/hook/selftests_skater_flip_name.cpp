// Self-tests for the skater's flip-and-rotate and floating name components (src/game/skater/flip_and_rotate.cpp,
// floating_name.cpp; 0x00511300-0x00511B9E): the original and ours on the same stand-in skater, every call they make
// recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/pool.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/floating_name.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kCore = 0x0500, kState = 0x0600, kModel = 0x0700,
                   kBlender = 0x0900, kPlayer = 0x0A00, kLocalPlayer = 0x0A80, kApp = 0x0B00, kLabel = 0x0C00,
                   kStructs = 0x2000, kParams = 0x3000;

struct Answers {
    uint32_t calloc;
    bool havePlayer, localPlayer, flipNeeded, netGame, showNames, haveLabel, shown, teams, normal, playerIsLocal;
    int clock;
    uint32_t nextStruct;
};
Answers g_a;
void* g_vtable[16];
void* g_labelVtable[16];

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        switch (r.args[0]) {
        case 0x5BD63B29: return Addr(kCore);
        case 0x43686585: return Addr(kState);
        case 0x286A8D26: return Addr(kModel);
        case 0x7149B753: return Addr(kBlender);
        default: return 0u;
        }
    });
    Rec(s, {0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 1, 0xAEA0, [](Regs&) { return g_a.havePlayer ? Addr(kPlayer) : 0u; });
    Rec(s, {0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, 0x3300, [](Regs&) { return g_a.localPlayer ? 1u : 0u; });
    Rec(s, {0x005434B0, AddressOf(&GameNet::PlayerInfo::GetSkaterValue2BC)}, 0, 0x34B0, [](Regs& r) { return 0x100u + (Rel(r.ecx) & 0xFFF); });
    Rec(s, {0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 1, 0x2EA0, [](Regs&) { return Addr(kApp); });
    Rec(s, {0x004549F0, AddressOf(&Net::MsgDesc::Construct)}, 0, 0x49F0, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x20);
        return r.ecx;
    });
    Rec(s, {0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 1, 0x9E50, [](Regs& r) {
        const auto* m = reinterpret_cast<const uint8_t*>(r.args[0]);
        uint16_t length = *reinterpret_cast<const uint16_t*>(m + 2);
        const auto* data = *reinterpret_cast<const uint8_t* const*>(m + 4);
        Record({m[0], m[1], length, HashBytes(m + 8, 0x18)});
        for (int i = 0; i < length && i < 8; ++i)
            Record({data[i]});
        return 0u;
    });
    Rec(s, {0x004FE120}, 0, 0xE120);
    Rec(s, {0x004FC440}, 0, 0xC440, [](Regs&) { return g_a.flipNeeded ? 1u : 0u; });
    Rec(s, {0x00419C10, AddressOf(&Obj::CAnimBlenderComponent::AnyChannel)}, 4, 0x9C10);
    Rec(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, 0xECE0, [](Regs&) { return static_cast<uint32_t>(g_a.clock); },
        false);
    Rec(s, {0x00476B40, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}, 1,
        0x6B40, [](Regs&) { return g_a.normal ? 1u : 0u; });
    // The floating name's.
    Rec(s, {0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_a.netGame ? 1u : 0u; });
    Rec(s, {0x00533140}, 0, 0x3140, [](Regs&) { return g_a.showNames ? 1u : 0u; });
    Rec(s, {0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 3, 0xAE20, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        out[0] = g_a.haveLabel ? Addr(kLabel) : 0u;
        return r.args[0];
    });
    Rec(s, {0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 1, 0x37E0);
    Rec(s, {0x00580050, AddressOf(&Obj::CSkater::GetFlag2C4)}, 0, 0x0050, [](Regs&) { return g_a.shown ? 1u : 0u; });
    Rec(s, {0x00532EC0}, 0, 0x2EC0, [](Regs&) { return g_a.playerIsLocal ? Addr(kPlayer) : Addr(kLocalPlayer); });
    Rec(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_a.teams ? 1u : 0u; });
    Rec(s, {0x00581580, AddressOf(&Obj::CSkater::GetDisplayName)}, 0, 0x1580,
        [](Regs&) { return reinterpret_cast<uint32_t>("Tony Hawk"); });
    s.At({0x006234FE}, 0, StandInSprintf);
    // The structures: from the pool (stood in), one after another in the arena.
    auto structAt = [](Regs&) {
        uint32_t at = Addr(kStructs + g_a.nextStruct);
        g_a.nextStruct += 0x20;
        return at;
    };
    // (the original's NewStruct 0x00409030 recorded as the pool allocation it makes - ours and the original's
    // inlined copies of it reach the pool themselves)
    s.At({0x00409030}, 0, [structAt](Regs& r) {
        Record({0xA3D0, Rel(Addr(0x1F00))});
        return structAt(r);
    });
    Rec(s, {0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, 0xA3D0, structAt);
    Rec(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0, 0x6410, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, 0x7130);
    Rec(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1, 0x9070, nullptr, false, true);
    Rec(s, {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
        2, 0x7D40);
    s.At({0x004779D0, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, const char*)>(&Script::CStruct::AddString))},
         8, [](Regs& r) {
             Record({0x79D0, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    Rec(s, {0x00477E20, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float, float, float)>(
                            &Script::CStruct::AddVector))},
        4, 0x7E20);
    Rec(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                            &Script::RunScript))},
        5, 0x5790, nullptr, false, true);
    for (int i = 0; i < 16; ++i) {
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
        g_labelVtable[i] = RecThunk(s, 1, 0x1AB00 + i);
    }
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kLabel) = reinterpret_cast<uint32_t>(g_labelVtable);
}

// Flip-and-rotate: made, destroyed, finalized; every member function name (each flag / answer); SetBoardRotated
// (sent or not, player there or not, local or not); the "after" flags cleared; Flip; DoAfterAnim with each flag.
void TestSkaterFlipAndRotate()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory clock({{0x00696D60, 4}});
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<int>(0x00696D60) = 0x1000 * static_cast<int>(seed);
            Originals guard(original, {0x00511470, 0x00511300, 0x00511320, 0x00511350, 0x00511390, 0x00511310,
                                       0x00511420, 0x005114F0, 0x00511570, 0x00511600});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00511470, 0x00511300, 0x00511320, kComponent, 0x28, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511350))(c);
            Lcg random(seed * 31);
            auto randomise = [&]() {
                for (uint32_t i = 0x18; i < 0x1C; ++i)
                    F<uint8_t>(kComponent + i) = static_cast<uint8_t>(random.Below(2)); // (bools)
                F<uint8_t>(kState + 0x28) = static_cast<uint8_t>(random.Below(3));
                F<uint32_t>(kObject + 0x14) = random.Below(8) + 0x10 * seed;
                F<int>(kObject + 0x2BC) = static_cast<int>(random.Below(4));
                F<uint8_t>(kModel + 0x1F4) = 0x77;
                g_a.havePlayer = random.Below(4) != 0;
                g_a.localPlayer = random.Below(2) != 0;
                g_a.flipNeeded = random.Below(2) != 0;
                g_a.normal = random.Below(2) != 0;
                g_a.clock = static_cast<int>(random.Next());
            };
            const uint32_t names[] = {0x79EE5CCF, 0x63E93F69, 0x0B1E7291, 0x587FBDD7, 0x65011BAA, 0x72289A28, 0xD9DE0C65,
                                      0xA6243DAA, 0xACAC1BAD, 0xEBA11B20, 0xE0F3A644, 0x79EE5CCE, 0x00000000};
            for (uint32_t name : names) {
                for (int k = 0; k < 4; ++k) {
                    randomise();
                    g_a.havePlayer = true; // (BoardRotate and DoAfterAnim don't check)
                    trace.push_back(reinterpret_cast<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                        Code(original, 0x00511600))(c, name, At(kParams), At(kParams + 0x100)));
                    trace.push_back(F<uint32_t>(kComponent + 0x18));
                }
            }
            for (int k = 0; k < 16; ++k) {
                randomise();
                trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, uint32_t, bool, uint32_t, bool)>(
                    Code(original, 0x00511390))(c, 0x300 + seed * 7 + k, (k & 1) != 0, 0xAB0000 + k, (k & 2) != 0)
                                    ? 1u
                                    : 0u);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
                randomise();
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511310))(c);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
                randomise();
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511420))(c);
                randomise();
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005114F0))(c);
                randomise();
                g_a.havePlayer = true;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511570))(c);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater flip and rotate component: made, destroyed, member functions, board, flip");
    }
    delete &s;
}

// The floating name: made, destroyed (the label destroyed), its id from the object id (0-7 and others); the update:
// no network game / names off (suspended), a label or none, shown / not / hidden, this machine's player or another,
// a team game or not.
void TestSkaterFloatingName()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory pool({{0x006F3614, 16}});
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            auto& pools = GameVariable<Mem::PoolPair>(0x006F3614);
            pools.current = 0;
            pools.pools[0] = reinterpret_cast<Mem::Pool*>(At(0x1F00));
            pools.pools[1] = nullptr;
            Originals guard(original, {0x00511810, 0x00511AA0, 0x00511B80, 0x00511780, 0x00511870});
            std::vector<uint32_t> trace;
            F<uint32_t>(kComponent + 0x18) = 0xAAAA0000 + seed;
            TestLifecycle(original, 0x00511810, 0x00511AA0, 0x00511B80, kComponent, 0x1C, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            for (uint32_t id : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 0xFFFFFFFFu, 0x80000003u}) {
                F<uint32_t>(kComponent + 0x18) = 0x5EED0000 + seed;
                F<uint32_t>(kObject + 0x14) = id;
                reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00511780))(c, At(kParams));
                trace.push_back(F<uint32_t>(kComponent + 0x18));
            }
            Lcg random(seed * 13);
            for (int k = 0; k < 40; ++k) {
                F<uint32_t>(kComponent) = reinterpret_cast<uint32_t>(g_vtable);
                g_a.netGame = k % 9 != 1;
                g_a.showNames = k % 7 != 2;
                g_a.haveLabel = (k & 1) != 0;
                g_a.shown = (k & 6) != 6;
                F<uint32_t>(kObject + 0x104) = (k & 8) ? 0x04u | random.Below(4) : random.Below(4) & 3;
                g_a.playerIsLocal = (k & 16) != 0;
                g_a.teams = random.Below(2) != 0;
                g_a.havePlayer = true; // (a network game's skater always has one: a team game reads its team)
                F<uint32_t>(kObject + 0x14) = random.Below(8);
                F<uint32_t>(kPlayer + 0x30) = random.Below(4) + 0x10;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511870))(c);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater floating name component: made, destroyed, its id, updated");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterFlipAndRotate, RemoveStandIns);
SELFTEST(Early, TestSkaterFloatingName, RemoveStandIns);
