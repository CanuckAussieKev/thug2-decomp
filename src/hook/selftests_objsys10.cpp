// Self-tests for the objsys agent's batch OS-10: blending animation poses (src/game/gfx/anim_blend.cpp,
// 0x0047D420-0x0047DC31). The original and ours on the same random poses, weights and ids (the vector blend
// 0x0043D990 the game's in both), the timeline's debug info into a stood-in structure, the blender channel's
// pieces on random channels; every result and the memory compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/anim_blend.h"
#include "game/script/struct.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/objsys_testkit.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Gfx::CPose;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
// A float's bits, every NaN as one.
uint32_t FB(float f)
{
    return std::isnan(f) ? 0x7FC00000u : Bits(f);
}

constexpr int kPoses = 64;
CPose g_poses[kPoses];
CPose g_out[kPoses];
float g_weights[8];
uint32_t g_ids[8];

float RandomComponent(Lcg& random)
{
    return static_cast<float>(static_cast<int>(random.Below(2001)) - 1000) / 1000.0f;
}

void RandomPoses(Lcg& random, int count)
{
    for (int p = 0; p < count; ++p) {
        CPose& pose = g_poses[p];
        for (int k = 0; k < 64; ++k) {
            for (int i = 0; i < 4; ++i) {
                pose.rotations[k][i] = RandomComponent(random);
                pose.translations[k][i] = RandomComponent(random) * 50.0f;
            }
            pose.valid[k] = static_cast<uint8_t>(random.Below(3) != 0);
        }
    }
    std::memset(g_out, 0xEE, sizeof(g_out));
}

void RandomWeights(Lcg& random, uint32_t id)
{
    static const float kWeights[] = {0.0f, 0.0f, 0.25f, 0.5f, 1.0f, 0.75f, 0.000005f, 0.00001f, -0.25f, 0.1f};
    for (int i = 0; i < 8; ++i) {
        g_weights[i] = random.Below(16) == 0 ? std::nanf("") : random.Pick(kWeights);
        g_ids[i] = random.Below(3) == 0 ? id + 1 : id;
    }
    g_ids[0] = id; // (the search for a lower pose stops at the first only when its id matches)
}

void PushFloats(const float* p, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        g_trace.push_back(FB(p[i]));
}

void PushPoses(int count, const CPose* poses)
{
    for (int p = 0; p < count; ++p) {
        PushFloats(&poses[p].rotations[0][0], 64 * 4);
        PushFloats(&poses[p].translations[0][0], 64 * 4);
        g_trace.push_back(HashBytes(poses[p].valid, 64));
    }
}

void TestAnimBlend()
{
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x5FC00000 + seed);
            int count = 1 + static_cast<int>(random.Below(7));
            RandomPoses(random, kPoses);
            uint32_t id = 0x1D000000 + random.Below(4);
            RandomWeights(random, id);
            static const int kBones[] = {0, 1, 7, 33, 64, 3};
            int bones = random.Pick(kBones);
            int which = static_cast<int>(random.Below(5));
            if (which == 0) {
                // the rotation blend on its own
                static const float kT[] = {0.0f, 1.0f, 0.5f, 0.3125f, -0.5f, 1.5f};
                for (int n = 0; n < 16; ++n) {
                    float out[4], from[4], to[4];
                    for (int i = 0; i < 4; ++i) {
                        from[i] = RandomComponent(random);
                        to[i] = random.Below(8) == 0 ? -from[i] : RandomComponent(random);
                        out[i] = 99.0f;
                    }
                    if (random.Below(8) == 0)
                        std::memset(to, 0, sizeof(to));
                    if (random.Below(8) == 0) { // (at right angles: a dot product of exactly 0)
                        to[0] = -from[1];
                        to[1] = from[0];
                        to[2] = -from[3];
                        to[3] = from[2];
                    }
                    float* result = Fn<float*(__cdecl*)(float*, const float*, float*, float)>(original, 0x0047D470)(
                        out, from, to, random.Pick(kT));
                    g_trace.push_back(result == out);
                    PushFloats(out, 4);
                    PushFloats(to, 4);
                }
            } else {
                CPose* out = random.Below(3) == 0 ? g_out : g_poses;
                if (which <= 2)
                    Fn<void(__cdecl*)(CPose*, float*, int, CPose*, int)>(original, 0x0047D680)(g_poses, g_weights, bones,
                                                                                             out, count);
                else
                    Fn<void(__cdecl*)(CPose*, float*, const uint32_t*, int, CPose*, int, uint32_t)>(original,
                                                                                                  0x0047D8F0)(
                        g_poses, g_weights, g_ids, bones, out, count, id);
                PushFloats(g_weights, 8);
                PushPoses(count, g_poses);
                if (out == g_out)
                    PushPoses(bones < kPoses ? bones : kPoses, g_out);
            }
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "anim blend: poses");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
}
SELFTEST(Early, TestAnimBlend, ObjsysTests::CleanUp);

// --- the timeline's info and the blender channel ----------------------------------------------------------------

Lcg g_answers(1);
alignas(16) float g_value;
bool g_mustFind; // (GetValue reads what it finds unchecked)

void TestBlendChannel()
{
    StandIns& s = *new StandIns();
    s.At({0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
         8, [](Regs& r) {
             Record({0xADDC0000, r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))}, 8,
         [](Regs& r) {
             Record({0xADDF0000, r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x004776B0, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t)>(&Script::CStruct::RemoveComponent))},
         4, [](Regs& r) {
             Record({0x4E3C0000, r.ecx, r.args[0]});
             return 0u;
         });
    s.At({0x004813B0}, 12, [](Regs& r) {
        Record({0x4813B000, r.ecx});
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<float*>(r.args[i]) = static_cast<float>(g_answers.Below(1000)) / 8.0f;
        return 0u;
    });
    s.At({0x0048CF50}, 0, [](Regs& r) {
        uint32_t found = g_mustFind || g_answers.Below(3) ? reinterpret_cast<uint32_t>(&g_value) : 0;
        g_value = static_cast<float>(g_answers.Below(1000)) / 4.0f;
        Record({0x48CF5000, r.args[0], r.args[1], found});
        return found;
    });
    alignas(16) static uint8_t channel[0x80];
    alignas(16) static uint8_t owner[0x40];
    alignas(16) static uint32_t timeline[0x34 / 4];
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x5FD00000 + seed);
            g_answers = Lcg(0x5FDA0000 + seed);
            for (auto& b : channel)
                b = static_cast<uint8_t>(random.Next());
            *reinterpret_cast<uint32_t*>(channel + 0x68) = reinterpret_cast<uint32_t>(owner);
            std::memset(owner, 0, sizeof(owner));
            *reinterpret_cast<uint32_t*>(owner + 0x20) = random.Below(3) ? 0x05C00000 : 0;
            for (auto& w : timeline)
                w = random.Next();
            timeline[0x28 / 4] = random.Below(4);
            for (int step = 0; step < 8; ++step) {
                switch (random.Below(6)) {
                case 0:
                    Fn<void(__thiscall*)(void*, bool)>(original, 0x0047D420)(channel, random.Below(2) != 0);
                    break;
                case 1: {
                    static const float kPeriods[] = {0.0f, 1.5f, 0.25f};
                    float period = random.Below(8) == 0 ? std::nanf("") : random.Pick(kPeriods);
                    Fn<void(__thiscall*)(void*, uint32_t, float)>(original, 0x0047D430)(channel, random.Next(), period);
                    break;
                }
                case 2:
                    g_mustFind = true;
                    g_trace.push_back(FB(Fn<float(__thiscall*)(void*, uint32_t)>(original, 0x0047D620)(
                        channel, random.Below(0x40))));
                    g_mustFind = false;
                    break;
                case 3:
                    g_trace.push_back(
                        Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047D640)(channel, random.Below(0x40)) &
                        0xFF);
                    break;
                case 4:
                    Fn<void(__thiscall*)(void*)>(original, 0x0047D660)(channel);
                    break;
                default:
                    Fn<void(__stdcall*)(void*, uint32_t)>(original, 0x0047D580)(timeline, 0x05C10000);
                    break;
                }
            }
            for (int i = 0; i < 0x80; i += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(channel + i));
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "anim blend: the channel and timeline info");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestBlendChannel, ObjsysTests::CleanUp);

} // namespace
