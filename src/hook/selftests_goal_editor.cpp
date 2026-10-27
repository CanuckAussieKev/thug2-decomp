// Self-tests for the goal editor's edited goals (src/game/ed/edited_goal.cpp): the original and ours on the same
// goal records with every structure, script, node and C runtime call stood in for (goal_editor_testkit.h); the
// calls and the records compared.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "game/ed/goal_editor.h"
#include "game/skate/skate_module.h"
#include "hook/goal_editor_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalEditorTests;
using Ed::CEditedGoal;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

CEditedGoal g_goal;
uint8_t g_skate[0x380]; // a stand-in Mdl::Skate: its level at +0x378

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}
float FromBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void UseGoalRelocation()
{
    g_relocate = [](uint32_t word) {
        uint32_t base = reinterpret_cast<uint32_t>(&g_goal);
        if (word >= base && word < base + sizeof(g_goal))
            return 0x60A10000u + (word - base);
        return OnStack(word) ? 0x57AC0000u : word;
    };
}

void DumpGoal(std::vector<uint32_t>& trace)
{
    const auto* words = reinterpret_cast<const uint32_t*>(&g_goal);
    for (size_t i = 0; i < sizeof(g_goal) / 4; ++i)
        trace.push_back(words[i]);
}

// The gap list, the key combo sets, the type, the time limit: every case on many filled records.
void TestEditedGoalLists()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x007CE478, 4}, {0x007CCC58, 0x40}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    UseGoalRelocation();
    static const uint32_t kTypes[] = {0x54166ACD, 0x4EC3CFB5, 0x25904450, 0x0FC6F698, 0x61C5D092, 0x6FE44C6D,
                                      0x7FE2238A, 0xD0DD047B, 0xEA7BA666, 0xEC414B76, 0, 0x12345678};
    auto run = [&](bool original) {
        g_trace.clear();
        g_values.clear();
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004E6B80, 0x004E6BF0, 0x004E7390, 0x004E6C40, 0x004E6CD0, 0x004E6D50,
                                   0x004E6DA0, 0x004E6F70, 0x004E93C0});
        Lcg lcg(0x5EED);
        for (uint32_t round = 0; round < 60; ++round) {
            FillGoal(g_goal, 0x1000 + round);
            // Gaps: small numbers so some are found (high bits of the argument ignored).
            for (int k = 0; k < 4; ++k) {
                uint32_t gap = (lcg.Below(12) & 0xFF) | (lcg.Next() << 8);
                for (int i = 0; i < 10; ++i)
                    g_goal.m_gaps[i] = static_cast<uint8_t>((g_goal.m_gaps[i] % 12));
                switch (lcg.Below(3)) {
                case 0: Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E6B80)(&g_goal, gap); break;
                case 1: Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E6BF0)(&g_goal, gap); break;
                default: Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E7390)(&g_goal, gap); break;
                }
            }
            // Key combo sets.
            for (int i = 0; i < 10; ++i)
                g_goal.m_keyComboSets[i].set = static_cast<uint8_t>(i * 3 % 7);
            static const int kSpins[] = {0, 90, 180, 359, 360, 539, 540, 719, 1079, -181, -359, -900};
            for (int k = 0; k < 4; ++k) {
                int set = static_cast<int>(lcg.Below(9)) - 1;
                switch (lcg.Below(3)) {
                case 0:
                    if (g_goal.m_numKeyComboSets < 10)
                        Fn<void(__thiscall*)(CEditedGoal*, int, int, int, int)>(original, 0x004E6C40)(
                            &g_goal, set, static_cast<int>(lcg.Below(3)) + 0x100,
                            lcg.Pick(kSpins), 0x203 + k);
                    break;
                case 1: Fn<void(__thiscall*)(CEditedGoal*, int)>(original, 0x004E6CD0)(&g_goal, set); break;
                default:
                    trace.push_back(Rel(reinterpret_cast<uint32_t>(
                        Fn<Ed::KeyComboSet*(__thiscall*)(CEditedGoal*, int)>(original, 0x004E6D50)(&g_goal, set))));
                    break;
                }
            }
            // The type (with and without the type's structure and its values).
            g_typeStructure = round % 5 != 0;
            if (round % 3 == 0) {
                g_values[{reinterpret_cast<uint32_t>(Token(12)), 0xC51BF6E3}] = 77 + round;
                g_values[{reinterpret_cast<uint32_t>(Token(12)), 0xCD66C8AE}] = 999;
            }
            if (round % 4 != 1)
                g_values[{reinterpret_cast<uint32_t>(Token(12)), 0x4BC5229D}] = round % 3;
            Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E6DA0)(&g_goal, kTypes[round % 12]);
            g_values.clear();
            // The time limit.
            g_goal.m_type = kTypes[(round * 7) % 12];
            g_goal.m_timeToStopAddingTricks = static_cast<int>(lcg.Below(60));
            Fn<void(__thiscall*)(CEditedGoal*, int)>(original, 0x004E6F70)(&g_goal,
                                                                          static_cast<int>(lcg.Below(70)) - 3);
            DumpGoal(trace);
        }
        FillGoal(g_goal, 77);
        trace.push_back(Rel(reinterpret_cast<uint32_t>(
            Fn<CEditedGoal*(__thiscall*)(CEditedGoal*)>(original, 0x004E93C0)(&g_goal))));
        DumpGoal(trace);
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor: an edited goal's gaps, key combo sets, type and time");
    g_relocate = nullptr;
    g_typeStructure = 1;
    delete &s;
}

// The points: markers made, placed (allowed or not, in the park editor or not, the last one, the next one's start),
// the nearness test, the current point, the nodes' positions.
void TestEditedGoalPoints()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x007CE478, 4}, {0x007CCC58, 0x40}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    UseGoalRelocation();
    static const uint32_t kTypes[] = {0x54166ACD, 0x4EC3CFB5, 0x25904450, 0x0FC6F698, 0x61C5D092};
    auto run = [&](bool original) {
        g_trace.clear();
        g_values.clear();
        g_globalFloats = {{0x71B20276, 20.0f}, {0x3F6E9739, 6.5f}, {0x71EC75E9, 3.25f}};
        GameVariable<uint8_t*>(0x007CE478) = g_skate;
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004E7050, 0x004E7780, 0x004E7210, 0x004E7300, 0x004E77C0, 0x004E7960,
                                   0x004E79C0, 0x004E7A80});
        Lcg lcg(0xB0A7);
        for (uint32_t round = 0; round < 80; ++round) {
            FillGoal(g_goal, 0x2000 + round);
            g_goal.m_type = kTypes[round % 5];
            g_goal.m_goalIndex = static_cast<int>(round % 11);
            *reinterpret_cast<uint32_t*>(g_skate + 0x378) = round % 3 == 0 ? 0xE8B4B836 : 0x11111111;
            g_insidePark = round % 6 != 3;
            // Markers: every kind (and none).
            g_goal.m_points[round % 13].kind = static_cast<int>(round % 6);
            Fn<void(__thiscall*)(CEditedGoal*, int)>(original, 0x004E7050)(&g_goal, static_cast<int>(round % 13));
            if (round % 4 == 0)
                Fn<void(__thiscall*)(CEditedGoal*)>(original, 0x004E7780)(&g_goal);
            // Nearness: at points, near them, far, not a number.
            for (int k = 0; k < 3; ++k) {
                Mth::Vector at = g_goal.m_points[lcg.Below(8)].pos;
                at.x += static_cast<float>(lcg.Below(40)) - 20.0f;
                at.y += static_cast<float>(lcg.Below(30)) - 10.0f;
                if (k == 2 && round % 10 == 9)
                    at.z = std::numeric_limits<float>::quiet_NaN();
                trace.push_back(Fn<bool(__thiscall*)(CEditedGoal*, const Mth::Vector&)>(original, 0x004E7210)(
                    &g_goal, at));
                trace.push_back(Fn<bool(__thiscall*)(CEditedGoal*, const Mth::Vector&, float)>(original, 0x004E7300)(
                    &g_goal, at, static_cast<float>(lcg.Below(9))));
            }
            // Placing points until the goal is full (or a few tries).
            g_goal.m_maxPoints = static_cast<uint8_t>(2 + round % 6);
            g_goal.m_currentPoint = static_cast<uint8_t>(round % 3);
            g_goal.m_numPoints = static_cast<uint8_t>(round % 4);
            for (int k = 0; k < 8; ++k) {
                Mth::Vector at = {static_cast<float>(k * 37 + round), 5.0f, static_cast<float>(k * -29), 1.0f};
                trace.push_back(Fn<bool(__thiscall*)(CEditedGoal*, const Mth::Vector&, float, float)>(original,
                                                                                                     0x004E77C0)(
                    &g_goal, at, static_cast<float>(k), 0.5f * k));
                Mth::Vector pos;
                float height = 0, angle = 0;
                trace.push_back(Fn<bool(__thiscall*)(CEditedGoal*, Mth::Vector*, float*, float*)>(original,
                                                                                                0x004E7960)(
                    &g_goal, &pos, &height, &angle));
                trace.insert(trace.end(), {Bits(pos.x), Bits(pos.y), Bits(pos.z), Bits(pos.w), Bits(height),
                                           Bits(angle)});
            }
            // The nodes.
            g_goal.m_numPoints = static_cast<uint8_t>(round % 8);
            Fn<void(__thiscall*)(CEditedGoal*)>(original, 0x004E7A80)(&g_goal);
            Fn<void(__thiscall*)(CEditedGoal*, int, uint32_t)>(original, 0x004E79C0)(
                &g_goal, static_cast<int>(round % 13) - 1 + (round % 13 == 0 ? 1 : 0), 0x1234 + round);
            DumpGoal(trace);
        }
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor: an edited goal's points, markers and nodes");
    g_relocate = nullptr;
    g_insidePark = 1;
    g_globalFloats.clear();
    delete &s;
}

// The type's settings read from parameters: every type, with and without each value, the key combo sets' array.
void TestEditedGoalParams()
{
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    UseGoalRelocation();
    static const uint32_t kTypes[] = {0x54166ACD, 0x4EC3CFB5, 0x25904450, 0x0FC6F698, 0x61C5D092, 0x6FE44C6D,
                                      0x7FE2238A, 0xD0DD047B, 0xEA7BA666, 0xEC414B76, 0x12345678};
    static const uint32_t kNames[] = {0x1DAAE95B, 0xD9426372, 0x67DD90CA, 0xDF47B144, 0x730844A3, 0xD76C173E,
                                      0xCD66C8AE, 0x45A375C9, 0xBFD3A831, 0x01181B29, 0x076F65C8, 0xDED8540A,
                                      0xA6D96603, 0x89473DB7, 0xEDF5DB70, 0x0490F171};
    auto run = [&](bool original) {
        g_trace.clear();
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004E73F0, 0x004E74F0, 0x004E7590});
        uint32_t params = reinterpret_cast<uint32_t>(Token(0));
        for (uint32_t round = 0; round < 66; ++round) {
            FillGoal(g_goal, 0x3000 + round);
            g_goal.m_type = kTypes[round % 11];
            g_values.clear();
            g_elements.clear();
            for (size_t i = 0; i < sizeof(kNames) / 4; ++i) {
                if (((round / 11) + i) % 3 != 0)
                    g_values[{params, kNames[i]}] = 0x40000000u + static_cast<uint32_t>(i * 77 + round);
            }
            g_values[{params, 0x730844A3}] = round % 4;
            g_values[{params, 0xD76C173E}] = 1;
            g_values[{params, 0x0490F171}] = 2;
            if (round % 5 == 0) {
                g_values.erase({params, 0xD76C173E});
                g_values.erase({params, 0x0490F171});
                g_values.erase({params, 0x730844A3});
            }
            g_arrays[1].m_size = 3 + round % 7;
            g_arrays[2].m_size = round % 4;
            for (uint32_t i = 0; i < 10; ++i)
                g_elements[{reinterpret_cast<uint32_t>(&g_arrays[1]), i}] = 0x700 + i * 3 + round;
            for (uint32_t i = 0; i < 4; ++i) {
                uint32_t entry = reinterpret_cast<uint32_t>(Token(1 + i));
                g_elements[{reinterpret_cast<uint32_t>(&g_arrays[2]), i}] = entry;
                if ((i + round) % 2)
                    g_values[{entry, 0xEB7D6498}] = 5 + i;
                if ((i + round) % 3)
                    g_values[{entry, 0xEDF5DB70}] = static_cast<uint32_t>(360 * i + 90 - 1000 * (round % 2));
                if ((i + round) % 4)
                    g_values[{entry, 0xA4BEE6A1}] = 0x300 + i;
                if ((i + round) % 5 < 2)
                    g_values[{entry, 0x687E2C25}] = 1;
            }
            switch (round % 4) {
            case 0: Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E73F0)(&g_goal, params); break;
            case 1: Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E74F0)(&g_goal, params); break;
            default: Fn<void(__thiscall*)(CEditedGoal*, uint32_t)>(original, 0x004E7590)(&g_goal, params); break;
            }
            DumpGoal(trace);
        }
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor: an edited goal's settings from parameters");
    g_relocate = nullptr;
    g_values.clear();
    g_elements.clear();
    delete &s;
}

} // namespace

SELFTEST(Early, TestEditedGoalLists, RemoveStandIns);
SELFTEST(Early, TestEditedGoalPoints, RemoveStandIns);
SELFTEST(Early, TestEditedGoalParams, RemoveStandIns);
