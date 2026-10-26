// Self-tests for the skater's matrix queries component (src/game/skater/matrix_queries.cpp, 0x00513FD0-0x005144A1):
// the original and ours on the same stand-in skater, every call they make recorded.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/matrix_queries.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0100, kCore = 0x0300, kScript = 0x0500, kParams = 0x0700,
                   kResults = 0x0800;

struct Answers {
    uint32_t calloc;
    float acos, angle;
    uint32_t beyond; // IsAngleBeyond's answers, a bit each call
    int beyondCalls;
};
Answers g_a;

void StandIn(StandIns& s)
{
    StandInComponentBase(s, &g_a.calloc);
    StandInStructGetters(s);
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs&) { return Addr(kCore); });
    Rec(s, {0x00404090, AddressOf(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(
                            &Mth::Matrix::operator=))},
        1, 0x4090, [](Regs& r) {
            g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 64));
            std::memmove(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 64);
            return r.ecx;
        });
    RecFloat(s, {0x00403EF0, AddressOf(&Mth::FastACos)}, 1, 0x3EF0, [](Regs&) { return g_a.acos; }, false, true);
    RecFloat(s, {0x00406460, AddressOf(&Mth::GetAngle)}, 2, 0x6460, [](Regs& r) {
        const auto* a = reinterpret_cast<const float*>(r.args[0]);
        const auto* b = reinterpret_cast<const float*>(r.args[1]);
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(a), 16));
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(b), 16));
        return g_a.angle;
    }, false, true);
    Rec(s, {0x004060F0, AddressOf(&Mth::Vector::RotateToPlane)}, 1, 0x60F0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.ecx);
        const auto* n = reinterpret_cast<const float*>(r.args[0]);
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(v), 16));
        g_trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(n), 16));
        v[0] = v[0] * 0.5f + n[2];
        v[2] = v[2] * 0.75f - n[0];
        return r.ecx;
    });
    s.At({0x00402550, AddressOf(&Mth::CrossProduct)}, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0x2550, Rel(r.args[1]), Rel(r.args[2]), HashBytes(reinterpret_cast<const uint8_t*>(a), 16),
                HashBytes(reinterpret_cast<const uint8_t*>(b), 16)});
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
        out[3] = 0.0f;
        return r.args[0];
    });
    s.At({0x00406560, AddressOf(&Mth::IsAngleBeyond)}, 0, [](Regs& r) {
        Record({0x6560, Rel(r.args[0]), Rel(r.args[1]), r.args[2],
                HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 16)});
        return (g_a.beyond >> (g_a.beyondCalls++ & 31)) & 1;
    });
    Rec(s, {0x0047AC00, AddressOf(&Script::CPair::Init)}, 0, 0xAC00, [](Regs& r) {
        reinterpret_cast<uint8_t*>(r.ecx)[0] = 0;
        return r.ecx;
    });
    s.At({0x00476DF0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CPair*, bool)>(
                          &Script::CStruct::GetPair))},
         12, [](Regs& r) {
             Record({0x6DF0, Rel(r.ecx), r.args[0], r.args[2] & 0xFF});
             auto* pair = reinterpret_cast<Script::CPair*>(r.args[1]);
             pair->x = Float(F<uint32_t>(kParams + 0x40));
             pair->y = Float(F<uint32_t>(kParams + 0x44));
             return 1u;
         });
    Rec(s, {0x00477C60, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))},
        2, 0x7C60);
}

// Made, destroyed, finalized, updated; each member function (angles and stand-in answers varied).
void TestSkaterMatrixQueries()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            FillArena(seed);
            g_a = {};
            g_structs.clear();
            Originals guard(original, {0x00514430, 0x00513FD0, 0x00513FE0, 0x00514010, 0x00514030, 0x00514040});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x00514430, 0x00513FD0, 0x00513FE0, kComponent, 0x60, g_a.calloc, trace);
            F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00514010))(c);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00514030))(c);
            F<uint32_t>(kScript + 0x14) = Addr(kResults);
            Lcg random(seed * 7);
            const uint32_t names[] = {0x8E7833BE, 0x5E269B2B, 0x97201739, 0xA0551543, 0xA745C080,
                                      0xC8C4D2F4, 0xD3313E92, 0xB992F3CC, 0x12345678};
            for (int k = 0; k < 90; ++k) {
                // A small mix: unit-ish rows and velocities of either sign, angles 0-180.
                for (uint32_t at : {0x18u, 0x1Cu, 0x20u, 0x28u, 0x2Cu, 0x30u, 0x38u, 0x3Cu, 0x40u})
                    F<float>(kComponent + at) = static_cast<float>(static_cast<int>(random.Below(17)) - 8) / 8.0f;
                for (uint32_t at : {0x6Cu, 0x8Cu, 0x90u, 0x94u, 0x98u, 0xACu, 0xB0u, 0xB4u})
                    F<float>(kObject + at) = static_cast<float>(static_cast<int>(random.Below(17)) - 8) / 8.0f;
                for (uint32_t at : {0x78u, 0x7Cu, 0x80u})
                    F<float>(kCore + at) = static_cast<float>(static_cast<int>(random.Below(17)) - 8) / 8.0f;
                SetComponent(At(kParams), 0, Bits(static_cast<float>(random.Below(181))));
                if (k % 11 == 4)
                    g_structs[Addr(kParams)].erase(0);
                F<uint32_t>(kParams + 0x40) = Bits(static_cast<float>(random.Below(90)));
                F<uint32_t>(kParams + 0x44) = Bits(static_cast<float>(90 + random.Below(90)));
                F<float>(kComponent + 0x58) = static_cast<float>(random.Below(50));
                g_a.acos = static_cast<float>(random.Below(315)) / 100.0f;
                g_a.angle = static_cast<float>(random.Below(90));
                g_a.beyond = random.Next();
                if (k % 18 == 1) { // (AbsolutePitchGreaterThan with the cosine exactly the up row's y)
                    SetComponent(At(kParams), 0, Bits(k % 36 == 1 ? 0.0f : 180.0f));
                    F<float>(kComponent + 0x2C) = k % 36 == 1 ? 1.0f : -1.0f;
                }
                trace.push_back(static_cast<uint32_t>(reinterpret_cast<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x00514040))(c, names[k % 9], At(kParams), At(kScript))));
                trace.push_back(F<uint32_t>(kComponent + 0x58));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater matrix queries: made, destroyed, updated, every query");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterMatrixQueries, RemoveStandIns);
