// Self-tests for the model particle type (src/game/gfx/model_particles.cpp): its constructor and Render, original and
// ours over a system in an arena - memory, rand(), the models' calls and the file name stood in; the calls, results
// and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/model.h"
#include "game/gfx/particle_system.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CModelParticles;

uint32_t FloatBits(float f) { return std::bit_cast<uint32_t>(f); }

Lcg* g_rand;
int g_modelsLeft;        // NewModel gives null after this many
int g_geomsLeft;         // AddFileGeom fails after this many
unsigned int g_memoryFree; // MallocAvailable

void StandInModelCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005BB000, AddressOf(&Mem::Manager::AllocateZeroed)}, 12, [](Regs& r) {
        Record({0xB000, r.args[0], r.args[1], r.args[2]});
        uint32_t at = r.args[0] >= 0x80000000u ? 0 : Bump(r.args[0]);
        if (at)
            std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    s.At({0x00623BA3}, 0, [](Regs&) { return g_rand->Next() & 0x7FFF; });
    s.At({0x005BBB10, AddressOf(&Mem::MallocAvailable)}, 0, [](Regs&) {
        Record({0xBB10});
        return g_memoryFree;
    });
    s.At({0x0048BFA0}, 0, [](Regs& r) {
        Record({0xBFA0, r.args[0] & 0xFF});
        if (g_modelsLeft-- <= 0)
            return 0u;
        return Bump(0x50);
    });
    s.At({0x00487A70}, 0, [](Regs& r) { // name + ext into out (an upper-case extension: the lower-casing shows)
        Record({0x7A70, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        std::snprintf(reinterpret_cast<char*>(r.args[2]), 512, "%s.MDL", reinterpret_cast<const char*>(r.args[0]));
        return 0u;
    });
    StandInThis(s, {0x00494BA0, AddressOf(&Nx::CModel::ClearGeoms)}, 0);
    s.At({0x00492EE0, AddressOf(&Nx::CModel::SetScale)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x2EE0, Relocate(r.ecx), v[0], v[1], v[2], v[3]});
        return 1u;
    });
    s.At({0x004935E0, AddressOf(&Nx::CModel::AddFileGeom)}, 32, [](Regs& r) {
        Record({0x35E0, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        for (int i = 1; i < 8; ++i)
            Record({r.args[i] & 0xFF});
        return g_geomsLeft-- > 0 ? 1u : 0u;
    });
    s.At({0x00493C60, AddressOf(&Nx::CModel::SetActive)}, 4, [](Regs& r) {
        Record({0x3C60, Relocate(r.ecx), r.args[0] & 0xFF});
        return 1u;
    });
    s.At({0x004948D0, AddressOf(&Nx::CModel::SetColor)}, 16, [](Regs& r) {
        Record({0x48D0, Relocate(r.ecx), r.args[0] & 0xFF, r.args[1] & 0xFF, r.args[2] & 0xFF, r.args[3] & 0xFF});
        return 1u;
    });
    s.At({0x00493090, AddressOf(&Nx::CModel::Render)}, 12, [](Regs& r) {
        Record({0x3090, Relocate(r.ecx), r.args[1] & 0xFF, r.args[2]});
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            Record({m[i]});
        return 1u;
    });
    StandInThis(s, {0x0049AD60}, 1);
}

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// Constructed (the memory and models there or failing at each step, little memory left, a geom failing), then drawn
// several times with random particles (none, fewer than the models, all; ages before, at and after the mid colour; no
// mid colour, NaN; velocities turning the rotations, zero rotations).
void TestModelParticles()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00658555, 1}, {0x006FA7C0, 4}});
    StandIns& s = *new StandIns();
    StandInModelCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("model particles", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            GameVariable<uint32_t>(0x006FA7C0) = 0x3A9B0000;
            GameVariable<uint8_t>(0x00658555) = static_cast<uint8_t>(round & 1);
            Lcg random(0x7300 + round);
            Lcg rands(0x1100 + round);
            g_rand = &rands;
            std::vector<uint32_t> trace;
            int maxParticles = static_cast<int>(random.Below(7));
            g_modelsLeft = round % 9 == 2 ? static_cast<int>(random.Below(4)) : 100;
            g_geomsLeft = round % 9 == 5 ? static_cast<int>(random.Below(4)) : 100;
            g_memoryFree = round % 13 == 7 ? static_cast<unsigned int>(maxParticles) * 0x1194 - 1
                                           : static_cast<unsigned int>(maxParticles) * 0x1194 + random.Below(2);
            auto* system = reinterpret_cast<CModelParticles*>(Bump(0x148));
            // (the models come from the same stand-in arena: 7 allocations and one a model)
            int failAt = round % 4 == 1 ? 1 + static_cast<int>(random.Below(7 + static_cast<uint32_t>(maxParticles)))
                                        : 0;
            for (int i = 1; i <= failAt; ++i)
                g_failAllocations.push_back(i == failAt);
            trace.push_back(ArenaRelocate(
                Fn<uint32_t(__thiscall*)(void*, uint32_t, int, const char*, uint32_t, int, int, float, int)>(
                    original, 0x0049A0C0)(system, random.Next(), maxParticles, "Models\\Junk\\Can", random.Next(),
                                          1, 2, 3.0f, 4)));
            g_failAllocations.clear();
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            if (!(system->m_flags & 4u))
                return trace;
            // drawn a few times with particles
            for (int frame = 0; frame < 4; ++frame) {
                g_trace.clear();
                int count = static_cast<int>(random.Below(static_cast<uint32_t>(maxParticles) + 1));
                if (frame == 3 && random.Below(2))
                    count = maxParticles;
                system->m_unknown8C = count;
                for (uint32_t* colour : {reinterpret_cast<uint32_t*>(system->mp_startColour),
                                         reinterpret_cast<uint32_t*>(system->mp_midColour),
                                         reinterpret_cast<uint32_t*>(system->mp_endColour)})
                    *colour = random.Next() ^ (random.Next() << 8);
                static const float kMids[] = {-1.0f, 0.5f, 0.25f, 0.0f, 0.999f};
                system->m_colourMid = random.Below(9) == 0 ? std::numeric_limits<float>::quiet_NaN()
                                                           : kMids[random.Below(5)];
                for (float& f : system->m_colourScale)
                    f = RandomFloat(random, 0.0f, 2.0f);
                system->m_unknownEC = FloatBits(RandomFloat(random, 0.5f, 2.0f));
                auto* rotations = static_cast<float*>(system->mp_unknown13C);
                for (int p = 0; p < maxParticles; ++p) {
                    Nx::sFlexibleParticle& particle = system->mp_particles[p];
                    for (float& v : particle.velocity)
                        v = random.Below(4) ? RandomFloat(random, -3.0f, 3.0f) : 0.0f;
                    particle.startSize[0] = RandomFloat(random, 0.0f, 4.0f);
                    particle.lifetime = RandomFloat(random, 0.5f, 3.0f);
                    particle.age = random.Below(5) == 0 ? particle.lifetime * system->m_colourMid
                                                        : RandomFloat(random, 0.0f, particle.lifetime);
                    for (int k = 0; k < 3; ++k)
                        system->mp_positions[p * 3 + k] = RandomFloat(random, -500.0f, 500.0f);
                    if (random.Below(6) == 0) // (a rotation of 0 and no velocity: left as it is)
                        for (int k = 0; k < 4; ++k) {
                            rotations[p * 4 + k] = 0.0f;
                            particle.velocity[k < 3 ? k : 0] = 0.0f;
                        }
                }
                Fn<void(__thiscall*)(void*)>(original, 0x0049A4C0)(system);
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                DumpArena(trace);
            }
            memory.Dump(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "model particles: constructed (each step failing in turn), drawn with their colours, turns and sizes");
}

} // namespace

SELFTEST(Early, TestModelParticles, RemoveStandIns);
