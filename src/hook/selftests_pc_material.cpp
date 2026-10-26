// Self-tests for the PC materials (src/game/gfx/pc_material.cpp, RFX-25): the original and ours over random
// materials - every pass flag, textures or none, colours (some exactly 0.5), UV wibbles, colour and texture
// animations - constructed, animated, set up for the shaders and pass by pass, destroyed; a recording stand-in
// device, the render states, the time and the heap stood in.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/material.h"
#include "game/gfx/primitive_states.h"
#include "game/gfx/render_state.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::sMaterial;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

uint32_t g_time;
uint32_t g_stageState[2];

uint8_t* MakeTexture(Lcg& random)
{
    auto* texture = reinterpret_cast<uint8_t*>(Bump(0x20));
    std::memset(texture, 0, 0x20);
    *reinterpret_cast<uint32_t*>(texture + 0x10) = 0x7E000000 + random.Below(64);
    *reinterpret_cast<uint32_t*>(texture + 0x14) = 0x7F000000 + random.Below(64);
    return texture;
}

void FillMaterial(sMaterial* m, Lcg& random)
{
    static const uint32_t kFlags[] = {1, 2, 8, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x4000};
    m->passCount = random.Below(5);
    m->twoSided = static_cast<uint8_t>(random.Below(2));
    m->alphaRef = static_cast<uint8_t>(random.Below(3) * 64);
    m->depthBias = static_cast<uint8_t>(random.Below(17));
    m->hasUVWibble = random.Below(3) != 0;
    m->hasTextureAnim = random.Below(3) != 0;
    for (int pass = 0; pass < 4; ++pass) {
        uint32_t flags = 0;
        for (uint32_t flag : kFlags)
            if (random.Below(3) == 0)
                flags |= flag;
        m->passFlags[pass] = flags;
        m->textures[pass] = random.Below(5) ? MakeTexture(random) : nullptr;
        for (int k = 0; k < 4; ++k)
            m->passColor[pass][k] = random.Below(3) == 0 ? 0.5f : RandomFloat(random, 0, 1.2f);
        uint32_t blends[] = {0, 1, 5, 0xC, 0xD, 0x11};
        m->blendModes[pass] = blends[random.Below(6)] | (random.Below(2) << 24);
        m->addressModes[pass] = random.Below(3) | random.Below(3) << 16;
        m->envMapTiling[pass][0] = RandomFloat(random, 0, 6);
        m->envMapTiling[pass][1] = RandomFloat(random, 0, 6);
        m->mipLodBias[pass] = random.Next();
        auto* wibble = reinterpret_cast<Nx::sUVWibble*>(Bump(0x30));
        auto* w = reinterpret_cast<float*>(wibble);
        for (int k = 0; k < 12; ++k)
            w[k] = RandomFloat(random, -3, 3);
        m->uvWibble[pass] = wibble;
    }
    m->colorAnimCount = random.Below(4);
    m->colorAnims = reinterpret_cast<Nx::sColorAnim*>(Bump(12 * 4));
    m->animColors = reinterpret_cast<uint32_t*>(Bump(16));
    for (uint32_t i = 0; i < m->colorAnimCount; ++i) {
        Nx::sColorAnim& anim = m->colorAnims[i];
        anim.keyCount = 2 + random.Below(4);
        anim.phase = static_cast<int>(random.Below(500));
        anim.keys = reinterpret_cast<Nx::sColorKey*>(Bump(8 * anim.keyCount));
        int time = static_cast<int>(random.Below(50));
        for (uint32_t k = 0; k < anim.keyCount; ++k) {
            anim.keys[k].time = time;
            time += 1 + static_cast<int>(random.Below(60));
            for (uint8_t& c : anim.keys[k].rgba)
                c = static_cast<uint8_t>(random.Next());
        }
        if (random.Below(3) == 0) // (the time exactly on the second key)
            anim.phase = anim.keys[1].time - anim.keys[0].time - GameVariable<int>(0x00786D00);
        if (random.Below(6) == 0)
            anim.keys[anim.keyCount - 1].time = anim.keys[0].time; // (no period: time 1)
    }
    auto* anim = reinterpret_cast<Nx::sTextureAnim*>(Bump(0x44));
    std::memset(anim, 0, 0x44);
    for (int pass = 0; pass < 4; ++pass) {
        uint32_t count = 2 + random.Below(4);
        anim->keyCounts[pass] = count;
        anim->phases[pass] = random.Below(3000);
        anim->loops[pass] = static_cast<int>(random.Below(3)) - (random.Below(4) == 0 ? 1 : 0);
        anim->keys[pass] = reinterpret_cast<Nx::sTextureKey*>(Bump(8 * count));
        int time = static_cast<int>(random.Below(400));
        for (uint32_t k = 0; k < count; ++k) {
            anim->keys[pass][k].time = time;
            time += 1 + static_cast<int>(random.Below(700));
            anim->keys[pass][k].texture = MakeTexture(random);
        }
        int first = anim->keys[pass][0].time, period = anim->keys[pass][count - 1].time - first;
        if (random.Below(3) == 0) // (exactly at the last loop's end)
            anim->loops[pass] = (static_cast<int>(g_time) - first) / period;
    }
    m->textureAnim = anim;
    for (float& v : m->specular)
        v = RandomFloat(random, 0, 1);
    m->specularPower = RandomFloat(random, 1, 40);
}

void TestPcMaterials()
{
    GameMemory memory({{0x007D1800, 4}, {0x007D69E0, 4}, {0x00786C50, 0x30}, {0x00786D00, 4}, {0x00786E4C, 4},
                       {0x00786E60, 1}, {0x00786EA4, 0x10}, {0x00786EB4, 1}, {0x00786ECC, 0x68}, {0x0067EAA8, 0x120}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    com.On(kDevice, 44, [](Regs& r) { // SetTransform
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 0x40)});
        return 0u;
    });
    com.On(kDevice, 49, [](Regs& r) { // SetMaterial
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), sizeof(D3DMATERIAL9))});
        return 0u;
    });
    com.On(kDevice, 66, [](Regs& r) { // GetTextureStageState
        *reinterpret_cast<uint32_t*>(r.args[3]) = r.args[2] == D3DTSS_TEXCOORDINDEX ? g_stageState[1] : g_stageState[0];
        return 0u;
    });
    s.At({0x004DAB60, AddressOf(&Nx::SetState)}, 0, [](Regs& r) {
        Record({0xAB60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004DA760, AddressOf(&Nx::SetBlendMode)}, 0, [](Regs& r) {
        Record({0xA760, r.args[0]});
        return 0u;
    });
    s.At({0x004DA730, AddressOf(&Nx::SetTexture)}, 0, [](Regs& r) {
        Record({0xA730, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004B6030, AddressOf(&Nx::SetTextureStageState)}, 0, [](Regs& r) {
        Record({0x6030, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x004B6050, AddressOf(&Nx::SetTransform)}, 0, [](Regs& r) {
        Record({0x6050, r.args[0], HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 0x40)});
        return 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) { return g_time; });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("pc materials", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x1A90 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            Lcg random(0x1A900 + round);
            std::vector<uint32_t> trace;
            GameVariable<uint32_t>(0x00786F30) = round == 7 ? 1 : 0;
            GameVariable<uint32_t>(0x00786F2C) = random.Below(3) == 0 ? random.Below(16) : 0;
            GameVariable<uint32_t>(0x00786E4C) = random.Below(2);
            GameVariable<uint8_t>(0x00786E60) = static_cast<uint8_t>(random.Below(2));
            GameVariable<uint8_t>(0x00786EB4) = static_cast<uint8_t>(random.Below(2));
            GameVariable<uint8_t>(0x00786F24) = static_cast<uint8_t>(random.Below(2));
            GameVariable<int>(0x00786D00) = static_cast<int>(random.Below(100000));
            auto* constants = &GameVariable<float>(0x00786ECC);
            for (int k = 0; k < 16; ++k)
                constants[k] = RandomFloat(random, 0, 1);
            g_time = random.Below(round % 3 == 0 ? 1 << 12 : 1 << 30) | (round % 4 == 3 ? 0x80000000u : 0u);
            g_stageState[0] = random.Below(3);
            g_stageState[1] = random.Below(4);
            // constructed, filled
            void* at = reinterpret_cast<void*>(Bump(sizeof(sMaterial)));
            auto* m = Fn<sMaterial*(__thiscall*)(void*)>(original, 0x004D1A90)(at);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(m)));
            DumpArena(trace);
            FillMaterial(m, random);
            if (g_time & 0x80000000u) // (a time before the keys: the texture before the first, out of the list)
                m->hasTextureAnim = false;
            if (round == 4 || round == 5) // (the same constants as the passes': nothing to send - or the 4th)
                std::memcpy(constants, m->passColor, 0x40);
            if (round == 5) { // (only the 4th value differs: still sent)
                constants[3] += 1.0f;
                m->passCount = 2;
                GameVariable<uint8_t>(0x00786F24) = 0;
                GameVariable<uint32_t>(0x00786F2C) = 0;
            }
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004D1D90)(m));
            // animated
            Fn<void(__thiscall*)(void*)>(original, 0x004D1DC0)(m);
            Fn<void(__thiscall*)(void*)>(original, 0x004D1B40)(m);
            Fn<void(__thiscall*)(void*)>(original, 0x004D1CC0)(m);
            // set up
            Fn<void(__thiscall*)(void*)>(original, 0x004D1F00)(m);
            trace.push_back(HashBytes(&GameVariable<uint8_t>(0x00786ECC), 0x40));
            GameVariable<uint32_t>(0x00786E4C) = random.Below(2);
            Fn<void(__thiscall*)(void*)>(original, 0x004D23E0)(m);
            float color[3] = {RandomFloat(random, 0, 1), RandomFloat(random, 0, 1), RandomFloat(random, 0, 1)};
            for (uint32_t pass = 0; pass < m->passCount; ++pass) {
                if (pass > 0 && !m->textures[pass - 1])
                    m->textures[pass - 1] = MakeTexture(random); // (blending with the pass before needs its texture)
                if (!m->textures[pass] && random.Below(2))
                    m->textures[pass] = MakeTexture(random);
                g_stageState[1] = random.Below(4);
                Fn<void(__thiscall*)(void*, uint32_t, const float*)>(original, 0x004D24D0)(m, pass,
                                                                                             random.Below(2) ? color : nullptr);
            }
            // destroyed
            if (random.Below(4) == 0)
                m->colorAnims = nullptr;
            if (random.Below(4) == 0)
                m->textureAnim = nullptr;
            if (random.Below(3) == 0)
                m->uvWibble[random.Below(4)] = nullptr;
            Fn<void(__thiscall*)(void*)>(original, 0x004D2BD0)(m);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc materials: constructed, animated, set up for the shaders / pass by pass, destroyed");
}

} // namespace

SELFTEST(Early, TestPcMaterials, RemoveStandIns);
