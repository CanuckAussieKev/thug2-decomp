// Self-tests for the engine's particle system's own members and its vector helpers (src/game/gfx/engine_particles.cpp,
// particle_maths.cpp, RND1-4): the original and ours over a stand-in system (a recording vtable, the script kill
// stood in), and the register helpers called through inline assembly with awkward values (NaN, signalling NaN,
// equal components, a turning point at zero, equal times) - results, registers kept and the arena compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/particle_maths.h"
#include "game/gfx/pc_particles.h"
#include "game/script/cscript.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcParticleSystem;

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }
float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}
uint32_t Awkward(Lcg& random, float low, float high)
{
    switch (random.Below(10)) {
    case 0:
        return 0x7FC00000u;
    case 1:
        return 0x7F800001u; // (signalling)
    case 2:
        return 0;
    case 3:
        return 0x80000000u;
    default:
        return std::bit_cast<uint32_t>(RandomFloat(random, low, high));
    }
}

void* g_vtable[8];

// The members, in a random order with random arguments.
void TestParticleSystemMembers()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    const uint16_t pops[8] = {4, 0, 0, 4, 4, 0, 0, 0};
    for (int slot = 0; slot < 8; ++slot) {
        uint16_t pop = pops[slot];
        g_vtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            // (bools: the original passes its caller's whole word on)
            Record({0xA000u + slot, Relocate(r.ecx), pop ? r.args[0] & 0xFF : 0u});
            return 0u;
        });
    }
    s.At({0x00474040, AddressOf(&Script::KillSpawnedScriptsWithId)}, 0, [](Regs& r) {
        Record({0x4040, r.args[0]});
        return 0u;
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("particle members", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            Lcg random(0x9A70 + round);
            auto* system = reinterpret_cast<CPcParticleSystem*>(Block(sizeof(CPcParticleSystem)));
            auto* bytes = reinterpret_cast<uint8_t*>(system);
            for (int step = 0; step < 20; ++step) {
                system->vtable = g_vtable;
                bytes[0x159] = static_cast<uint8_t>(random.Pick({0u, 1u, 0u, 2u}));
                bytes[0x15A] = static_cast<uint8_t>(random.Pick({0u, 0u, 1u}));
                *reinterpret_cast<uint32_t*>(bytes + 0x4C) = random.Below(2) ? 0 : random.Next();
                uint32_t which = random.Below(3);
                switch (random.Below(12)) {
                case 0:
                    Fn<void(__thiscall*)(void*)>(original, 0x00494DF0)(system);
                    break;
                case 1:
                    Record({Relocate(
                        Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00495230)(system, random.Below(4)))});
                    break;
                case 2:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00494E00)(system, random.Next());
                    break;
                case 3:
                    Fn<void(__thiscall*)(void*)>(original, 0x00494E20)(system);
                    break;
                case 4:
                    Fn<void(__thiscall*)(void*)>(original, 0x00494E40)(system);
                    break;
                case 5:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00494E70)(system, random.Pick({0u, 1u, 0x100u}));
                    break;
                case 6:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00494E90)(system, Awkward(random, 0, 90));
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00494EA0)(system, Awkward(random, 0, 9));
                    break;
                case 7:
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x00494EB0)(system, which,
                                                                                        Awkward(random, 0, 9));
                    break;
                case 8: {
                    uint32_t colour = random.Next() ^ (random.Next() << 8);
                    Fn<void(__thiscall*)(void*, uint32_t, const uint32_t*)>(original, 0x00494EC0)(system, which,
                                                                                                  &colour);
                    break;
                }
                case 9:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00494EE0)(system, Awkward(random, -9, 9));
                    break;
                case 10:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00494EF0)(system, Awkward(random, 0, 100));
                    break;
                default:
                    break;
                }
                Record({bytes[0x159], *reinterpret_cast<uint32_t*>(bytes + 0x4C)});
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "particle system: the engine's members (destroy, render / update unless hidden, release, hide, rate, "
              "lifetime, size, colour, +0x38, colour mid)");
}

// The register helpers.
uint32_t CallQuadratic(void* code, Mth::Vector* target, const Nx::sQuadraticPoints* points, uint32_t* keptEcx,
                       uint32_t* keptEdx)
{
    uint32_t result, ecxAfter, edxAfter;
    __asm {
        mov esi, points
        sub esp, 56
        mov edi, esp
        mov ecx, 14
        rep movsd
        mov ecx, target
        mov edx, 0x0BADF00D
        call code
        add esp, 56
        mov result, eax
        mov ecxAfter, ecx
        mov edxAfter, edx
    }
    *keptEcx = ecxAfter;
    *keptEdx = edxAfter;
    return result;
}
uint32_t CallMinMax(void* code, Mth::Vector* target, const Mth::Vector* a, const Mth::Vector* b, uint32_t* keptEsi)
{
    uint32_t result, esiAfter;
    __asm {
        push esi
        mov esi, target
        mov ecx, a
        mov edx, b
        call code
        mov result, eax
        mov esiAfter, esi
        pop esi
    }
    *keptEsi = esiAfter;
    return result;
}

void TestParticleMaths()
{
    bool ok = true, reported = false;
    for (int round = 0; round < 400; ++round) {
        ok &= CompareTwoRuns("particle maths", reported, [&](bool original) {
            std::vector<uint32_t> trace;
            Lcg random(0x3A70 + round);
            Nx::sQuadraticPoints points;
            for (Mth::Vector* v : {&points.start, &points.p1, &points.p2}) {
                for (int k = 0; k < 4; ++k) {
                    uint32_t bits = random.Below(12) ? std::bit_cast<uint32_t>(RandomFloat(random, -50, 50))
                                                     : Awkward(random, -50, 50);
                    std::memcpy(&v->x + k, &bits, 4);
                }
            }
            if (random.Below(4) == 0)
                points.p1 = points.p2; // (no curve)
            if (random.Below(8) == 0)
                points.p1 = points.p2 = points.start; // (no offsets: a t^2 coefficient of 0)
            points.t1 = RandomFloat(random, 0.05f, 0.95f);
            points.t2 = random.Below(8) ? RandomFloat(random, 0.5f, 3) : points.t1;
            Mth::Vector out = {7, 7, 7, 7};
            uint32_t ecxAfter = 0, edxAfter = 0;
            void* code = original ? OriginalFunction(0x00494F10) : reinterpret_cast<void*>(&Nx::QuadraticLowestStub);
            uint32_t result = CallQuadratic(code, &out, &points, &ecxAfter, &edxAfter);
            trace.push_back(result == reinterpret_cast<uint32_t>(&out));
            trace.push_back(ecxAfter == reinterpret_cast<uint32_t>(&out));
            trace.push_back(edxAfter);
            for (int k = 0; k < 4; ++k)
            {
                float value = (&out.x)[k];
                // (any NaN as one: the payload of a computed NaN differs between the x87 and SSE)
                trace.push_back(value != value ? 0x7FC00000u : std::bit_cast<uint32_t>(value));
            }
            // The smaller / larger: random, NaN and equal components, out the same as a.
            Mth::Vector a, b;
            for (int k = 0; k < 4; ++k) {
                uint32_t bitsA = Awkward(random, -5, 5);
                uint32_t bitsB = random.Below(4) ? Awkward(random, -5, 5) : bitsA;
                std::memcpy(&a.x + k, &bitsA, 4);
                std::memcpy(&b.x + k, &bitsB, 4);
            }
            for (uint32_t address : {0x00495130u, 0x004951B0u}) {
                void* helper = original ? OriginalFunction(address)
                                        : reinterpret_cast<void*>(address == 0x00495130u ? &Nx::MinVectorStub
                                                                                         : &Nx::MaxVectorStub);
                Mth::Vector into = {9, 9, 9, 9};
                Mth::Vector copy;
                std::memcpy(&copy, &a, sizeof(copy));
                bool alias = random.Below(4) == 0;
                Mth::Vector* target = alias ? &copy : &into;
                uint32_t esiAfter = 0;
                uint32_t r = CallMinMax(helper, target, &copy, &b, &esiAfter);
                trace.push_back(r == reinterpret_cast<uint32_t>(target));
                trace.push_back(esiAfter == reinterpret_cast<uint32_t>(target));
                for (int k = 0; k < 4; ++k)
                    trace.push_back(std::bit_cast<uint32_t>((&target->x)[k]));
            }
            return trace;
        });
    }
    Check(ok, "particle maths: a quadratic path's lowest point, componentwise min / max (registers, NaN, aliasing)");
}

} // namespace

SELFTEST(Early, TestParticleSystemMembers, RemoveStandIns);
SELFTEST(Early, TestParticleMaths);
