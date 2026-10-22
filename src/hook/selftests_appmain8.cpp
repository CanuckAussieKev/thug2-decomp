// Self-tests for drawing a scene's meshes and the light glows (appmain batch 8: src/game/gfx/scene_render.cpp,
// glow.cpp): the original and ours against stand-in culling, materials, mesh draws, shadows and occlusion queries;
// every call and the memory compared.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "game/gfx/glow.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/scene_render.h"
#include "game/gfx/shadow.h"
#include "game/lst/hashtable.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

struct SceneWorld {
    alignas(16) uint8_t scene[0x100];
    uint8_t meshes[40][0x100];
    uint8_t materials[5][0x20];
    uint8_t* list[0x1100]; // (the big scenes repeat the 40 meshes)
};
alignas(16) SceneWorld g_scene;

uint32_t SceneRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_scene);
    if (word >= base && word < base + sizeof(g_scene))
        return 0xA0000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}
uint32_t MeshIndex(uint32_t p)
{
    auto base = reinterpret_cast<uint32_t>(g_scene.meshes);
    return p >= base && p < base + sizeof(g_scene.meshes) ? (p - base) / 0x100 : 0xFFFF;
}

// RenderScene with every flag combination that matters, viewports, the depth around 600 (also exactly, not a
// number), meshes shown / hidden / flagged / culled / occluded, the transparent and flagged ranges, shadows on / off,
// the anisotropic filter on / off; CompareDistances through the sort.
void TestRenderScene()
{
    GameMemory memory({{0x00786E30, 0x110}, {0x00787338, 0x14}, {0x007CA968, 4}, {0x007CA978, 4},
                       {0x00792640, 0x4000}, {0x007B7640, 0x300}, {0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    g_relocate = SceneRelocate;
    DWORD minFilter = 3;
    com.On(kDevice, kGetSamplerState, [&](Regs& r) {
        *reinterpret_cast<DWORD*>(r.args[3]) = minFilter;
        return 0u;
    });
    Lcg* script = nullptr;
    static const float kDepths[] = {10.0f, 599.0f, 600.0f, 601.0f, 5000.0f, -3.0f,
                                    std::numeric_limits<float>::quiet_NaN(), 600.00006f};
    s.At({0x004DB790, AddressOf(&Nx::SphereInView)}, 0, [&](Regs& r) {
        Record({0xB790, MeshIndex(r.args[0] - 4), r.args[1]});
        GameVariable<float>(0x007CA978) = script->Pick(kDepths);
        return static_cast<uint32_t>(script->Below(5) != 0);
    });
    s.At({0x004D9AC0, AddressOf(&Nx::SphereOccluded)}, 0, [&](Regs& r) {
        Record({0x9AC0, MeshIndex(r.args[0] - 4), r.args[1], r.args[2]});
        return static_cast<uint32_t>(script->Below(4) == 0);
    });
    s.At({0x004D1F00}, 0, [](Regs& r) { Record({0x1F00, SceneRelocate(r.ecx)}); return 0u; });
    s.At({0x004D5290}, 0, [](Regs& r) { Record({0x5290, MeshIndex(r.ecx)}); return 0u; });
    s.At({0x004DBF50, AddressOf(&Nx::DrawShadows)}, 0, [](Regs& r) {
        Record({0xBF50, SceneRelocate(r.args[0]), r.args[2]});
        auto* list = reinterpret_cast<uint8_t* const*>(r.args[1]);
        for (uint32_t i = 0; i < r.args[2] && i < 64; ++i)
            Record({MeshIndex(reinterpret_cast<uint32_t>(list[i]))});
        Record({HashBytes(reinterpret_cast<const uint8_t*>(list), 4 * (r.args[2] < 0x1000 ? r.args[2] : 0x1000))});
        return 0u;
    });
    s.At({0x004DB9D0, AddressOf(&Nx::DrawShadowVolumes)}, 0, [](Regs& r) {
        Record({0xB9D0, SceneRelocate(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x004CEAB0}, 4, [](Regs& r) { Record({0xEAB0, r.ecx, r.args[0]}); return 0u; });
    // Opaque (1), transparent (2), occlusion (4), no culling (8), near first (0x10), volumes (0x20), stats (0x40).
    static const uint32_t kFlags[] = {0x01, 0x11, 0x03, 0x13, 0x02, 0x07, 0x1B, 0x0B, 0x1B, 0x43, 0x1B, 0x20,
                                      0x21, 0x15, 0x05, 0x13, 0x13, 0x11, 0x1B, 0x12, 0x13, 0x17, 0x51, 0x13};
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0x5CE00000 + seed);
            Lcg random(0x5CE10000 + seed);
            Lcg scripted(0x5CE20000 + seed);
            script = &scripted;
            std::memset(&g_scene, 0, sizeof(g_scene));
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x00786F34) = 0; // (no blend override)
            GameVariable<uint8_t>(0x00786E51) = static_cast<uint8_t>(random.Below(2));
            minFilter = random.Below(2) ? 3 : 2;
            auto* scene = reinterpret_cast<Nx::sScene*>(g_scene.scene);
            bool big = seed == 7 || seed == 19; // (over 0x1000 meshes: the drawn list's cap, the transparent limit)
            int count = big ? 0x1000 + 40 : 8 + static_cast<int>(random.Below(30));
            scene->meshCount = count;
            scene->meshes = g_scene.list;
            scene->firstTransparent = static_cast<int>(random.Below(count + 1));
            scene->firstFlagged = scene->firstTransparent + static_cast<int>(random.Below(count - scene->firstTransparent + 1));
            scene->flaggedCount = random.Below(4) == 0 ? 0 : static_cast<int>(random.Below(count - scene->firstFlagged + 1));
            if (big) {
                scene->firstTransparent = seed == 7 ? 0x1000 : 0x0FFF;
                scene->firstFlagged = scene->firstTransparent + 8;
                scene->flaggedCount = 20;
            }
            if (seed % 6 == 3) { // (many flagged ones: the sort by depth)
                scene->firstTransparent = 0;
                scene->firstFlagged = 2;
                scene->flaggedCount = count - 4;
            }
            scene->flags = random.Below(2) || big ? 0x100 : 0;
            scene->unknown40 = seed == 11;
            for (int i = 0; i < 40; ++i) {
                uint8_t* mesh = g_scene.meshes[i];
                *reinterpret_cast<uint32_t*>(mesh) = random.Pick({0x40u, 0x40u, 0x40u, 0x50u, 0x240u, 0x0u, 0x440u});
                if (big) // (all drawn: 4096 and more remembered)
                    *reinterpret_cast<uint32_t*>(mesh) = 0x40;
                *reinterpret_cast<float*>(mesh + 0x10) = static_cast<float>(i) + 0.5f;
                *reinterpret_cast<uint8_t**>(mesh + 0x14) = g_scene.materials[random.Below(5)];
                mesh[0x1B] = static_cast<uint8_t>(random.Next());
            }
            for (int i = 0; i < count; ++i)
                g_scene.list[i] = g_scene.meshes[i % 40];
            uint32_t flags = kFlags[seed - 1];
            int viewport = static_cast<int>(random.Below(9));
            Originals guard(original, {0x004DC700, 0x004DA690});
            Fn<void(__cdecl*)(void*, uint32_t, int)>(original, 0x004DC700)(scene, flags, viewport);
            std::vector<uint32_t> trace = g_trace;
            for (int i = 0; i < 40; ++i)
                trace.push_back(*reinterpret_cast<uint32_t*>(g_scene.meshes[i] + 0x7C));
            memory.Dump(trace);
            for (uint32_t& word : trace)
                word = MeshIndex(word) != 0xFFFF ? 0xB0000000 + MeshIndex(word) : word;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: drawing a scene's meshes");
    }
    g_relocate = nullptr;
    delete &s;
}

// UpdateGlows: the query pass (queries handed out, all 1024 in use, three outstanding) and the draw pass (answers
// 0 / 1 / other, pixels seen or not, the Radeon 8500 flag), the camera's axes of every kind (also a column of zeros).
void TestGlows()
{
    GameMemory memory({{0x00786BD0, 0x40}, {0x00786E30, 0x110}, {0x007BA520, 0x400}, {0x007CA940, 0x28},
                       {0x007CA9D8, 0x18}, {0x007CA9F0, 0x40}, {0x007D69E0, 4}, {0x00786AB5, 1}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    g_relocate = [](uint32_t word) { return OnStack(word) ? 0x57AC0000u : word; };
    alignas(16) static Nx::sGlow glows[6];
    static Lst::HashTable::Item items[4];
    Lcg* script = nullptr;
    s.At({0x005F4C80, Resolve(OursAt(0x005F4C80))}, 0, [&](Regs& r) {
        Record({0x4C80, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.args[1]) = script->Below(3) == 0 ? 0 : script->Below(500);
        return script->Below(3);
    });
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0x610E0000 + seed);
            Lcg random(0x610F0000 + seed);
            Lcg scripted(0x61100000 + seed);
            script = &scripted;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x00786F34) = 0; // (no blend override)
            GameVariable<uint32_t>(0x00786F34) = 0; // (no blend override)
            GameVariable<bool>(0x00786AB5) = seed == 6;
            std::memset(&GameVariable<uint8_t>(0x007BA520), seed == 3 ? 1 : 0, 0x400);
            if (seed == 4)
                GameVariable<uint8_t>(0x007BA520 + 0x3FE) = 0;
            float* view = &GameVariable<float>(0x00786BD0);
            view[2] = seed == 5 ? 0.0f : static_cast<float>(random.Below(2001)) / 1000.0f - 1.0f;
            view[6] = seed == 5 ? 0.0f : static_cast<float>(random.Below(2001)) / 1000.0f - 1.0f;
            view[10] = static_cast<float>(random.Below(2001)) / 1000.0f - 1.0f;
            if (seed == 9) // (a column with no z: the zero's sign)
                view[10] = 0.0f;
            if (seed == 10) { // (tiny: normalised all the same)
                view[2] *= 1e-5f;
                view[6] *= 1e-5f;
                view[10] *= 1e-5f;
            }
            for (Nx::sGlow& g : glows) {
                std::memset(&g, 0, sizeof(g));
                for (float& p : g.position)
                    p = static_cast<float>(random.Below(2001)) / 10.0f - 100.0f;
                g.maxSize = static_cast<float>(random.Below(100)) / 7.0f;
                g.size = static_cast<float>(random.Below(100)) / 9.0f;
                g.querySize = static_cast<float>(random.Below(100)) / 11.0f;
                g.rate = static_cast<float>(random.Below(100)) / 100.0f;
                g.queryCount = random.Below(5);
                for (uint32_t& q : g.queries)
                    q = random.Below(0x400);
                g.pixels = random.Below(2) ? 0 : 77;
            }
            auto& table = GameVariable<Lst::HashTable>(0x007CA9D8);
            table.m_bits = seed >= 11 ? 0 : 2; // (one glow alone: its first query 0 is free)
            table.m_buckets = items;
            if (seed >= 11) {
                std::memset(&GameVariable<uint8_t>(0x007BA520), 0, 0x400);
                glows[0].queryCount = 0;
            }
            items[0] = {1, &glows[0], nullptr};
            items[1] = {2, &glows[1], nullptr};
            items[2] = {3, seed == 8 ? nullptr : &glows[2], nullptr};
            items[3] = {4, &glows[3], nullptr};
            Originals guard(original, {0x004DCC70, 0x00588190});
            for (int pass = 0; pass < 4; ++pass)
                Fn<void(__cdecl*)(bool)>(original, 0x004DCC70)(((pass + seed) & 1) != 0);
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(glows), sizeof(glows)));
            memory.Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: light glows");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestRenderScene, RemoveStandIns);
SELFTEST(Early, TestGlows, RemoveStandIns);
