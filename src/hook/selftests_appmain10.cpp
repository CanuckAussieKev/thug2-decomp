// Self-tests for the Avoid and CameraLookAround components, the cameras' helpers, the component types' registry and
// the goal editor's helpers (appmain batch 10: src/game/obj/avoid.cpp, camera_lookaround.cpp, camera_util.cpp,
// component_types.cpp, src/game/ed/goal_editor_util.cpp): the original and ours in a stand-in world
// (appmain_components.h); every call and the memory compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/ed/goal_editor_util.h"
#include "game/obj/avoid.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/compositemgr.h"
#include "hook/appmain_components.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

std::vector<uint32_t> WorldWords()
{
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < offsetof(ComponentWorld, shadowVtable) / 4; ++i)
        words.push_back(WorldRelocate(reinterpret_cast<const uint32_t*>(&g_world)[i]));
    return words;
}

GameMemory* ComponentMemory()
{
    return new GameMemory({{0x007CE478, 4}, {0x007D1800, 4}, {0x007CC979, 1}, {0x007CCBE8, 0x58},
                           {0x007CEFA8, 4}, {0x007CE5D8, 4}, {0x007CCC58, 0x40}});
}

// The Avoid component: made (and failing to be), constructed, destroyed, its member functions registered; the
// distance per quarter (every quarter, the borders, negative angles, not a number); the side away from the skater
// (skater heading flat / zero / either side); the jump tried with every outcome of the probe, the skater too
// close, too far from the herd point; the jump animations per quarter (own / mirrored, no blender); the jump
// member function finding each of its four tries or none; the steps, the end, the stored points, the motion hold.
void TestAvoidComponent()
{
    std::unique_ptr<GameMemory> memory(ComponentMemory());
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    g_relocate = WorldRelocate;
    static const float kAngles[] = {0.0f, 0.7853981f, 0.7853982f, 1.0f, 2.3561945f, 3.0f, 3.9269907f, 5.0f,
                                    5.4977871f, 6.2831853f, -0.5f, -3.0f, 7.0f, 100.0f, -100.0f, 45.0f, 135.0f,
                                    225.0f, 315.0f, std::numeric_limits<float>::quiet_NaN()};
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0xA701D000 + seed);
            Lcg random(0xA7010000 + seed);
            ResetComponentWorld(random);
            Lcg answers(0xA7020000 + seed);
            g_answers = Answers();
            g_answers.getFloatNamed = [&answers](const char*, float* out) {
                if (answers.Below(4) == 0)
                    return false;
                *out = static_cast<float>(answers.Below(200)) - 20.0f;
                return true;
            };
            g_answers.getChecksum = [&answers](uint32_t name, uint32_t* out) {
                if (answers.Below(3) == 0)
                    return false;
                *out = name ^ 0x5A5A;
                return true;
            };
            g_answers.component = [&answers, seed](uint32_t type) -> Obj::CBaseComponent* {
                if (type == 0x7149B753)
                    return answers.Below(5) == 0 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_world.blender);
                if (type == 0x0A015E17)
                    return seed == 3 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_world.motion);
                return nullptr;
            };
            int pathCalls = 0; // (asked twice in a row, the same answer: the original reads it the second time)
            g_answers.motionPath = [&pathCalls]() -> uint8_t* {
                int k = pathCalls++;
                if ((k / 2) % 3 == 0) {
                    ++pathCalls; // (no second question after a null)
                    return nullptr;
                }
                return g_world.motionPath;
            };
            g_answers.collide = [&answers](uint8_t*) { return answers.Below(3) == 0; };
            g_answers.angleInPlane = [&answers](const Mth::Matrix*, const Mth::Vector* v) {
                return static_cast<float>(answers.Below(720)) - 360.0f + v->x * 0.0f;
            };
            g_answers.animComplete = [&answers] { return answers.Below(2) != 0; };
            g_answers.duration = 10.0f + static_cast<float>(seed);
            g_answers.frameSeconds = 1.0 / (30 + seed * 7);
            *reinterpret_cast<float*>(g_world.motionPath + 0x74) = 12.5f;
            *reinterpret_cast<uint32_t*>(g_world.motion + 0x1C) = random.Below(16);
            Originals guard(original, {0x004E39E0, 0x004E3660, 0x004E3330, 0x004E36C0, 0x004E34C0, 0x004E3A40,
                                       0x004E3340, 0x004E3BD0, 0x004E36F0, 0x004E3EC0, 0x004E37D0, 0x004E3810,
                                       0x004E3850, 0x004E38C0, 0x004E38F0, 0x004E3270, 0x004E3930, 0x004E3310});
            std::vector<uint32_t> trace;
            // Made, destroyed, registered.
            g_answers.callocFails = seed == 2;
            trace.push_back(WorldRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004E39E0)()));
            g_answers.callocFails = false;
            Fn<void(__cdecl*)()>(original, 0x004E3310)();
            auto* c = reinterpret_cast<Obj::CAvoidComponent*>(g_world.component);
            trace.push_back(WorldRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004E3660)(c)));
            c->m_object = WorldObject();
            // The distances and the probe line.
            auto* params = reinterpret_cast<Script::CStruct*>(0x5A5A0000);
            for (float angle : kAngles) {
                float d = CallFloatWithEsi(Code(original, 0x004E3270), angle, params);
                trace.push_back(Bits(d));
            }
            Mth::Vector a = {1, 2, 3, 1}, b = {4, 5, 6, 1};
            for (int i = 0; i < 4; ++i)
                trace.push_back(CallWithEdiEsi(Code(original, 0x004E3930), &a, &b));
            // The side away from the skater.
            for (int i = 0; i < 6; ++i) {
                auto* skater = reinterpret_cast<Obj::CCompositeObject*>(g_world.skater);
                if (i == 2)
                    skater->m_matrix.row[2] = {0.0f, 1.0f, 0.0f, 0.0f}; // (no horizontal heading)
                if (i == 3)
                    skater->m_matrix.row[2] = {-0.0f, 0.5f, 0.0f, 0.0f};
                if (i == 4)
                    skater->m_pos = WorldObject()->m_pos;
                trace.push_back(Bits(Fn<float(__thiscall*)(void*)>(original, 0x004E34C0)(c)));
                skater->m_matrix.row[2] = {static_cast<float>(i) - 2.5f, 0.1f, 1.5f - static_cast<float>(i), 0.0f};
            }
            // Jumps tried.
            c->m_herdPoint = {WorldObject()->m_pos.x + 20.0f, 0.0f, WorldObject()->m_pos.z - 10.0f, 1.0f};
            for (int i = 0; i < 10; ++i) {
                Mth::Matrix start = WorldObject()->m_displayMatrix, out;
                start.row[3] = WorldObject()->m_pos;
                std::memset(&out, 0x77, sizeof(out));
                float angle = kAngles[i * 2 % 20], distance = 10.0f + 9.0f * static_cast<float>(i);
                float maxDistance = i == 7 ? 1.0f : 100.0f + 10.0f * static_cast<float>(i);
                if (i == 5) // (the skater on the landing)
                    reinterpret_cast<Obj::CCompositeObject*>(g_world.skater)->m_pos = WorldObject()->m_pos;
                bool ok = Fn<bool(__thiscall*)(void*, const Mth::Matrix*, Mth::Matrix*, float, float, float)>(
                    original, 0x004E3A40)(c, &start, &out, angle, distance, maxDistance);
                trace.push_back(ok);
                trace.push_back(HashFloats(&out, 16));
            }
            // The jump animations.
            for (float angle : kAngles) {
                Fn<void(__thiscall*)(void*, float, void*)>(original, 0x004E3340)(c, angle, params);
                trace.push_back(Bits(c->m_duration));
            }
            // The jump member function, the steps, the end.
            for (int i = 0; i < 8; ++i) {
                trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E3BD0)(c, params, nullptr));
                for (int step = 0; step < 3; ++step) {
                    trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E3EC0)(c, params, nullptr));
                    trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E37D0)(c, params, nullptr));
                }
                c->m_progress = i == 3 ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(i) * 0.25f;
                trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E37D0)(c, params, nullptr));
            }
            // The points, the motion component's flag.
            for (int i = 0; i < 4; ++i) {
                trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E3810)(c, params, nullptr));
                trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E3850)(c, params, nullptr));
                if (seed != 3) {
                    *reinterpret_cast<uint32_t*>(g_world.motion + 0x1C) = random.Below(16);
                    trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E38C0)(c, params, nullptr));
                    trace.push_back(c->m_motionHeld);
                }
                *reinterpret_cast<uint32_t*>(g_world.motion + 0x1C) = random.Below(16);
                c->m_motionHeld = (i & 1) != 0;
                trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E38F0)(c, params, nullptr));
                trace.push_back(*reinterpret_cast<uint32_t*>(g_world.motion + 0x1C));
                trace.push_back(Fn<bool(__thiscall*)(void*, void*, void*)>(original, 0x004E36F0)(c, params, nullptr));
                WorldObject()->m_pos.x += 3.0f;
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004E3330)(c);
            c->m_vtable = reinterpret_cast<void**>(0x0064B220);
            trace.push_back(WorldRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned)>(original, 0x004E36C0)(c, seed & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            std::vector<uint32_t> world = WorldWords();
            trace.insert(trace.end(), world.begin(), world.end());
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the Avoid component");
    }
    g_relocate = nullptr;
    delete &s;
}

// CameraLookAround: made, destroyed, initialised, finalised, locked; the update for every pad state (both pads,
// the globals set / not, either object), the stick at rest / moved / within the dead band, while turning (time
// left, ending, none); TurnTo with and without a time; its member functions.
void TestCameraLookAround()
{
    std::unique_ptr<GameMemory> memory(ComponentMemory());
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    g_relocate = WorldRelocate;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0xC1A0D000 + seed);
            Lcg random(0xC1A00000 + seed);
            ResetComponentWorld(random);
            if (seed == 4)
                WorldObject()->m_id = 0x967C138C;
            Lcg answers(0xC1A10000 + seed);
            g_answers = Answers();
            g_answers.globalInteger = [&answers](uint32_t) { return static_cast<int>(answers.Below(2)); };
            g_answers.component = [](uint32_t type) {
                return type == 0x27D7CD28 ? reinterpret_cast<Obj::CBaseComponent*>(g_world.input) : nullptr;
            };
            g_answers.getFloat = [&answers](uint32_t name, float* out) {
                if (name == 0x48D4868B && answers.Below(2))
                    return false;
                *out = static_cast<float>(answers.Below(400)) / 100.0f - (name == 0x906B67BA ? 1.0f : 2.0f);
                return true;
            };
            g_answers.frameSeconds = 1.0 / (20 + 10 * seed);
            Originals guard(original, {0x004E42E0, 0x004E3F60, 0x004E4010, 0x004E3F70, 0x004E4040, 0x004E4000,
                                       0x004E3FA0, 0x004E4060, 0x004E4220});
            std::vector<uint32_t> trace;
            g_answers.callocFails = seed == 2;
            trace.push_back(WorldRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004E42E0)()));
            g_answers.callocFails = false;
            auto* c = reinterpret_cast<Obj::CCameraLookAroundComponent*>(g_world.component);
            std::memcpy(c, g_world.heap, sizeof(*c));
            c->m_object = WorldObject();
            Fn<void(__thiscall*)(void*, void*)>(original, 0x004E3F70)(c, nullptr);
            Fn<void(__thiscall*)(void*)>(original, 0x004E4040)(c);
            for (int frame = 0; frame < 40; ++frame) {
                uint8_t* pad = g_world.input + 0x20 + (frame & 1 ? 0x238 : 0);
                for (uint8_t* p : {g_world.input + 0x20, g_world.input + 0x258}) {
                    p[0x1F0] = random.Below(5) == 0;
                    p[0x130] = random.Below(6) == 0;
                    float x = static_cast<float>(random.Below(201)) / 100.0f - 1.0f;
                    float y = static_cast<float>(random.Below(201)) / 100.0f - 1.0f;
                    if (random.Below(4) == 0)
                        x = 0.0f;
                    *reinterpret_cast<float*>(p + 0x214) = x;
                    *reinterpret_cast<float*>(p + 0x210) = y;
                }
                (void)pad;
                if (frame % 9 == 3 || frame == 12) // (12: exactly one frame's time)
                    Fn<void(__thiscall*)(void*, float, float, float, float)>(original, 0x004E3FA0)(
                        c, 1.0f + frame * 0.1f, -0.5f,
                        frame == 12 ? static_cast<float>(g_answers.frameSeconds) : (frame % 2 ? 0.0f : 0.3f), 0.75f);
                if (frame % 13 == 7)
                    trace.push_back(static_cast<uint32_t>(Fn<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                        original, 0x004E4220)(c, 0xB8F23899, reinterpret_cast<void*>(0x5A5A0000), nullptr)));
                if (frame % 17 == 11)
                    trace.push_back(static_cast<uint32_t>(Fn<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                        original, 0x004E4220)(c, frame % 2 ? 0xE3B327C0 : 0x12345678, nullptr, nullptr)));
                if (frame == 20)
                    Fn<void(__thiscall*)(void*, bool)>(original, 0x004E4000)(c, true);
                if (frame == 25)
                    Fn<void(__thiscall*)(void*, bool)>(original, 0x004E4000)(c, false);
                Fn<void(__thiscall*)(void*)>(original, 0x004E4060)(c);
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(c), sizeof(*c)));
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004E3F60)(c);
            trace.push_back(WorldRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned)>(original, 0x004E4010)(c, seed & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            std::vector<uint32_t> world = WorldWords();
            trace.insert(trace.end(), world.begin(), world.end());
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the CameraLookAround component");
    }
    g_relocate = nullptr;
    delete &s;
}

// The frame blend for rates and times of every kind; the camera's collision avoidance with each probe hitting or
// not (the hit near / far), sideways or not, refocus or not, a camera on its target.
void TestCameraUtil()
{
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    g_relocate = WorldRelocate;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0xCA700000 + seed);
            Lcg answers(0xCA710000 + seed);
            g_answers = Answers();
            g_answers.collide = [&answers](uint8_t* feeler) {
                if (answers.Below(2) == 0)
                    return false;
                *reinterpret_cast<float*>(feeler + 0x74) = static_cast<float>(answers.Below(1001)) / 1000.0f;
                for (int i = 0; i < 4; ++i)
                    reinterpret_cast<float*>(feeler + 0x78)[i] = static_cast<float>(answers.Below(2000)) / 10.0f - 100.0f;
                return true;
            };
            Originals guard(original, {0x004E4340, 0x004E4360});
            std::vector<uint32_t> trace;
            for (int i = 0; i < 30; ++i) {
                float rate = static_cast<float>(random.Below(1001)) / 1000.0f, time = static_cast<float>(random.Below(100)) / 1000.0f;
                if (i == 0)
                    rate = 1.0f, time = 0.0f;
                double blend = Fn<double(__cdecl*)(float, float)>(original, 0x004E4340)(rate, time);
                trace.push_back(Bits(static_cast<float>(blend)));
                uint64_t bits = std::bit_cast<uint64_t>(blend);
                trace.push_back(static_cast<uint32_t>(bits >> 20));
            }
            for (int i = 0; i < 24; ++i) {
                auto v = [&random](float range) {
                    return Mth::Vector{static_cast<float>(random.Below(20001)) / 10000.0f * range - range,
                                       static_cast<float>(random.Below(20001)) / 10000.0f * range - range,
                                       static_cast<float>(random.Below(20001)) / 10000.0f * range - range, 1.0f};
                };
                Mth::Vector position = v(200), target = v(200), focus = v(200);
                if (i == 3)
                    position = target;
                Mth::Matrix matrix;
                for (Mth::Vector& row : matrix.row)
                    row = v(1);
                Fn<void(__cdecl*)(Mth::Vector*, Mth::Matrix*, const Mth::Vector*, const Mth::Vector*, bool, bool)>(
                    original, 0x004E4360)(&position, &matrix, &target, &focus, (i & 1) != 0, (i & 2) != 0);
                trace.push_back(HashFloats(&position, 4));
                trace.push_back(HashFloats(&matrix, 16));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the cameras' blend and collision avoidance");
    }
    g_relocate = nullptr;
    delete &s;
}

// The registry: every type registered with its functions, in order (a stand-in manager's table compared).
void TestComponentTypes()
{
    GameMemory memory({{0x006F0FB8, 4}});
    StandIns& s = *new StandIns();
    s.At({0x0045F270, AddressOf(&Obj::CCompositeObjectManager::RegisterComponent)}, 12, [](Regs& r) {
        Record({0xF270, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    auto run = [&](bool original) {
        g_trace.clear();
        GameVariable<uint32_t>(0x006F0FB8) = 0x3A3A0000;
        Fn<void(__cdecl*)()>(original, 0x004E4860)();
        return g_trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "appmain: the component types registered");
    delete &s;
}

// The goal editor's helpers: every type and index (also out of range), node names for each kind, the type
// structures (every type and others), an edited goal's defaults, the marker object's script.
void TestGoalEditorUtil()
{
    GameMemory memory({{0x007CCC58, 0x40}});
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 8);
        return r.ecx;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
         8, [](Regs& r) { Record({0x7D40, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs&) { Record({0x7090}); return 0u; });
    g_relocate = [](uint32_t word) { return OnStack(word) ? 0x57AC0000u : word; };
    static const uint32_t kTypes[] = {0x54166ACD, 0x4EC3CFB5, 0x25904450, 0x0FC6F698, 0x61C5D092, 0x6FE44C6D,
                                      0x7FE2238A, 0xD0DD047B, 0xEA7BA666, 0xEC414B76, 0, 0x12345678};
    auto run = [&](bool original) {
        g_trace.clear();
        g_answers = Answers();
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004E6600, 0x004E66A0, 0x004E6730, 0x004E67A0, 0x004E6890, 0x004E69C0,
                                   0x004E69D0, 0x004E6AE0});
        for (uint32_t type : kTypes) {
            for (int index = -1; index <= 6; ++index) {
                trace.push_back(Hash(Fn<const char*(__fastcall*)(uint32_t, int)>(original, 0x004E6600)(type, index)));
                trace.push_back(Fn<uint32_t(__fastcall*)(uint32_t, int)>(original, 0x004E66A0)(type, index));
            }
            trace.push_back(Fn<uint32_t(__fastcall*)(uint32_t)>(original, 0x004E67A0)(type));
            trace.push_back(CallWithEax(Code(original, 0x004E6890), type));
        }
        for (int which = 0; which < 6; ++which)
            trace.push_back(Fn<uint32_t(__fastcall*)(int, int)>(original, 0x004E6730)(which, which * 3 + 1));
        trace.push_back(static_cast<uint32_t>(Fn<int(__cdecl*)()>(original, 0x004E69C0)()));
        alignas(16) static uint8_t goal[0x320];
        std::memset(goal, 0xCD, sizeof(goal));
        Fn<void(__fastcall*)(uint8_t*)>(original, 0x004E69D0)(goal);
        trace.push_back(HashBytes(goal, sizeof(goal)));
        for (int index : {0, 7, 123})
            Fn<void(__stdcall*)(int)>(original, 0x004E6AE0)(index);
        trace.push_back(HashBytes(&GameVariable<uint8_t>(0x007CCC58), 0x40));
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "appmain: the goal editor's helpers");
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestAvoidComponent, RemoveStandIns);
SELFTEST(Early, TestCameraLookAround, RemoveStandIns);
SELFTEST(Early, TestCameraUtil, RemoveStandIns);
SELFTEST(Early, TestComponentTypes, RemoveStandIns);
SELFTEST(Early, TestGoalEditorUtil, RemoveStandIns);
