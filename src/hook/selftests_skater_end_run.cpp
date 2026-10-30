// Self-tests for the skater's end-of-run and finalize-physics components (src/game/skater/end_run.cpp,
// finalize_physics.cpp; 0x00510D20-0x005112F8): the original and ours on the same stand-in skater, every call they
// make recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/mth/matrix.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/skater/end_run.h"
#include "game/skater/finalize_physics.h"
#include "game/skater/looping_sound.h"
#include "game/skater/skater.h"
#include "game/net/gamenet_manager.h"
#include "game/skate/skate_module.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kCore = 0x0400, kState = 0x0800, kSound = 0x0A00,
                   kLooping = 0x0B00, kOtherObject = 0x1000;

struct Answers {
    uint32_t calloc;
    bool multiplayer, modeEndsRuns, serverTold, patched, noComponents;
    float maxSpeed;
};
Answers g_a;
void* g_vtable[16];

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    Rec(s, {0x005651F0, AddressOf(&Mdl::Skate::IsMultiplayer)}, 0, 0x51F0, [](Regs&) { return g_a.multiplayer ? 1u : 0u; });
    Rec(s, {0x00553550}, 0, 0x3550, [](Regs&) { return g_a.modeEndsRuns ? 1u : 0u; });
    Rec(s, {0x005332A0, AddressOf(&GameNet::Manager::GameEndedSent)}, 0, 0x32A0, [](Regs&) { return g_a.serverTold ? 1u : 0u; });
    Rec(s, {0x00533250, AddressOf(&GameNet::Manager::SendGameEnded)}, 0, 0x3250);
    Rec(s, {0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 2, 0x09F0);
    Rec(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                            &Script::RunScript))},
        5, 0x5790, nullptr, false, true);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        if (g_a.noComponents)
            return 0u;
        switch (r.args[0]) {
        case 0x5BD63B29: return Addr(kCore);
        case 0x43686585: return Addr(kState);
        case 0x187574FA: return Addr(kSound);
        case 0xB5F1E9D2: return Addr(kLooping);
        default: return 0u;
        }
    });
    // The matrix functions: recorded, and each changes the matrix so their order shows.
    Rec(s, {0x00405170, AddressOf(&Mth::Matrix::OrthoNormalizeAbout)}, 1, 0x5170, [](Regs& r) {
        F<float>(reinterpret_cast<void*>(r.ecx), 4 * r.args[0]) += 1.5f;
        return r.ecx;
    });
    Rec(s, {0x00404090, AddressOf(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(
                            &Mth::Matrix::operator=))},
        1, 0x4090, [](Regs& r) {
            g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 64));
            std::memmove(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 64);
            return r.ecx;
        });
    Rec(s, {0x00405480, AddressOf(&Mth::Matrix::PatchOrthogonality)}, 0, 0x5480, [](Regs& r) {
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(r.ecx), 64));
        return g_a.patched ? 1u : 0u;
    });
    RecFloat(s, {0x00580790, AddressOf(&Obj::CSkater::GetScaledPhysics)}, 1, 0x0790, [](Regs&) { return g_a.maxSpeed; });
    Rec(s, {0x005138B0, AddressOf(&Obj::CSkaterLoopingSoundComponent::SetSpeedFraction)}, 1, 0x38B0);
    // The component's Suspend (vtable +0x1C).
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kCore + 0x0C) = Addr(kOtherObject);
}

// The end-of-run component: made, destroyed; each member function name with flag sets; the update and EndRun in
// every case; the flag getters and setters.
void TestSkaterEndRun()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x00510F90, 0x00510D20, 0x00510D40, 0x00510D70, 0x00510DC0, 0x00510D30,
                                       0x00510EA0, 0x00510F10, 0x00510F30, 0x00510F40, 0x00510F50, 0x00510F60,
                                       0x00510F70, 0x00510F80});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00510F90, 0x00510D20, 0x00510D40, kComponent, 0x1C, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            uint32_t& flags = F<uint32_t>(kComponent + 0x18);
            void* c = At(kComponent);
            Lcg random(seed);
            const uint32_t names[] = {0x95BDCFCD, 0x00AFC6A7, 0x4C58771E, 0x69A9E37F, 0x962DEA01,
                                      0xB866BF9B, 0xEA77E667, 0x95BDCFCC, 0x962DEA02, 0x12345678};
            for (uint32_t name : names) {
                for (uint32_t f : {0x00u, 0x01u, 0x03u, 0x3Fu, 0x14u, 0x2Au, 0xFFFFFFC1u}) {
                    flags = f ^ (seed << 8);
                    trace.push_back(reinterpret_cast<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                        Code(original, 0x00510DC0))(c, name, At(0x2000), At(0x2100)));
                    trace.push_back(flags);
                }
            }
            // Update
            for (int k = 0; k < 16; ++k) {
                F<uint32_t>(kComponent) = reinterpret_cast<uint32_t>(g_vtable);
                g_a.multiplayer = (k & 1) != 0;
                F<uint8_t>(kObject + 0xBC) = (k & 2) ? static_cast<uint8_t>(seed) : 0;
                flags = (k & 4) ? 0x2u | (seed & 1) : 0x1u | (seed << 3);
                g_a.serverTold = (k & 8) != 0;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00510D70))(c);
                trace.push_back(flags);
            }
            // EndRun
            for (int k = 0; k < 32; ++k) {
                g_a.modeEndsRuns = (k & 1) != 0;
                F<uint8_t>(kObject + 0xBC) = (k & 2) ? 1 : 0;
                flags = ((k >> 2) & 3) == 0 ? 0x19u : ((k >> 2) & 3) == 1 ? 0x2Bu : ((k >> 2) & 3) == 2 ? 0x06u : 0x31u;
                bool force = (k & 16) != 0;
                reinterpret_cast<void(__thiscall*)(void*, bool)>(Code(original, 0x00510EA0))(c, force);
                trace.push_back(flags);
            }
            // EndGoalRun, GetFlags, the getters and clearers
            for (uint32_t f : {0x3Fu, 0x10u, 0x00u, 0x15u, 0x2Au}) {
                flags = f;
                reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x00510F10))(c, At(0x2000));
                trace.push_back(flags);
                flags = f ^ 0xFF00 ^ seed;
                uint32_t out = 0;
                trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t*)>(
                    Code(original, 0x00510D30))(c, &out)));
                trace.push_back(out);
                for (uint32_t getter : {0x00510F30u, 0x00510F40u, 0x00510F50u})
                    trace.push_back(reinterpret_cast<bool(__thiscall*)(void*)>(Code(original, getter))(c) ? 1u : 0u);
                for (uint32_t clearer : {0x00510F60u, 0x00510F70u, 0x00510F80u}) {
                    flags = f ^ (random.Next() & 0x3F);
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, clearer))(c);
                    trace.push_back(flags);
                }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater end run component: made, destroyed, member functions, update, EndRun, flags");
    }
    delete &s;
}

// The finalize-physics component: made, destroyed, finalized; its update in every case (out of a lip or not, each
// state, speeds below 1 or not - NaN too -, the matrix repaired or not, sound values changed or not).
void TestSkaterFinalizePhysics()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x00511280, 0x00511010, 0x00511020, 0x00511050, 0x00511080});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00511280, 0x00511010, 0x00511020, kComponent, 0x20, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            g_a.noComponents = true;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511050))(c);
            trace.push_back(F<uint32_t>(kComponent + 0x18));
            trace.push_back(F<uint32_t>(kComponent + 0x1C));
            g_a.noComponents = false;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511050))(c);
            Lcg random(seed * 77);
            const float speeds[] = {0.0f, 0.5f, -0.99f, 1.0f, -1.0f, 3.0f, std::numeric_limits<float>::quiet_NaN()};
            for (int k = 0; k < 48; ++k) {
                F<uint8_t>(kCore + 0x256) = (k & 1) ? static_cast<uint8_t>(1 + (k & 2)) : 0;
                F<int>(kState + 0x18) = (k >> 1) % 6;
                F<float>(kObject + 0xAC) = speeds[random.Below(7)];
                F<float>(kObject + 0xB4) = speeds[random.Below(7)];
                g_a.patched = (k % 5) == 2;
                g_a.maxSpeed = 10.0f + static_cast<float>(random.Below(100)) / 7.0f;
                if (k % 3 == 0) {
                    F<uint8_t>(kLooping + 0x100) = F<uint8_t>(kState + 0x58);
                    F<uint8_t>(kLooping + 0x60) = F<uint8_t>(kState + 0xE1);
                    F<int>(kLooping + 0x74) = F<int>(kState + 0x18);
                    F<uint8_t>(kLooping + 0xFD) = 0;
                } else if (k % 3 == 1) { // (only the state differs)
                    F<uint8_t>(kLooping + 0x100) = F<uint8_t>(kState + 0x58);
                    F<uint8_t>(kLooping + 0x60) = F<uint8_t>(kState + 0xE1);
                    F<int>(kLooping + 0x74) = F<int>(kState + 0x18) + 1;
                    F<uint8_t>(kLooping + 0xFD) = 0;
                } else {
                    F<uint8_t>(kState + 0x58) = static_cast<uint8_t>(random.Next());
                    F<uint8_t>(kState + 0xE1) = static_cast<uint8_t>(random.Next());
                    F<uint8_t>(kLooping + 0xFD) = static_cast<uint8_t>(k & 4 ? 0 : 0x10);
                }
                F<uint8_t>(kState + 0x70) = static_cast<uint8_t>(random.Next());
                F<uint8_t>(kState + 0xF4) = 0xAA;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00511080))(c);
                trace.push_back(F<uint8_t>(kState + 0xF4));
                trace.push_back(F<uint32_t>(kLooping + 0xFC));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater finalize physics component: made, destroyed, finalized, updated");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterEndRun, RemoveStandIns);
SELFTEST(Early, TestSkaterFinalizePhysics, RemoveStandIns);
