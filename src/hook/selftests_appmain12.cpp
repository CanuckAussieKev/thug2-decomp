// Self-test for the FlipRotate animation controller's pose (appmain: 0x004E2D20 in src/game/obj/flip_rotate.cpp):
// the original and ours on random poses with stand-in skeleton queries and quaternion maths.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/quat.h"
#include "game/obj/flip_rotate.h"
#include "game/obj/object.h"
#include "game/obj/vehicle_component.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

// The pose turned (a bone's rotation) and mirrored, for: the turn component missing / off / on, the bone found or
// not, flipped or not, bone counts 0 / 3 / 64, mirror bones none / themselves / others (also past the count).
void TestFlipRotatePose()
{
    StandIns& s = *new StandIns();
    alignas(16) static uint8_t pose[0x800];
    alignas(16) static uint8_t skeleton[0x20];
    alignas(16) static uint8_t turner[0x20];
    static uint32_t seedNow = 0;
    static int count = 0;
    s.At({0x00481770}, 0, [](Regs&) { Record({0x1770}); return reinterpret_cast<uint32_t>(skeleton); });
    s.At({0x00481760}, 0, [](Regs&) { Record({0x1760}); return 0x0B1E0000u; });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        return seedNow % 3 == 0 ? 0u : reinterpret_cast<uint32_t>(turner);
    });
    s.At({0x004A2B60}, 4, [](Regs& r) {
        Record({0x2B60, r.ecx == reinterpret_cast<uint32_t>(skeleton), r.args[0]});
        return seedNow % 4 == 1 ? 0xFFFFFFFFu : (seedNow == 8 ? 0u : seedNow % 7); // (8: bone 0)
    });
    s.At({0x004354C0, AddressOf(Obj::QuatProduct)}, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0x54C0, HashBytes(reinterpret_cast<const uint8_t*>(a), 16), HashBytes(reinterpret_cast<const uint8_t*>(b), 16)});
        for (int i = 0; i < 4; ++i)
            out[i] = a[i] * 0.5f + b[3 - i];
        return r.args[0];
    });
    s.At({0x0042E310, AddressOf(&Mth::Quat::Normalize)}, 4, [](Regs& r) {
        Record({0xE310, r.args[0]});
        reinterpret_cast<float*>(r.ecx)[1] += 1.0f;
        return r.ecx;
    });
    s.At({0x004A2BA0}, 0, [](Regs&) { Record({0x2BA0}); return static_cast<uint32_t>(count); });
    s.At({0x004A2C60}, 4, [](Regs& r) {
        Record({0x2C60, r.ecx, r.args[0]});
        uint32_t i = r.args[0];
        switch ((i + seedNow) % 4) {
        case 0: return 0xFFFFFFFFu;
        case 1: return i;
        case 2: return (i * 5 + 1) % static_cast<uint32_t>(count);
        default: return (i + 1) % static_cast<uint32_t>(count);
        }
    });
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            seedNow = seed;
            count = seed % 3 == 0 ? 0 : (seed % 3 == 1 ? 3 : 64);
            Lcg random(0xF11F0000 + seed);
            for (uint32_t i = 0; i < sizeof(pose) / 4; ++i)
                reinterpret_cast<float*>(pose)[i] = static_cast<float>(static_cast<int>(random.Below(2001)) - 1000) / 77.0f;
            std::memset(skeleton, 0, sizeof(skeleton));
            *reinterpret_cast<uint32_t*>(skeleton + 8) = 0x5BE00000;
            std::memset(turner, 0, sizeof(turner));
            turner[0x18] = seed % 5 != 2;
            alignas(16) uint8_t controller[0x18] = {};
            reinterpret_cast<Obj::CFlipRotateComponent*>(controller)->m_flipped = (seed & 1) != 0 || seed == 4;
            Originals guard(original, {0x004E2D20});
            bool result = reinterpret_cast<bool(__thiscall*)(void*, uint8_t*)>(Code(original, 0x004E2D20))(controller, pose);
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(result);
            trace.push_back(HashBytes(pose, sizeof(pose)));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the FlipRotate controller's pose");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestFlipRotatePose, RemoveStandIns);
