// Self-test for the skater's physics control component (src/game/skater/physics_control.cpp, 0x005176A0-0x00517CB1):
// the original and ours on the same stand-in skater, every call they make recorded.

#include <initializer_list>
#include <utility>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/matrix.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/physics_control.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/skater/skater_camera_component.h"
#include "game/skater/walk_camera.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout: the component, the skater, its components (each with the recording vtable), the camera object.
constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kCore = 0x0300, kState = 0x0500, kRotate = 0x0600,
                   kAdjust = 0x0640, kFinalize = 0x0680, kWalk = 0x0700, kCamera = 0x0F00, kSkaterCamera = 0x1100,
                   kWalkCamera = 0x1140, kScript = 0x1200, kResults = 0x1300;

struct Answers {
    uint32_t calloc;
    int clock;
};
Answers g_a;
void* g_vtable[16];

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        bool camera = r.ecx == Addr(kCamera);
        switch (r.args[0]) {
        case 0x5BD63B29: return Addr(kCore);
        case 0x43686585: return Addr(kState);
        case 0x52D61A0D: return Addr(kRotate);
        case 0x9F9CC949: return Addr(kAdjust);
        case 0x9B373E58: return Addr(kFinalize);
        case 0x726E85AA: return Addr(kWalk);
        case 0x5E43A604: return camera ? Addr(kSkaterCamera) : 0u;
        case 0xC7D37EC2: return camera ? Addr(kWalkCamera) : 0u;
        default: return 0u;
        }
    });
    Rec(s, {0x00580880, AddressOf(&Obj::CSkater::GetSkaterCamera)}, 0, 0x0880, [](Regs&) { return Addr(kCamera); });
    Rec(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, 0xECE0,
        [](Regs&) { return static_cast<uint32_t>(g_a.clock += 0x111); }, false);
    Rec(s, {0x004FDD50, AddressOf(&Skater::CorePhysicsComponent::LeaveSkating)}, 0, 0xDD50);
    // (bool arguments: only their low byte is read - the original passes SETcc results with stale upper bytes)
    s.At({0x0050C670, AddressOf(&Skater::CorePhysicsComponent::ResumeSkating)}, 12, [](Regs& r) {
        Record({0xC670, Rel(r.ecx), r.args[0] & 0xFF, Rel(r.args[1]), Rel(r.args[2])});
        return 0u;
    });
    s.At({0x00519680, AddressOf(&Obj::CSkaterStateComponent::SetPhysicsMode)}, 4, [](Regs& r) {
        Record({0x9680, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0052AF40, AddressOf(&Obj::CWalkComponent::Begin)}, 4, [](Regs& r) {
        Record({0xAF40, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    Rec(s, {0x005260C0, AddressOf(&Obj::CWalkComponent::ClearModelOffset)}, 0, 0x60C0);
    // The cameras' state: written by one, read (hashed) by the other.
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x004FA420u, AddressOf(&Obj::CSkaterCameraComponent::SaveState)}, {0x00525060u, AddressOf(&Obj::CWalkCameraComponent::SaveState)}}) {
        Rec(s, {address, ours_}, 1, address & 0xFFFF, [](Regs& r) {
            for (int i = 0; i < 0x48; ++i)
                reinterpret_cast<uint8_t*>(r.args[0])[i] = static_cast<uint8_t>(i * 5 + r.ecx);
            return 0u;
        });
    }
    for (auto [address, ours_] : std::initializer_list<std::pair<uint32_t, uint32_t>>{{0x00524FF0u, AddressOf(&Obj::CWalkCameraComponent::RestoreState)}, {0x004F9A40u, AddressOf(&Obj::CSkaterCameraComponent::SetState)}}) {
        Rec(s, {address, ours_}, 1, address & 0xFFFF, [](Regs& r) {
            g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 0x48));
            return 0u;
        });
    }
    Rec(s, {0x00404090, AddressOf(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(
                            &Mth::Matrix::operator=))},
        1, 0x4090, [](Regs& r) {
            std::memmove(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 64);
            return r.ecx;
        });
    Rec(s, {0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 3, 0xF060, nullptr, false, true);
    Rec(s, {0x00477C60, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))},
        2, 0x7C60);
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
}

void TestSkaterPhysicsControl()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory clock({{0x00696D60, 4}});
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            FillArena(seed);
            g_a = {};
            g_a.clock = static_cast<int>(seed * 0x10000);
            GameVariable<int>(0x00696D60) = static_cast<int>(seed * 0x777);
            Originals guard(original, {0x00517830, 0x005176A0, 0x005176C0, 0x00517740, 0x005176D0, 0x00517770,
                                       0x00517AC0, 0x00517C80, 0x005178B0, 0x00517940, 0x00517B20, 0x00517A80,
                                       0x00517AA0, 0x005177A0, 0x005177F0, 0x005176E0, 0x005176F0, 0x00517700,
                                       0x00517710, 0x00517720, 0x00517730});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00517830, 0x005176C0, 0x00517740, kComponent, 0x30, g_a.calloc, trace);
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x005176A0))();
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            for (uint32_t at : {kCore, kRotate, kAdjust, kFinalize, kWalk, kSkaterCamera, kWalkCamera})
                F<uint32_t>(at) = reinterpret_cast<uint32_t>(g_vtable);
            void* c = At(kComponent);
            F<uint8_t>(kComponent + 0x18) = 0x5A;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x005176D0))(c, At(kScript));
            trace.push_back(F<uint32_t>(kComponent + 0x18));
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00517770))(c);
            trace.push_back(F<uint32_t>(kState + 0x1C));
            F<uint32_t>(kScript + 0x14) = Addr(kResults);
            Lcg random(seed * 3);
            auto randomise = [&]() {
                F<int>(kState + 0x1C) = static_cast<int>(random.Below(4)) - 1;
                F<uint32_t>(kObject + 0x104) = random.Below(4);
                F<uint8_t>(kComponent + 0x18) = static_cast<uint8_t>(random.Below(2));
                F<uint32_t>(kCore + 0x1528) = Addr(kCore + 0x100);
                F<int>(kCore + 0x100 + 0x18) = static_cast<int>(random.Below(3));
                F<uint32_t>(kCore + 8) = random.Below(4);
                F<int>(kWalk + 0x748) = static_cast<int>(random.Below(4));
                F<int>(kWalk + 0x18) = static_cast<int>(random.Below(2));
                F<int>(kComponent + 0x1C) = static_cast<int>(random.Next()) - 0x400000;
                F<int>(kComponent + 0x20) = static_cast<int>(random.Next() * 7u);
            };
            for (int k = 0; k < 40; ++k) {
                randomise();
                reinterpret_cast<void(__thiscall*)(void*, bool)>(Code(original, 0x005178B0))(c, (k & 1) != 0);
                trace.push_back(F<uint32_t>(kComponent + 0x18));
                randomise();
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00517AC0))(c);
                trace.push_back(F<uint32_t>(kComponent + 0x24));
                randomise();
                const uint32_t names[] = {0x5C038F9B, 0x6E8E39B3, 0x12345678};
                trace.push_back(static_cast<uint32_t>(reinterpret_cast<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x00517C80))(c, names[k % 3], At(kScript), At(kScript))));
                trace.push_back(F<uint32_t>(kComponent + 0x1C));
                trace.push_back(F<uint32_t>(kComponent + 0x20));
                for (uint32_t member : {0x00517A80u, 0x00517AA0u, 0x005177A0u, 0x005177F0u, 0x005176E0u, 0x00517700u,
                                        0x005176F0u, 0x00517730u, 0x00517710u, 0x00517700u, 0x00517730u, 0x00517720u}) {
                    randomise();
                    trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*)>(Code(original, member))(
                                        c, At(kScript), At(kScript))
                                        ? 1u
                                        : 0u);
                    trace.push_back(F<uint32_t>(kComponent + 0x24));
                }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater physics control: made, paused, switched, updated, member functions");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterPhysicsControl, RemoveStandIns);
