// Self-tests for the PC instances (src/game/gfx/pc_instance.cpp, RFX-23): the original and ours over stand-in scenes,
// meshes, bones and models' lights - constructed, compared, drawn all together and one by one, destroyed - with a
// recording stand-in device; the scene drawing, culling, lights, render states, qsort and D3DX stood in.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/render_state.h"
#include "game/gfx/scene_render.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sInstance;
using Nx::sScene;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

float g_nearDistance;

std::vector<uint8_t> LoadFloat(const float* value)
{
    std::vector<uint8_t> bytes = {0xD9, 0x05, 0, 0, 0, 0, 0xC3};
    uint32_t address = reinterpret_cast<uint32_t>(value);
    std::memcpy(&bytes[2], &address, 4);
    return bytes;
}

sScene* MakeScene(Lcg& random)
{
    auto* scene = reinterpret_cast<sScene*>(Bump(sizeof(sScene)));
    std::memset(scene, 0, sizeof(sScene));
    scene->flags = random.Below(5) ? 0x100u : 0x180u;
    int count = static_cast<int>(random.Below(5)); // (none sometimes)
    scene->meshCount = count;
    scene->transparentCount = count ? static_cast<int>(random.Below(count + 1)) : 0;
    scene->firstTransparent = count - scene->transparentCount;
    scene->meshes = reinterpret_cast<uint8_t**>(Bump(4 * (count + 1)));
    for (int i = 0; i < count; ++i) {
        uint8_t* mesh = reinterpret_cast<uint8_t*>(Bump(0xD8));
        std::memset(mesh, 0, 0xD8);
        *reinterpret_cast<uint32_t*>(mesh) = (random.Below(2) ? 0x40u : 0u) | (random.Below(7) ? 0u : 0x20000u);
        mesh[0x1F] = static_cast<uint8_t>(random.Below(4) == 0 ? 0xFF : random.Below(5));
        mesh[0x38] = static_cast<uint8_t>(random.Below(6) ? 0x10 : 0);
        if (random.Below(3)) {
            uint8_t* material = reinterpret_cast<uint8_t*>(Bump(0x110));
            std::memset(material, 0, 0x110);
            *reinterpret_cast<float*>(material + 0x1C) = static_cast<float>(random.Below(3));
            *reinterpret_cast<uint8_t**>(mesh + 0x14) = material;
        }
        scene->meshes[i] = mesh;
    }
    for (float& v : scene->centre)
        v = RandomFloat(random, -500, 500);
    scene->radius = RandomFloat(random, 1, 150);
    uint32_t kinds[4] = {0, 0, 3, 0xFFFFFFFF};
    scene->unknown78 = kinds[random.Below(4)];
    return scene;
}

uint8_t* MakeModel(Lcg& random, void** lightsVtable)
{
    if (random.Below(3) == 0)
        return nullptr;
    uint8_t* model = reinterpret_cast<uint8_t*>(Bump(0x40));
    std::memset(model, 0, 0x40);
    if (random.Below(3)) {
        uint8_t* lights = reinterpret_cast<uint8_t*>(Bump(0x30));
        std::memset(lights, 0, 0x30);
        *reinterpret_cast<void***>(lights) = lightsVtable;
        *reinterpret_cast<uint32_t*>(lights + 4) = 0x5A000000 + random.Below(9);
        *reinterpret_cast<uint8_t**>(model + 0x2C) = lights;
    }
    return model;
}

void StandInRendering(StandIns& s)
{
    s.At({0x004DC700, AddressOf(&Nx::RenderScene)}, 0, [](Regs& r) {
        auto* scene = reinterpret_cast<sScene*>(r.args[0]);
        uint32_t shown = 0;
        for (int i = 0; i < scene->meshCount; ++i)
            shown = shown << 1 | ((*reinterpret_cast<uint32_t*>(scene->meshes[i]) >> 6) & 1);
        Record({0xC700, Relocate(r.args[0]), r.args[1], r.args[2], shown, Relocate(GameVariable<uint32_t>(0x007825B0)),
                GameVariable<uint32_t>(0x007825B4), Relocate(GameVariable<uint32_t>(0x007825B8)),
                GameVariable<uint32_t>(0x00786F1C), GameVariable<uint32_t>(0x00786F20), GameVariable<uint8_t>(0x00786E60),
                GameVariable<uint8_t>(0x00786EB4)});
        return 0u;
    });
    s.At({0x004DB620, AddressOf(&Nx::SetWorldTransform)}, 0, [](Regs& r) {
        Record({0xB620, r.args[0] ? HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 0x40) : 0u});
        return 0u;
    });
    s.At({0x004DB790, AddressOf(&Nx::SphereInView)}, 0, [](Regs& r) {
        Record({0xB790, Relocate(r.args[0]), r.args[1]});
        return static_cast<uint32_t>((r.args[1] >> 9) % 4 != 0);
    });
    s.At({0x004D9AC0, AddressOf(&Nx::SphereOccluded)}, 0, [](Regs& r) {
        Record({0x9AC0, Relocate(r.args[0]), r.args[1], r.args[2]});
        return static_cast<uint32_t>((r.args[1] >> 11) % 5 == 0);
    });
    s.At({0x004B6940, AddressOf(&Nx::ResetLights)}, 0, [](Regs&) {
        Record({0x6940});
        return 0u;
    });
    s.At({0x004DAB60, AddressOf(&Nx::SetState)}, 0, [](Regs& r) {
        Record({0xAB60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004DA730, AddressOf(&Nx::SetTexture)}, 0, [](Regs& r) {
        Record({0xA730, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004DE490, AddressOf(&Nx::sScene::DestroyBody)}, 0, [](Regs& r) {
        Record({0xE490, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) { return 0x12345u; });
    using GetFloatFn = float (*)(uint32_t);
    s.Bytes(0x00478F00, LoadFloat(&g_nearDistance));
    s.Bytes(AddressOf(static_cast<GetFloatFn>(&Script::GetFloat)), LoadFloat(&g_nearDistance));
    s.At({0x006242A0}, 0, [](Regs& r) { // qsort, with the comparison given
        Record({0x42A0, Relocate(r.args[0]), r.args[1]});
        std::qsort(reinterpret_cast<void*>(r.args[0]), r.args[1], r.args[2],
                   reinterpret_cast<int(__cdecl*)(const void*, const void*)>(r.args[3]));
        return 0u;
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        float out[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                out[i * 4 + j] = static_cast<float>(sum);
            }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
}

void TestPcInstances()
{
    GameMemory memory({{0x007D1800, 4}, {0x007D69E0, 4}, {0x007825B0, 0x60}, {0x0067E9D8, 0xD0},
                       {0x00786F1C, 0x6C}, {0x00786EC4, 4}, {0x00786E60, 1}, {0x00786EB4, 1}, {0x00786CC4, 0xC},
                       {0x00786B90, 0x40}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    StandInRendering(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    com.On(kDevice, 44, [](Regs& r) { // SetTransform
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 0x40)});
        return 0u;
    });
    com.On(kDevice, 51, [](Regs& r) { // SetLight
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), sizeof(D3DLIGHT9))});
        return 0u;
    });
    // the models' lights: virtual [7] (Update) recorded
    void** lightsVtable = new void*[8];
    for (int i = 0; i < 8; ++i)
        lightsVtable[i] = s.Thunk(0, [i](Regs&) {
            Record({0xBAD0 + static_cast<uint32_t>(i)});
            return 0u;
        });
    lightsVtable[7] = s.Thunk(8, [](Regs& r) {
        Record({0x63B0, Relocate(r.ecx), HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 16), r.args[1]});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    const uint32_t passFlags[8] = {1, 2, 3, 0x82, 0x102, 0x183, 0x1, 0x42};
    bool ok = true, reported = false;
    for (int round = 0; round < 20; ++round) {
        ok &= CompareTwoRuns("pc instances", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x1130 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<sInstance*>(0x007825C0) = nullptr;
            GameVariable<uint32_t>(0x007825BC) = round % 3 == 0 ? 0 : 0x0BAD0000;
            Lcg random(0x11300 + round);
            g_nearDistance = RandomFloat(random, 0, 60);
            std::vector<uint32_t> trace;
            auto* lights = &GameVariable<float>(0x00786F38);
            for (int k = 0; k < 19; ++k)
                lights[k] = RandomFloat(random, round == 4 ? -1.0f : 0.0f, round == 4 ? 3.0f : 1.0f);
            auto* camera = &GameVariable<float>(0x00786CC4);
            for (int k = 0; k < 3; ++k)
                camera[k] = RandomFloat(random, -300, 300);
            // instances: constructed
            std::vector<sInstance*> instances;
            Nx::sScene* shared = MakeScene(random);
            Mth::Matrix* sharedBones = reinterpret_cast<Mth::Matrix*>(Bump(0x40 * 3));
            for (int k = 0; k < 3 * 16; ++k)
                reinterpret_cast<float*>(sharedBones)[k] = RandomFloat(random, -2, 2);
            for (int k = 0; k < 9; ++k) {
                float transform[16];
                for (float& v : transform)
                    v = RandomFloat(random, -400, 400);
                int boneCount = static_cast<int>(random.Below(4) == 0 ? 0x40 + random.Below(3) : random.Below(4));
                Mth::Matrix* bones = nullptr;
                if (random.Below(4)) {
                    if (random.Below(3) == 0 && boneCount <= 3) {
                        bones = sharedBones;
                    } else {
                        int made = boneCount < 0x3A ? boneCount + 1 : 0x3A;
                        bones = reinterpret_cast<Mth::Matrix*>(Bump(0x40 * made));
                        for (int i = 0; i < 16 * made; ++i)
                            reinterpret_cast<float*>(bones)[i] = RandomFloat(random, -2, 2);
                    }
                }
                sScene* scene = random.Below(4) == 0 ? shared : MakeScene(random);
                if (!bones && scene->unknown78 != 0 && boneCount > 0) // (bones drawn one by one need bones)
                    bones = sharedBones, boneCount = boneCount < 3 ? boneCount : 3;
                void* at = reinterpret_cast<void*>(Bump(sizeof(sInstance)));
                auto* instance = Fn<sInstance*(__thiscall*)(void*, sScene*, const float*, int, Mth::Matrix*)>(
                    original, 0x004D1030)(at, scene, transform, boneCount, bones);
                trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(instance)));
                trace.push_back(reinterpret_cast<uint32_t>(instance->model));
                instance->model = MakeModel(random, lightsVtable);
                if (random.Below(3) == 0)
                    instance->flags |= 4;
                for (uint8_t& c : instance->color)
                    c = static_cast<uint8_t>(random.Below(256));
                if (random.Below(6) == 0)
                    instance->active = false;
                if (random.Below(3) == 0)
                    instance->viewports = random.Next();
                instances.push_back(instance);
            }
            // compared
            for (int k = 0; k < 24; ++k) {
                sInstance* pair[2] = {instances[random.Below(9)], instances[random.Below(9)]};
                trace.push_back(Fn<uint32_t(__cdecl*)(const void*, const void*)>(original, 0x004D0F40)(&pair[0],
                                                                                                        &pair[1]));
            }
            // drawn all together
            for (int k = 0; k < 3; ++k)
                Fn<void(__cdecl*)(uint32_t, int)>(original, 0x004D1760)(passFlags[random.Below(8)],
                                                                         static_cast<int>(random.Below(3)));
            // one by one, and directly
            for (sInstance* instance : instances) {
                Fn<void(__cdecl*)(sInstance*, uint32_t)>(original, 0x004D16B0)(instance, random.Below(2) ? 8 : 0);
                if (random.Below(2))
                    Fn<void(__thiscall*)(sInstance*, uint32_t)>(original, 0x004D1130)(instance, passFlags[random.Below(8)]);
            }
            // destroyed (some own their scenes and bones)
            for (size_t k = 0; k < instances.size(); ++k) {
                if (random.Below(3) == 0)
                    continue;
                if (random.Below(2))
                    instances[k]->flags |= 1;
                Fn<void(__thiscall*)(sInstance*)>(original, 0x004D10C0)(instances[k]);
            }
            trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x007825C0)));
            // a UV wibble, a texture animation
            void* wibble = reinterpret_cast<void*>(Bump(0x30));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D1A60)(wibble)));
            void* anim = reinterpret_cast<void*>(Bump(8));
            Fn<void(__thiscall*)(void*)>(original, 0x004D1A80)(anim);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete[] lightsVtable;
    g_relocate = nullptr;
    Check(ok, "pc instances: constructed, compared, drawn together / one by one / directly, destroyed; UV wibble");
}

} // namespace

SELFTEST(Early, TestPcInstances, RemoveStandIns);
