// Self-tests for the model component's display turns, display matrix and smaller members
// (src/game/obj/model_component.cpp): the original and ours on the same arena, every call into the rest of the game
// stood in for and recorded (the model and its lights, the feeler, the network, the clocks, the rotation matrices).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/mth/matrix.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/script/names.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;
using Obj::CModelComponent;

constexpr uint32_t kComponent = 0x0000, kObject = 0x0300, kModel = 0x0500, kLights = 0x0600, kGeom = 0x0700,
                   kFeeler = 0x0800, kPlayer = 0x0900, kApp = 0x0A00, kSkeletonComponent = 0x0B00, kSkeleton = 0x0C00,
                   kLine = 0x0D00, kVolumes = 0x0E00;

struct Answers {
    int clock;          // GameClockElapsed
    uint32_t time;      // Tmr::GetTime
    bool havePlayer, localPlayer, hit, haveLights, haveVolumes, replaced;
    double hitValue;
};
Answers g_a;
void* g_geomVtable[16];
void* g_componentVtable[16];

double __fastcall StandInHitValue(void* feeler, void*)
{
    Record({0x74A0, Rel(reinterpret_cast<uint32_t>(feeler))});
    return g_a.hitValue;
}

// A rotation matrix stand-in: recorded, filled from the angle.
Mth::Matrix& StandInRotate(uint32_t tag, Mth::Matrix& m, float angle)
{
    Record({tag, Rel(reinterpret_cast<uint32_t>(&m)), Bits(angle)});
    for (int i = 0; i < 16; ++i)
        (&m.row[0].x)[i] = (i % 5 == 0 ? 1.0f : 0.0f) + angle * static_cast<float>(i - 7) * 0.125f;
    return m;
}
Mth::Matrix& StandInRotateX(Mth::Matrix& m, float angle) { return StandInRotate(0x4DD0, m, angle); }
Mth::Matrix& StandInRotateY(Mth::Matrix& m, float angle) { return StandInRotate(0x4E20, m, angle); }
Mth::Matrix& StandInRotateZ(Mth::Matrix& m, float angle) { return StandInRotate(0x4E70, m, angle); }

void StandIn(StandIns& s)
{
    Rec(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, 0xECE0,
        [](Regs&) { return static_cast<uint32_t>(g_a.clock); }, false);
    Rec(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0, 0xE970, [](Regs&) { return g_a.time; }, false);
    // The model.
    Rec(s, {0x00494B70}, 1, 0x4B70);
    Rec(s, {0x00494800}, 2, 0x4800);
    Rec(s, {0x00493E80}, 3, 0x3E80, [](Regs&) { return g_a.replaced ? 0x12345601u : 0x12345600u; });
    Rec(s, {0x00494640}, 1, 0x4640, [](Regs&) { return Addr(kGeom); });
    Rec(s, {0x00492FC0}, 0, 0x2FC0, [](Regs&) { return g_a.haveVolumes ? Addr(kVolumes) : 0u; });
    Rec(s, {0x004939E0}, 7, 0x39E0, [](Regs& r) {
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[2]);
        for (int i = 0; i < 16; ++i)
            g_trace.push_back(m[i]);
        return 0x7E57u;
    });
    g_geomVtable[6] = RecThunk(s, 1, 0x6E06);
    // Its lights.
    Rec(s, {0x0048E660}, 0, 0xE660);
    Rec(s, {0x0048E8A0}, 1, 0xE8A0, [](Regs&) { return 1u; });
    Rec(s, {0x0048E830}, 2, 0xE830, [](Regs&) { return 1u; });
    Rec(s, {0x0048EDB0}, 1, 0xEDB0, [](Regs&) { return 1u; }, false, true);
    // The feeler.
    Rec(s, {0x00417120, AddressOf(&Nx::CFeeler::IsHitTriangleCollision)}, 0, 0x7120,
        [](Regs&) { return g_a.hit ? 1u : 0u; });
    s.Bytes(0x004174A0, Branch(0xE9, 0x004174A0, reinterpret_cast<void*>(&StandInHitValue)));
    uint32_t oursHit = AddressOf(&Nx::CFeeler::GetHitVertexValue);
    s.Bytes(oursHit, Branch(0xE9, oursHit, reinterpret_cast<void*>(&StandInHitValue)));
    // The network.
    Rec(s, {0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 1, 0xAEA0, [](Regs&) { return g_a.havePlayer ? Addr(kPlayer) : 0u; });
    Rec(s, {0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, 0x3300, [](Regs&) { return g_a.localPlayer ? 1u : 0u; });
    Rec(s, {0x005434B0, AddressOf(&GameNet::PlayerInfo::GetSkaterValue2BC)}, 0, 0x34B0, [](Regs&) { return 3u; });
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
        // (HideGeom's last two bytes and RotateDisplay's last one are never written: not compared)
        int compared = length == 8 ? 6 : length == 0x10 ? 15 : length;
        for (int i = 0; i < compared; ++i)
            Record({data[i]});
        return 0u;
    });
    // Debug output.
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, HashBytes(reinterpret_cast<const uint8_t*>(format), std::strlen(format)), r.args[1]});
        return 0u;
    });
    Rec(s, {0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 1, 0xD110,
        [](Regs&) { return 0x00646174u; }, false, true);
    // The rotation matrices (the original's sines and ours aren't compared here).
    for (auto [original, fn] : {std::pair<uint32_t, Mth::Matrix& (*)(Mth::Matrix&, float)>{0x00404DD0, StandInRotateX},
                                {0x00404E20, StandInRotateY},
                                {0x00404E70, StandInRotateZ}}) {
        s.Bytes(original, Branch(0xE9, original, reinterpret_cast<void*>(fn)));
    }
    uint32_t oursX = AddressOf(&Mth::CreateRotateXMatrix), oursY = AddressOf(&Mth::CreateRotateYMatrix),
             oursZ = AddressOf(&Mth::CreateRotateZMatrix);
    s.Bytes(oursX, Branch(0xE9, oursX, reinterpret_cast<void*>(&StandInRotateX)));
    s.Bytes(oursY, Branch(0xE9, oursY, reinterpret_cast<void*>(&StandInRotateY)));
    s.Bytes(oursZ, Branch(0xE9, oursZ, reinterpret_cast<void*>(&StandInRotateZ)));
    // The component's vtable slot 13 (InitModelFromProfile).
    g_componentVtable[13] = RecThunk(s, 4, 0x0D13);
}

constexpr uint32_t kModelAddresses[] = {0x00420590, 0x004204F0, 0x00420520, 0x004213E0, 0x004215F0, 0x00421960,
                                        0x00420070, 0x00420090, 0x004200C0, 0x004200E0, 0x00420100, 0x00420110,
                                        0x004202A0, 0x00420670, 0x00420700, 0x00420730, 0x004208D0, 0x00420250};

// The arena for one run: the component, its object, model (its lights), geom, skeleton.
void SetUpWorld(uint32_t seed, Lcg& random)
{
    FillArena(seed);
    auto* c = reinterpret_cast<CModelComponent*>(At(kComponent));
    c->m_vtable = g_componentVtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(At(kObject));
    c->mp_model = random.Below(6) == 0 ? nullptr : reinterpret_cast<Nx::CModel*>(At(kModel));
    c->mp_skeleton = reinterpret_cast<Obj::CBaseComponent*>(At(kSkeletonComponent));
    F<uint32_t>(kSkeletonComponent + 0x18) = Addr(kSkeleton);
    F<uint32_t>(kModel + 0x2C) = g_a.haveLights ? Addr(kLights) : 0u;
    F<void**>(kGeom) = g_geomVtable;
    F<uint32_t>(kObject + 0x14) = 0x2300 + seed; // (its id)
    c->m_colorValueCount = static_cast<int>(random.Below(6)) - 1;
    c->m_flip = random.Below(2) != 0;
    c->m_sectorModel = random.Below(2) != 0;
    // The spins: random flags, periods (some NaN), powers -3 to 4, starts around now.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    bool noneActive = seed % 5 == 0; // (no turn: the display matrix unturned)
    for (auto& axis : c->m_spin) {
        axis.m_active = !noneActive && random.Below(4) != 0;
        bool allOff = random.Below(4) == 0; // (an axis with no turn on: inactive after its angle)
        for (auto& spin : axis.m_spins) {
            spin.m_flags = static_cast<uint8_t>(random.Below(8) == 0 ? 0 : random.Next());
            if (allOff)
                spin.m_flags &= ~Obj::MODEL_SPIN_ON;
            spin.m_period = random.Below(13) == 0 ? nan
                                                  : static_cast<float>(random.Below(3000)) +
                                                        (random.Below(3) == 0 ? 0.0f : 0.5f);
            spin.m_start = 1000u + random.Below(4000) - (random.Below(9) == 0 ? 0x90000000u : 0u);
            if (random.Below(4) == 0 && spin.m_period == spin.m_period) {
                // (exactly at its end)
                uint32_t now = spin.m_flags & Obj::MODEL_SPIN_REAL_TIME
                                   ? g_a.time
                                   : static_cast<uint32_t>(g_a.clock + GameVariable<int>(0x00696D60));
                spin.m_start = now - static_cast<uint32_t>(spin.m_period);
            }
            spin.m_power = static_cast<int>(random.Below(8)) - 3;
            spin.m_base = static_cast<float>(static_cast<int>(random.Below(721)) - 360);
            spin.m_range = static_cast<float>(static_cast<int>(random.Below(721)) - 360) * 0.75f;
        }
    }
    static const float pivots[][4] = {{0, 0, 0, 1}, {0, 30, 0, 1}, {2.5f, 0, 0, 7}, {0, 0, -4, 1}};
    int p = static_cast<int>(random.Below(5));
    if (p < 4)
        c->m_pivot = {pivots[p][0], pivots[p][1], pivots[p][2], pivots[p][3]};
    else
        c->m_pivot = {0.0f, nan, 0.0f, 1.0f};
}

void TestModelComponentMembers()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    StandInStructGetters(s);
    GameMemory memory({{0x00696D60, 4}, {0x007CCE14, 4}});
    for (uint32_t seed = 1; seed <= 64; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_a = {};
            Lcg random(0x30DE0000 + seed);
            g_a.clock = static_cast<int>(random.Below(5000));
            g_a.time = 1000u + random.Below(6000);
            g_a.havePlayer = random.Below(4) != 0;
            g_a.localPlayer = random.Below(3) != 0;
            g_a.hit = random.Below(4) != 0;
            g_a.haveLights = random.Below(3) != 0;
            g_a.haveVolumes = random.Below(3) != 0;
            g_a.replaced = random.Below(2) != 0;
            g_a.hitValue = static_cast<double>(random.Below(1000)) / 997.0 + 1e-12;
            GameVariable<int>(0x00696D60) = static_cast<int>(random.Below(3000));
            GameVariable<uint32_t>(0x007CCE14) = 0x0E7AA000;
            SetUpWorld(seed, random);
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kModelAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CModelComponent*>(At(kComponent));
            auto call = [&](uint32_t address) { return Code(original, address); };
            // The spins: each one's angle, each axis's.
            for (auto& axis : c->m_spin) {
                for (auto& spin : axis.m_spins) {
                    // (left in ST0 unrounded: the original's x87 precision is finer than a double's, compared as the
                    // callers use it - rounded to float)
                    float angle = static_cast<float>(
                        reinterpret_cast<double(__thiscall*)(void*)>(call(0x00420590))(&spin));
                    trace.push_back(angle != angle ? 0x7FC00000u : Bits(angle));
                    trace.push_back(spin.m_flags);
                }
            }
            SetUpWorld(seed, random);
            for (auto& axis : c->m_spin) {
                float angle = reinterpret_cast<float(__thiscall*)(void*)>(call(0x004213E0))(&axis);
                trace.push_back(angle != angle ? 0x7FC00000u : Bits(angle));
                trace.push_back(axis.m_active);
            }
            // The display matrix (twice: the spins may have gone off), and a line against the model.
            SetUpWorld(seed, random);
            for (int k = 0; k < 2; ++k) {
                Mth::Matrix m;
                std::memset(&m, 0xCD, sizeof(m));
                reinterpret_cast<void(__thiscall*)(void*, void*)>(call(0x004215F0))(c, &m);
                for (int i = 0; i < 16; ++i) {
                    float f = (&m.row[0].x)[i];
                    trace.push_back(f != f ? 0x7FC00000u : Bits(f));
                }
                g_a.clock += 700;
            }
            if (random.Below(8) != 0) // (no skeleton component: the original crashes)
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, uint32_t, uint32_t, uint32_t,
                                                                       uint32_t)>(call(0x00421960))(
                    c, At(kLine), 0x1111, 0x2222, 0x3333, 0x4444));
            // Clear, Add (a full axis too).
            reinterpret_cast<void(__thiscall*)(void*)>(call(0x004204F0))(&c->m_spin[seed % 3]);
            trace.push_back(c->m_spin[seed % 3].m_active);
            for (auto& spin : c->m_spin[seed % 3].m_spins)
                trace.push_back(spin.m_flags);
            for (int k = 0; k < 8; ++k)
                reinterpret_cast<void(__thiscall*)(void*, float, uint32_t, float, float, int, bool, bool)>(
                    call(0x00420520))(&c->m_spin[(seed + k) % 3], 100.0f * static_cast<float>(k), 77u * k,
                                      static_cast<float>(k) - 3.5f, 1.25f * static_cast<float>(seed), k - 2,
                                      (k & 1) != 0, (k & 2) != 0);
            // The small members.
            reinterpret_cast<void(__thiscall*)(void*, bool)>(call(0x00420070))(c, (seed & 1) != 0);
            for (int index = -1; index <= 5; ++index)
                reinterpret_cast<void(__thiscall*)(void*, int, float)>(call(0x00420090))(c, index,
                                                                                         static_cast<float>(index) * 3.0f);
            if (c->mp_model)
                reinterpret_cast<void(__thiscall*)(void*)>(call(0x004200C0))(c);
            reinterpret_cast<void(__thiscall*)(void*, void*, uint32_t)>(call(0x004200E0))(c, At(0x0F00), 0xB0B0 + seed);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*)>(call(0x00420100))(c)));
            trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, const char*, const char*)>(
                                call(0x00420110))(c, 0xC4E78E22 + (seed & 1), "src", "dest") &
                            0xFF);
            trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, bool, bool)>(call(0x004202A0))(
                                c, 0xA70C0000 + seed, (seed & 2) != 0, (seed & 4) != 0) &
                            0xFF);
            if (c->mp_model) {
                reinterpret_cast<void(__thiscall*)(void*, void*, bool, float)>(call(0x00420670))(
                    c, At(kFeeler), (seed & 8) != 0, 0.37f * static_cast<float>(seed));
            }
            if (c->mp_model)
                reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(call(0x00420700))(c, 0x80706050 + seed);
            reinterpret_cast<void(__thiscall*)(void*, float, float, float, int, bool, bool, bool, bool)>(
                call(0x00420730))(c, 7.3f * static_cast<float>(seed), -45.5f + static_cast<float>(seed),
                                  1000.75f - 30.0f * static_cast<float>(seed), static_cast<int>(seed % 7) - 3,
                                  (seed & 1) != 0, (seed & 2) != 0, (seed & 4) != 0, (seed % 3) != 0);
            if (seed % 4 == 0)
                reinterpret_cast<void(__thiscall*)(void*)>(call(0x004208D0))(c);
            trace.push_back(static_cast<uint32_t>(reinterpret_cast<int(__cdecl*)(int)>(call(0x00420250))(
                static_cast<int>(seed * 1234u) - 30000)));
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (uint32_t i = 0; i < 0x1000; i += 4) {
                uint32_t word = F<uint32_t>(i);
                float f;
                std::memcpy(&f, &word, 4);
                trace.push_back(f != f ? 0x7FC00000u : Rel(word));
            }
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual,
                "model component: display turns, display matrix, line test, geoms, colours, lights, RotateDisplay");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestModelComponentMembers, RemoveStandIns);
