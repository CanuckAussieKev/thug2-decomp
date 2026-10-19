// Self-tests for the editor camera component and its ground checks (appmain batch 11: src/game/obj/
// editor_camera.cpp): the original and ours in a stand-in world (appmain_components.h); every call and the memory
// compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <vector>

#include "game/obj/editor_camera.h"
#include "hook/appmain_components.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
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

// The flag search's scripts, one buffer per name (a search keeps reading its script while the ones it calls are
// fetched): a name and 0x42 with no name after it, then one or two calls - plain or the 0x42 form, after a short
// jump with or without the 0x39 after it or none - to the next name, a flagged one, itself, the excluded one or
// another unflagged one.
std::map<uint32_t, std::array<uint8_t, 0x40>> g_scripts;

uint8_t* BuildScript(uint32_t name, Lcg& answers)
{
    uint8_t* code = g_scripts[name].data();
    uint8_t* p = code;
    auto put4 = [&p](uint32_t v) {
        std::memcpy(p, &v, 4);
        p += 4;
    };
    *p++ = 0x01; // (an end of line the first skip passes)
    *p++ = 0x16;
    put4(0x77770000);
    *p++ = 0x42;
    *p++ = 0x17;
    put4(5);
    *p++ = 0x01;
    for (int call = 0, calls = 1 + static_cast<int>(answers.Below(2)); call < calls; ++call) {
        uint32_t target;
        switch (answers.Below(5)) {
        case 0: target = name + 1; break;
        case 1: target = 0xF1A60000 + answers.Below(4); break;
        case 2: target = name; break;
        case 3: target = 0x0FFF0000; break;
        default: target = 0xF1A60004 + answers.Below(8); break;
        }
        // (a short jump before the call: with the 0x39 after it, without, or none)
        static const uint8_t kJumps[] = {0x47, 0x02, 0x00, 0x39};
        size_t jump = answers.Below(3) == 0 ? 0 : 3 + answers.Below(2);
        std::memcpy(p, kJumps, jump);
        if (jump == 3)
            p[1] = 0x00;
        p += jump;
        *p++ = 0x16;
        if (answers.Below(2)) {
            put4(0x55550000);
            *p++ = 0x42;
            *p++ = 0x16;
        }
        put4(target);
        *p++ = 0x01;
    }
    *p++ = 0x24;
    return code;
}

// The component's life (made, constructed, initialised with each flag, finalised, hidden / shown, destroyed);
// its member functions; frames of the update for each controller type (pads at rest, sticks moved, in the dead
// band, the d-pad, the zoom and height buttons) with the feelers hitting or not at random (ceilings, ground,
// steep ground, walls, flagged objects), in and out of the park editor; the flag search over scripts calling
// scripts (loops, the 0x42 form, flagged and unflagged ones).
void TestEditorCamera()
{
    GameMemory memory({{0x007CE478, 4}, {0x007D1800, 4}, {0x007CC979, 1}, {0x007CCBE8, 0x58}, {0x007CEFA8, 4},
                       {0x007CE5D8, 4}, {0x006F3558, 4}, {0x0067EFD8, 0x14}});
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    g_relocate = WorldRelocate;
    alignas(16) static uint8_t platform[0x48];
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_scripts.clear();
            memory.Fill(0xEDCA0000 + seed);
            Lcg random(0xEDC00000 + seed);
            ResetComponentWorld(random);
            if (seed == 5)
                WorldObject()->m_id = 0x5B509AD3;
            *reinterpret_cast<uint32_t*>(g_world.skate + 0x378) = seed & 1 ? 0xE8B4B836 : 0;
            std::memset(platform, 0, sizeof(platform));
            *reinterpret_cast<uint32_t*>(platform + 0x40) = seed & 2 ? 0x7ECA21E5 : 0;
            GameVariable<uint8_t*>(0x007CE5D8) = platform;
            GameVariable<uint32_t>(0x006F3558) = 0x5C5C0000;
            for (uint32_t i = 0; i < 4; ++i)
                GameVariable<uint32_t>(0x0067EFD8 + 4 * i) = 0xF1A60000 + i;
            GameVariable<uint32_t>(0x0067EFE8) = 0x0FFF0000;
            Lcg answers(0xEDC10000 + seed);
            float placeGlobal = -1.0f;
            float* ground = nullptr;
            g_answers = Answers();
            g_answers.global = [&answers, &placeGlobal](uint32_t name) {
                switch (name) {
                case 0x71EC75E9: return placeGlobal >= 0.0f ? placeGlobal : static_cast<float>(answers.Below(60));
                case 0x7D16D113: return -0.4f;
                case 0x411BEE4A: return 1.2f;
                case 0xC7210318: return 24.0f;
                case 0xF230D521: return -36.0f;
                default: return static_cast<float>(answers.Below(200)) / 100.0f;
                }
            };
            g_answers.component = [](uint32_t type) {
                return type == 0x27D7CD28 ? reinterpret_cast<Obj::CBaseComponent*>(g_world.input) : nullptr;
            };
            g_answers.getFloat = [&answers](uint32_t name, float* out) {
                if (answers.Below(3) == 0)
                    return false;
                float low = 0.0f, high = 1500.0f;
                switch (name) {
                case 0x9CDF40CD: high = 30.0f; break;                  // min_height
                case 0x52EECB98: low = 20.0f, high = 150.0f; break;    // the zoom range
                case 0x53E2512C: low = 300.0f, high = 2500.0f; break;
                case 0xC48391A5: low = 30.0f, high = 900.0f; break;    // zoom
                case 0xE3C07609: low = -0.5f, high = 1.5f; break;      // tilt
                case 0xB8D629CA: high = answers.Below(2) ? 2.0f : 200.0f; break; // height
                }
                *out = low + (high - low) * static_cast<float>(answers.Below(1000)) / 999.0f;
                return true;
            };
            g_answers.getVector = [&answers](uint32_t, Script::VectorValue* out) {
                *out = {static_cast<float>(answers.Below(100)), static_cast<float>(answers.Below(100)) - 50.0f,
                        static_cast<float>(answers.Below(100)), 1.0f};
                return answers.Below(4) != 0;
            };
            g_answers.containsFlag = [&answers](uint32_t) { return answers.Below(2) != 0; };
            g_answers.collide = [&answers, &ground](uint8_t* feeler) {
                if (ground) { // (a directed PlaceCursor: the ground at `ground`, nothing else hit)
                    bool first = *ground == *ground; // (not a number once taken)
                    if (first) {
                        float* point = reinterpret_cast<float*>(feeler + 0x78);
                        const float* start = reinterpret_cast<const float*>(feeler);
                        point[0] = start[0], point[1] = *ground, point[2] = start[2], point[3] = 1.0f;
                        float* normal = reinterpret_cast<float*>(feeler + 0x88);
                        normal[0] = 0.0f, normal[1] = 0.9f, normal[2] = 0.0f, normal[3] = 0.0f;
                        feeler[0x6D] = 0;
                        *ground = std::numeric_limits<float>::quiet_NaN();
                    }
                    return first;
                }
                if (answers.Below(3) == 0)
                    return false;
                const float* start = reinterpret_cast<const float*>(feeler);
                float* point = reinterpret_cast<float*>(feeler + 0x78);
                point[0] = start[0] + static_cast<float>(answers.Below(9)) - 4.0f;
                point[1] = start[1] - static_cast<float>(answers.Below(120));
                point[2] = start[2] + static_cast<float>(answers.Below(9)) - 4.0f;
                point[3] = 1.0f;
                float* normal = reinterpret_cast<float*>(feeler + 0x88);
                normal[0] = 0.0f;
                normal[1] = answers.Below(4) == 0 ? 0.05f : (answers.Below(9) == 0 ? 0.087f : 0.9f);
                normal[2] = 0.1f;
                normal[3] = 0.0f;
                feeler[0x6D] = static_cast<uint8_t>(answers.Below(2));
                *reinterpret_cast<uint32_t*>(feeler + 0x58) = answers.Below(3) == 0 ? 0 : 0x0DE00000 + answers.Below(4);
                return true;
            };
            g_answers.node = [&answers](uint32_t) {
                return answers.Below(3) == 0 ? nullptr : reinterpret_cast<Script::CStruct*>(g_world.node);
            };
            g_answers.getChecksum = [&answers](uint32_t, uint32_t* out) {
                *out = answers.Below(3) == 0 ? 0 : 0xF1A60000 + answers.Below(8);
                return true;
            };
            g_answers.script = [&answers](uint32_t name) -> uint8_t* {
                if (answers.Below(5) == 0 || (name & 0xFF) > 6) // (a bounded chain: the seen list has 20 places)
                    return nullptr;
                return BuildScript(name, answers);
            };
            g_answers.frameSeconds = 1.0 / 60;
            Originals guard(original, {0x004E5940, 0x004E5060, 0x004E5100, 0x004E59A0, 0x004E4ED0, 0x004E51F0,
                                       0x004E59D0, 0x004E5210, 0x004E5C20, 0x004E5170, 0x004E5A10, 0x004E5460,
                                       0x004E5640, 0x004E4F50, 0x004E5320, 0x004E5390});
            std::vector<uint32_t> trace;
            g_answers.callocFails = seed == 2;
            trace.push_back(WorldRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004E5940)()));
            g_answers.callocFails = false;
            auto* c = reinterpret_cast<Obj::CEditorCameraComponent*>(g_world.component);
            trace.push_back(WorldRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004E5060)(c)));
            c->m_object = WorldObject();
            Fn<void(__thiscall*)(void*, void*)>(original, 0x004E4ED0)(c, reinterpret_cast<void*>(0x5A5A0000));
            Fn<void(__thiscall*)(void*)>(original, 0x004E51F0)(c);
            c->m_cursor = WorldObject()->m_pos;
            c->m_zoom = 300.0f;
            for (int frame = 0; frame < 30; ++frame) {
                static const int kControllers[] = {2, 4, 5, 7};
                GameVariable<int>(0x007CEFA8) = kControllers[(seed + frame) % 4];
                uint8_t* pad = g_world.input + 0x20;
                *reinterpret_cast<uint32_t*>(g_world.input + 0x1C) = random.Next() ^ (random.Next() << 8); // (Next has 24 bits)
                for (uint32_t offset : {0x00u, 0x20u, 0x40u, 0x60u, 0x80u, 0xA0u, 0xE0u, 0x100u})
                    pad[offset] = random.Below(4) == 0;
                for (uint32_t offset : {0x208u, 0x20Cu, 0x210u, 0x214u}) {
                    int v = static_cast<int>(random.Below(257)) - 128;
                    if (random.Below(4) == 0)
                        v = 0;
                    *reinterpret_cast<float*>(pad + offset) = offset < 0x210 ? static_cast<float>(v)
                                                                             : static_cast<float>(v) / 128.0f;
                }
                if (frame == 4)
                    c->m_heading = 6.2f;
                if (frame == 9)
                    c->m_heading = -0.1f;
                if (frame % 7 == 3)
                    trace.push_back(static_cast<uint32_t>(Fn<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                        original, 0x004E5210)(c, frame % 2 ? 0x69CB3AE1 : 0xF362BBBF, reinterpret_cast<void*>(0x5A5A0000),
                                              nullptr)));
                if (frame % 11 == 5)
                    Fn<void(__thiscall*)(void*, bool)>(original, 0x004E59D0)(c, (frame & 1) != 0);
                Fn<void(__thiscall*)(void*)>(original, 0x004E5C20)(c);
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(c), sizeof(*c)));
            }
            trace.push_back(static_cast<uint32_t>(Fn<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                original, 0x004E5210)(c, 0x1234, nullptr, nullptr)));
            for (uint32_t name : {0xF1A60001u, 0x0FFF0000u, 0x22220000u, 0x33330000u}) {
                GameVariable<int>(0x007CCC3C) = 0;
                trace.push_back(Fn<bool(__cdecl*)(uint32_t)>(original, 0x004E4F50)(name));
            }
            // PlaceCursor directed: the camera low or not, drops below / at / above the step, rises, with and without
            // simple collision, the editor's own camera or not, the global under / over the height.
            struct V4 {
                float x, y, z, w;
            };
            for (int k = 0; k < 96; ++k) {
                static const float kHeights[] = {0.5f, 1.0f, 1.5f, 2.5f};
                static const float kRises[] = {-23.0f, -24.0f, -25.0f, 0.0f, 5.0f, 30.0f};
                c->m_cursor = {10.0f, 100.0f, 10.0f, 1.0f};
                c->m_height = kHeights[k % 4];
                c->m_minHeight = 1.0f;
                c->m_simpleCollision = (k / 24) % 2 != 0;
                c->m_noWallCheck = true;
                WorldObject()->m_id = k >= 48 ? 0x5B509AD3 : 0x0B1E0001;
                placeGlobal = (k / 4) % 2 ? 0.0f : 3.0f;
                float level = 100.0f + kRises[(k / 4) % 6];
                ground = &level;
                bool placed = Fn<bool(__thiscall*)(void*, V4, float)>(original, 0x004E5640)(c, {10.0f, 100.0f, 9.0f, 1.0f}, 24.0f);
                ground = nullptr;
                trace.push_back(placed);
                trace.push_back(std::bit_cast<uint32_t>(c->m_height));
                trace.push_back(std::bit_cast<uint32_t>(c->m_cursor.y));
            }
            placeGlobal = -1.0f;
            // Lowering with nothing hit, close in (0.2 a frame): from just over the minimum height (put back on it),
            // from further up and from under it.
            for (float height : {5.1f, 5.25f, 4.0f, 7.0f}) {
                uint8_t* pad = g_world.input + 0x20;
                std::memset(pad, 0, 0x218);
                *reinterpret_cast<uint32_t*>(g_world.input + 0x1C) = 0;
                pad[0xA0] = 1;
                GameVariable<int>(0x007CEFA8) = 2;
                c->m_minHeight = 5.0f;
                c->m_height = height;
                c->m_zoom = 50.0f;
                c->m_minZoom = 20.0f;
                c->m_maxZoom = 2000.0f;
                float nothing = std::numeric_limits<float>::quiet_NaN();
                ground = &nothing;
                Fn<void(__thiscall*)(void*)>(original, 0x004E5C20)(c);
                ground = nullptr;
                trace.push_back(std::bit_cast<uint32_t>(c->m_height));
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004E5100)(c);
            c->m_vtable = reinterpret_cast<void**>(0x0064B2D0);
            trace.push_back(WorldRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned)>(original, 0x004E59A0)(c, seed & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            std::vector<uint32_t> world = WorldWords();
            trace.insert(trace.end(), world.begin(), world.end());
            memory.Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the editor camera component");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestEditorCamera, RemoveStandIns);
