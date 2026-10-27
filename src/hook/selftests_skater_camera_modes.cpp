// Self-tests for the skater camera's functions at 0x004F9970-0x004F9E7E (skater_camera_modes.cpp): original vs ours on a
// stand-in component in skater_core_testkit's arena, every callee stood in for (the script values chosen per variant).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/camera.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skater/skater_camera_modes.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Obj::CSkaterCameraModes;

constexpr uint32_t kCam = kFree;                  // the component (0x170 bytes)
constexpr uint32_t kLookaround = kFree + 0x180;
constexpr uint32_t kCameraComp = kFree + 0x1C0;   // +0x18 its Gfx::Camera
constexpr uint32_t kState = kFree + 0x200;        // a state handed over (0x58 bytes)
constexpr uint32_t kParamsArea = kFree + 0x280;

bool g_missing;

std::vector<StandIn> CameraStandIns()
{
    return {
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame& f, Result& r) {
             r.eax = reinterpret_cast<uint32_t>(ArenaPtr(f.args[0] == 0x81C7122D ? kLookaround : kCameraComp));
         }},
        {0x004A0710, 0, 0, Ret::Int, false, 0, {}, [](Frame&, Result& r) { r.eax = Next() % 3; }},
        {0x00479090, 0, 0, Ret::Int, false, 0,
         {Ours(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, [](Frame& f, Result& r) {
             const char* name = reinterpret_cast<const char*>(f.args[0]);
             Record(static_cast<uint32_t>(std::strlen(name)) * 0x100 + static_cast<uint8_t>(name[16]));
             r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kFree + 0x2C0));
         }},
        {0x0046CC30, 4, 1, Ret::Int, true, 0, {Ours(&Script::CArray::GetElement)},
         [](Frame& f, Result& r) { r.eax = 0xC0DE0000u + f.args[0]; }},
        {0x00479020, 0, 1, Ret::Int, false, 0,
         {Ours(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kParamsArea)); }},
        {0x004766D0, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(&Script::CStruct::GetFloat))},
         [](Frame& f, Result& r) {
             // (the first five are locals the original leaves unset when missing: always given)
             static const uint32_t always[5] = {0x94DF1603, 0x52E6C41B, 0x0B96AE2D, 0xE3C07609, 0x012F6992};
             bool must = false;
             for (uint32_t n : always)
                 must = must || f.args[0] == n;
             r.eax = must || f.args[0] == 0x748743A7 || !g_missing || Next() % 2;
             if (r.eax)
                 *reinterpret_cast<float*>(f.args[1]) = RandomFloat(30.0f);
         }},
        {0x00476C60, 12, 3, Ret::Bool, true, 4,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(&Script::CStruct::GetString))}},
        {0x00485B20, 4, 1, Ret::None, true, 0, {Ours(&Gfx::Camera::SetHFOV)}},
    };
}

void TestSkaterCameraModes()
{
    StandIns standIns(CameraStandIns());
    bool same = true;
    for (int variant = 0; variant < 6000 && same; ++variant) {
        g_rng = 0x7A3C9D11u * (variant + 1);
        FillArena();
        auto* cam = reinterpret_cast<CSkaterCameraModes*>(ArenaPtr(kCam));
        ArenaAt<uint8_t*>(kCam + 0x0C) = ArenaPtr(kObject);
        ArenaAt<uint8_t*>(kCam + 0x168) = ArenaPtr(kLookaround);
        ArenaAt<uint8_t*>(kCam + 0x16C) = ArenaPtr(kFree + 0x2E0);
        ArenaAt<uint8_t*>(kCameraComp + 0x18) = ArenaPtr(kFree + 0x300);
        for (uint32_t t : {0xC8u, 0xD4u})
            ArenaAt<float>(kCam + t) = Next() % 3 ? RandomFloat(0.05f) : (Next() % 2 ? 0.0f : 0.01666f);
        g_missing = Next() % 2;
        int op = static_cast<int>(Next() % 5);
        int mode = static_cast<int>(Next() % 15);
        float time = Next() % 3 ? RandomFloat(2.0f) : 0.0f;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto runOp = [&](bool o) -> uint32_t {
            switch (op) {
            case 0:
                o ? Original<void(__thiscall*)(void*)>(0x004F99F0)(cam) : cam->DestroyBody();
                return 0;
            case 1:
                o ? Original<void(__thiscall*)(void*)>(0x004F9A20)(cam) : cam->Finalize();
                return 1;
            case 2: {
                auto* state = reinterpret_cast<const Obj::WalkCameraStateView*>(ArenaPtr(kState));
                o ? Original<void(__thiscall*)(void*, const void*)>(0x004F9A40)(cam, state) : cam->SetState(state);
                return 2;
            }
            case 3:
                for (int i = 0; i < 4; ++i)
                    o ? Original<void(__thiscall*)(void*)>(0x004F9970)(cam) : cam->UpdateLerps();
                return 3;
            default:
                o ? Original<void(__thiscall*)(void*, int, float)>(0x004F9AE0)(cam, mode, time) : cam->SetMode(mode, time);
                return 4;
            }
        };
        auto expected = RunOnArena(seed, [&](uint8_t*) { return runOp(true); });
        auto actual = RunOnArena(seed, [&](uint8_t*) { return runOp(false); });
        same = Compare(expected, actual, "skater camera modes", variant);
        if (!same)
            Log("  (op %d mode %d)", op, mode);
    }
    Check(same, "skater: SkaterCamera's destructor body, Finalize, SetState, UpdateLerps and SetMode");
}

} // namespace

SELFTEST(Early, TestSkaterCameraModes);
