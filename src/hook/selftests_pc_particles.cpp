// Self-tests for the PC model's bounding spheres and the PC particle systems (src/game/gfx/pc_model.cpp,
// pc_particles.cpp, RFX-10): the original and ours over stand-in geoms, a recording stand-in device, the dynamic
// vertex buffer, the culling tests, the materials' code, rand() and the engine's constructors stood in; the
// systems' state, their vertices and the random sequence compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/occlusion.h"
#include "game/gfx/pc_model.h"
#include "game/gfx/pc_particles.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcModel;
using Nx::CPcParticleSystem;
using Nx::sModelGeomNode;
using Nx::sParticleBurst;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// --- the model ---

// Models with no geoms, one, and several: spheres inside each other, around each other, the same, apart, NaN.
void TestPcModelSpheres()
{
    StandIns& s = *new StandIns();
    StandInMemory(s);
    s.At({0x00494C40}, 0, [](Regs& r) {
        Record({0x4C40, Relocate(r.ecx)});
        *reinterpret_cast<uint32_t*>(r.ecx) = 0x00649348;
        return r.ecx;
    });
    StandInThis(s, {0x00494D00}, 0);
    s.At({0x0048DE90}, 4, [](Regs& r) { // a geom's sphere: kept at its +4
        Record({0xDE90, Relocate(r.ecx)});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), reinterpret_cast<const void*>(r.ecx + 4), 16);
        return r.args[0];
    });
    s.At({0x0048DEB0}, 4, [](Regs& r) {
        const auto* sphere = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xDEB0, Relocate(r.ecx), sphere[0], sphere[1], sphere[2], sphere[3]});
        return 0u;
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("model spheres", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            Lcg random(0x3D00 + round);
            auto* model = reinterpret_cast<CPcModel*>(Bump(sizeof(CPcModel)));
            ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004BAD90)(model));
            // the geom ring
            auto* head = reinterpret_cast<sModelGeomNode*>(Bump(sizeof(sModelGeomNode)));
            head->next = head->prev = head;
            model->geoms = head;
            int count = round == 0 ? 0 : 1 + static_cast<int>(random.Below(6));
            model->geomCount = static_cast<uint16_t>(count);
            float base[4] = {RandomFloat(random, -50, 50), RandomFloat(random, -50, 50), RandomFloat(random, -50, 50),
                             RandomFloat(random, 1, 30)};
            for (int i = 0; i < count; ++i) {
                auto* geom = reinterpret_cast<float*>(Bump(0x20));
                float* sphere = geom + 1;
                switch (random.Below(6)) {
                case 0: // the same as the first
                    std::memcpy(sphere, base, 16);
                    break;
                case 1: // inside it
                    for (int k = 0; k < 3; ++k)
                        sphere[k] = base[k] + RandomFloat(random, -1, 1);
                    sphere[3] = base[3] * 0.25f;
                    break;
                case 2: // around it
                    for (int k = 0; k < 3; ++k)
                        sphere[k] = base[k] + RandomFloat(random, -1, 1);
                    sphere[3] = base[3] * 4.0f + 5.0f;
                    break;
                default:
                    for (int k = 0; k < 3; ++k)
                        sphere[k] = RandomFloat(random, -80, 80);
                    sphere[3] = RandomFloat(random, 0.5f, 25);
                    break;
                }
                if (round == 7 && i == 1)
                    sphere[3] = std::numeric_limits<float>::quiet_NaN();
                auto** entry = reinterpret_cast<void**>(Bump(0x18));
                *entry = geom;
                auto* node = reinterpret_cast<sModelGeomNode*>(Bump(sizeof(sModelGeomNode)));
                node->entry = entry;
                node->next = head;
                node->prev = head->prev;
                head->prev->next = node;
                head->prev = node;
            }
            float out[4] = {7, 7, 7, 7};
            uint32_t result = ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, float*)>(original, 0x004BAE30)(model, out));
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(result == reinterpret_cast<uint32_t>(out));
            for (float f : out)
                trace.push_back(std::bit_cast<uint32_t>(f));
            float set[4] = {RandomFloat(random, -9, 9), 1, 2, 3};
            g_trace.clear();
            Fn<void(__thiscall*)(void*, const float*)>(original, 0x004BADF0)(model, set);
            Fn<void(__thiscall*)(void*)>(original, 0x004BADB0)(model);
            trace.push_back(ArenaRelocate(
                Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BADC0)(model, round & 1 ? 1 : 0)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc particles: a model's geoms' bounding spheres together, set; constructed, destroyed");
}

// --- the particle systems ---

uint32_t g_rand;

void StandInParticleCalls(StandIns& s, FakeComSet& com)
{
    StandInMemory(s);
    s.At({0x00623BA3}, 0, [](Regs&) {
        g_rand = g_rand * 1103515245u + 12345u;
        return (g_rand >> 16) & 0x7FFF;
    });
    s.At({0x00496360}, 0, [](Regs& r) {
        Record({0x6360, Relocate(r.ecx)});
        *reinterpret_cast<uint32_t*>(r.ecx) = 0x00649380;
        return r.ecx;
    });
    StandInThis(s, {0x00494DF0}, 0);
    s.At({0x004D1A90}, 0, [](Regs& r) {
        Record({0x1A90, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x5C, 0x110);
        return r.ecx;
    });
    StandInThis(s, {0x004D2BD0}, 0);
    StandInThis(s, {0x004D23E0}, 0);
    StandInThis(s, {0x004D24D0}, 2);
    s.At({0x004D9CF0, AddressOf(&Nx::BlendModeFromName)}, 0, [](Regs& r) {
        Record({0x9CF0, r.args[0]});
        return r.args[0] * 2654435761u;
    });
    // The texture lookup: none for even checksums, else a texture (its key through a pointer for checksums & 2).
    s.At({0x0049F540}, 4, [](Regs& r) {
        Record({0xF540, r.ecx, r.args[0]});
        if (!(r.args[0] & 1))
            return 0u;
        auto* texture = reinterpret_cast<uint8_t*>(Bump(0x20));
        std::memset(texture, 0, 0x20);
        if (r.args[0] & 2) {
            auto* key = reinterpret_cast<uint32_t*>(Bump(4));
            *key = r.args[0] ^ 0xABCD;
            *reinterpret_cast<uint32_t**>(texture + 0x10) = key;
            texture[8] = 3;
        } else {
            *reinterpret_cast<uint32_t*>(texture + 0x10) = r.args[0] ^ 0x1234;
            texture[8] = 2;
        }
        return reinterpret_cast<uint32_t>(texture);
    });
    s.At({0x004DB570, AddressOf(&Nx::SphereOutsideView)}, 0, [](Regs& r) {
        const auto* centre = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xB570, centre[0], centre[1], centre[2], r.args[1]});
        return centre[0] & 0x100 ? 0u : 1u;
    });
    s.At({0x004DB790, AddressOf(&Nx::SphereInView)}, 0, [](Regs& r) {
        const auto* centre = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xB790, centre[0], centre[1], centre[2], r.args[1]});
        return centre[1] & 0x100 ? 0xFFFFFF00u : 0xFFFFFF01u; // (AL only)
    });
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    // The dynamic vertex buffer: room for a few quads (sometimes plenty).
    s.At({0x005F4740, AddressOf(&Platform::LockDynamicVertices)}, 0, [](Regs& r) {
        Record({0x4740, r.args[0], r.args[1]});
        uint32_t room = g_rand & 1 ? r.args[0] : 4 * (1 + (g_rand >> 3) % 9);
        g_rand = g_rand * 69069u + 1u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = 0xB0FF0000;
        *reinterpret_cast<uint32_t*>(r.args[3]) = g_rand % 1000;
        if (r.args[4])
            *reinterpret_cast<uint32_t*>(r.args[4]) = room;
        return Bump(r.args[1] * (room + 4));
    });
    s.At({0x005F4800, AddressOf(&Platform::UnlockDynamicVertices)}, 0, [](Regs& r) {
        Record({0x4800, r.args[0], r.args[1]});
        return 0u;
    });
    (void)com;
}

// A system's parameters at random (sane floats), its flags from `flags`.
void RandomParams(CPcParticleSystem* system, Lcg& random, uint32_t flags)
{
    for (float& f : system->sizes)
        f = RandomFloat(random, 0.1f, 4);
    for (float& f : system->radii)
        f = RandomFloat(random, 0, 3);
    system->rate = flags & 0x100 ? 0.0f : RandomFloat(random, 5, 90);
    system->lifetime = RandomFloat(random, 0.3f, 2.5f);
    system->midPercent = RandomFloat(random, 5, 95);
    for (Mth::Vector& v : system->positions)
        v = {RandomFloat(random, -40, 40), RandomFloat(random, -40, 40), RandomFloat(random, -40, 40), 1.0f};
    for (Mth::Vector& v : system->boxes)
        v = {RandomFloat(random, 0, 9), RandomFloat(random, 0, 9), RandomFloat(random, 0, 9), 0.0f};
    system->colourMidPercent = RandomFloat(random, 5, 95);
    for (auto& colour : system->colours)
        for (uint8_t& c : colour)
            c = static_cast<uint8_t>(random.Next());
    system->blendMode = random.Next();
    system->alphaCutOff = static_cast<int>(random.Below(300)) - 20;
    system->texture = random.Next();
    system->useMid = flags & 1;
    system->localPositions = flags & 2;
    system->colourUseMid = flags & 4;
    system->cullOutside = flags & 8;
    system->bounds = {RandomFloat(random, -9, 9), RandomFloat(random, -9, 9), RandomFloat(random, -9, 9),
                      RandomFloat(random, 1, 20)};
}

// Systems made, set up (with and without the mid, a texture or not), updated, rendered for frames (the rate changing,
// going to 0 and back, aged or not, culled or not), released, destroyed.
void TestPcParticleSystems()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007D69E8, 4}, {0x007CA964, 4},
                       {0x00700F20, 4}, {0x00786BD8, 0x24}, {0x00684FC8, 4}, {0x00705608, 4}, {0x0067E974, 8}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInParticleCalls(s, com);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("particle systems", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0xA70 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007D69E8) = 0x1D8E0000;
            g_rand = 0x2468 + round;
            Lcg random(0xA700 + round);
            GameVariable<float>(0x00786BD8) = RandomFloat(random, -1, 1);
            GameVariable<float>(0x00786BE8) = round == 5 ? 0.0f : RandomFloat(random, -1, 1);
            GameVariable<float>(0x00786BF8) = round == 5 ? 0.0f : RandomFloat(random, -1, 1);
            if (round == 5)
                GameVariable<float>(0x00786BD8) = 0.0f; // (no length: not normalised)
            GameVariable<float>(0x00684FC8) = RandomFloat(random, 2, 14); // (slow motion: frames of 1/30 - 1/4 s)
            auto* system = Fn<CPcParticleSystem*(__fastcall*)(void*)>(original, 0x004BCE90)(
                reinterpret_cast<void*>(0x3A9A0000));
            uint32_t flags = round < 16 ? static_cast<uint32_t>(round) : random.Next();
            RandomParams(system, random, flags & ~0x100u);
            bool busy = round >= 24 && round < 32, sparse = round >= 32;
            if (busy) // (the rate changing every frame, long lives: all five bursts live)
                system->lifetime = RandomFloat(random, 2.5f, 4);
            if (sparse) { // (a particle a lifetime or less)
                system->rate = RandomFloat(random, 0.2f, 1.6f);
                system->lifetime = RandomFloat(random, 0.3f, 1.2f);
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004BC250)(system);
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004BB0B0)(system) & 0xFF);
            int frames = 6 + static_cast<int>(random.Below(40));
            float stoppedRate = 10.0f;
            for (int frame = 0; frame < frames; ++frame) {
                if (busy)
                    system->rate = RandomFloat(random, 5, 90);
                switch (random.Below(12)) {
                case 0: // stopped, or started again at the rate it stopped at
                    if (system->rate != 0.0f) {
                        stoppedRate = system->rate;
                        system->rate = 0.0f;
                    } else {
                        system->rate = stoppedRate;
                    }
                    break;
                case 1:
                case 2:
                    system->rate = sparse ? RandomFloat(random, 0.2f, 1.6f) : RandomFloat(random, 5, 90);
                    break;
                case 3:
                    system->positions[random.Below(3)].y += 1.0f;
                    Fn<void(__thiscall*)(void*)>(original, 0x004BC240)(system);
                    break;
                case 4: // a burst's seed moved on directly
                    Fn<void(__thiscall*)(void*, int)>(original, 0x004BB040)(
                        system->bursts + random.Below(5), static_cast<int>(random.Below(9)) - 2);
                    trace.push_back(GameVariable<uint32_t>(0x00705608));
                    trace.push_back(GameVariable<uint32_t>(0x0067E974));
                    trace.push_back(GameVariable<uint32_t>(0x0067E978));
                    break;
                case 5:
                    system->bounds.x = RandomFloat(random, -9, 9);
                    system->bounds.y = RandomFloat(random, -9, 9);
                    break;
                default:
                    break;
                }
                Fn<void(__thiscall*)(void*, bool)>(original, 0x004BB0C0)(system, random.Below(5) != 0);
                trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004BB0B0)(system) & 0xFF);
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004BBD20)(system);
            if (round & 1)
                system->bursts = nullptr;
            if (round & 2)
                system->material = nullptr;
            Fn<void(__thiscall*)(void*)>(original, 0x004BCE10)(system);
            trace.push_back(ArenaRelocate(
                Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BCE50)(system, round & 4 ? 1 : 0)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc particles: systems made, set up, updated, rendered for frames, released, destroyed");
}

} // namespace

SELFTEST(Early, TestPcModelSpheres, RemoveStandIns);
SELFTEST(Early, TestPcParticleSystems, RemoveStandIns);
