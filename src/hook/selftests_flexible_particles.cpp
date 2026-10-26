// Self-tests for the PC flexible particle systems (src/game/gfx/pc_flexible_particles.cpp, RFX-11 on): the original
// and ours over stand-in systems (random particles, colours, positions), a recording stand-in device, the dynamic
// vertex buffer, the materials' code, the engine's constructors / destructors and its emitter stood in; the
// vertices, colours, draws and the systems' state compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/pc_flexible_particles.h"
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
using Nx::CPcFlexibleParticles;
using Nx::sFlexibleParticle;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

uint32_t g_room;

void StandInFlexibleCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005BB000, AddressOf(&Mem::Manager::AllocateZeroed)}, 12, [](Regs& r) {
        Record({0xB000, r.args[0], r.args[1], r.args[2]});
        uint32_t at = Bump(r.args[0]);
        if (at)
            std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    s.At({0x004D9B80, AddressOf(&Nx::sSimpleMaterial::Construct)}, 28, [](Regs& r) {
        Record({0x9B80, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        auto* m = reinterpret_cast<uint32_t*>(r.ecx);
        auto* own = reinterpret_cast<uint8_t*>(Bump(0x110)); // (a material: textured for odd texture checksums)
        std::memset(own, 0, 0x110);
        *reinterpret_cast<uint32_t*>(own + 0x30) = r.args[2] & 1 ? 0x7E570000 : 0;
        m[0] = reinterpret_cast<uint32_t>(own);
        m[1] = 0x5AD00000;
        return r.ecx;
    });
    StandInThis(s, {0x004D9CD0, AddressOf(&Nx::sSimpleMaterial::DestroyBody)}, 0);
    StandInThis(s, {0x004D1F00}, 0);
    StandInThis(s, {0x00496FE0}, 1);
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    // The dynamic vertex buffer: room (in vertices) as the test says.
    s.At({0x005F4740, AddressOf(&Platform::LockDynamicVertices)}, 0, [](Regs& r) {
        Record({0x4740, r.args[0], r.args[1]});
        *reinterpret_cast<uint32_t*>(r.args[2]) = 0xB0FF0000;
        *reinterpret_cast<uint32_t*>(r.args[3]) = g_room * 7 % 1000;
        if (r.args[4])
            *reinterpret_cast<uint32_t*>(r.args[4]) = g_room;
        uint32_t room = g_room;
        g_room += 8;
        return Bump(r.args[1] * ((room > r.args[0] ? room : r.args[0]) + 8));
    });
    s.At({0x005F4800, AddressOf(&Platform::UnlockDynamicVertices)}, 0, [](Regs& r) {
        Record({0x4800, r.args[0], r.args[1]});
        return 0u;
    });
}

// A system's shared fields at random: `count` particles, colours, the mid (or none), scales.
void RandomSystem(CPcFlexibleParticles* system, Lcg& random, int count, int round)
{
    system->position = {RandomFloat(random, -30, 30), RandomFloat(random, -30, 30), RandomFloat(random, -30, 30), 1};
    for (float& f : system->colourScale)
        f = RandomFloat(random, 0, 2.5f);
    system->brightness = RandomFloat(random, 0.2f, 1.6f);
    system->emitRate = round % 5 == 0 ? 0.0f : RandomFloat(random, 1, 200);
    system->count = count;
    system->particles = reinterpret_cast<sFlexibleParticle*>(Bump(sizeof(sFlexibleParticle) * (count + 1)));
    for (int i = 0; i < count; ++i) {
        sFlexibleParticle& p = system->particles[i];
        for (int k = 0; k < 2; ++k) {
            p.startSize[k] = RandomFloat(random, 0, 5);
            p.endSize[k] = RandomFloat(random, 0, 5);
        }
        p.lifetime = RandomFloat(random, 0.2f, 3);
        p.age = RandomFloat(random, 0, p.lifetime);
    }
    system->positions = reinterpret_cast<float*>(Bump(12 * (count + 1)));
    for (int i = 0; i < 3 * count; ++i)
        system->positions[i] = RandomFloat(random, -50, 50);
    for (uint32_t** colour : {&system->startColour, &system->midColour, &system->endColour}) {
        *colour = reinterpret_cast<uint32_t*>(Bump(16));
        **colour = random.Next() ^ random.Next() << 8;
    }
    system->colourMid = round % 3 == 0 ? -1.0f : RandomFloat(random, 0.05f, 0.95f);
    if (round == 4)
        system->colourMid = std::numeric_limits<float>::quiet_NaN();
    system->emitterIndependent = random.Below(2) != 0;
}

void SetCamera(Lcg& random, int round)
{
    GameVariable<float>(0x00786BD8) = round == 2 ? 0.0f : RandomFloat(random, -1, 1);
    GameVariable<float>(0x00786BE8) = RandomFloat(random, -1, 1);
    GameVariable<float>(0x00786BF8) = round == 2 ? 0.0f : RandomFloat(random, -1, 1);
}

// The colour helper over many colours and fractions (wrapping channels, scales past 255, NaN).
void TestFlexibleColourLerp()
{
    bool ok = true, reported = false;
    for (int round = 0; round < 8; ++round) {
        ok &= CompareTwoRuns("colour lerp", reported, [&](bool original) {
            Lcg random(0xC010 + round);
            std::vector<uint32_t> trace;
            alignas(16) uint8_t system[0x150] = {};
            auto* s = reinterpret_cast<CPcFlexibleParticles*>(system);
            for (int i = 0; i < 300; ++i) {
                for (float& f : s->colourScale)
                    f = RandomFloat(random, -0.5f, 3);
                s->brightness = i == 7 ? std::numeric_limits<float>::quiet_NaN() : RandomFloat(random, 0, 2);
                uint32_t from = random.Next() ^ random.Next() << 8, to = random.Next() ^ random.Next() << 8, out = 0;
                float t = RandomFloat(random, -0.5f, 1.5f);
                uint32_t result = Fn<uint32_t(__thiscall*)(void*, uint32_t*, const void*, const void*, float)>(
                    original, 0x004BD470)(s, &out, &from, &to, t);
                trace.push_back(out);
                trace.push_back(result == reinterpret_cast<uint32_t>(&out));
            }
            return trace;
        });
    }
    Check(ok, "flexible particles: colours lerped and scaled");
}

// The factory: every type (and an unknown one), memory and none.
void TestFlexibleFactory()
{
    StandIns& s = *new StandIns();
    StandInMemory(s);
    s.At({0x005BB000, AddressOf(&Mem::Manager::AllocateZeroed)}, 12, [](Regs& r) {
        Record({0xB000, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    const uint32_t constructors[][2] = {{0x004BED10, 7}, {0x004C0EB0, 8}, {0x004C3D80, 7}, {0x0049A0C0, 8},
                                        {0x004C48C0, 7}, {0x004C09F0, 7}, {0x004BDC80, 8}, {0x004C2B30, 7},
                                        {0x004BF720, 8}, {0x004C3550, 8}, {0x004C1720, 8}, {0x004C21A0, 5},
                                        {0x004BD3A0, 7}};
    for (auto& c : constructors) {
        uint32_t address = c[0], args = c[1];
        s.At({address}, static_cast<uint16_t>(4 * args), [address, args](Regs& r) {
            Record({address, Relocate(r.ecx)});
            for (uint32_t i = 0; i < args; ++i)
                Record({r.args[i]});
            return r.ecx;
        });
    }
    g_relocate = ArenaRelocate;
    const uint32_t types[] = {0x15834EEA, 0x0EE6FC5B, 0x097CB7A9, 0x286A8D26, 0x3624A5EB, 0x2EEB4B09, 0x3D56A7A6,
                              0x8ADDAC1F, 0x7EC7252D, 0x3F109FCC, 0xC4D5A4CB, 0xF4D8D486, 0xDEDFC057, 0x12345678,
                              0x00000000, 0xFFFFFFFF};
    GameMemory memory({{0x00658555, 1}});
    bool ok = true, reported = false;
    for (int round = 0; round < 2; ++round) {
        ok &= CompareTwoRuns("flexible factory", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0);
            GameVariable<uint8_t>(0x00658555) = static_cast<uint8_t>(0x40 + round);
            std::vector<uint32_t> trace;
            for (uint32_t type : types) {
                g_failAllocations = {round == 1 && (type & 4)};
                trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t, uint32_t, int, int, uint32_t, uint32_t,
                                                                    int, int, float, int, const char*)>(
                    original, 0x004BCEF0)(0x11, type, 22, 33, 0x44, 0x55, 66, 77, 8.5f, 99,
                                          reinterpret_cast<const char*>(0x00ABCDEF))));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "flexible particles: the factory's types and their parameters");
}

// newflat systems: constructed (with and without memory for the material), rendered (the vertex buffer's room
// big, small - the original's early stop -, the mid colour or not, emitting), destroyed both ways.
void TestNewFlatParticles()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007D69E8, 4}, {0x007CA964, 4},
                       {0x00786BD8, 0x24}, {0x00786EBC, 8}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInFlexibleCalls(s);
    s.At({0x00499010}, 32, [](Regs& r) {
        Record({0x9010, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5],
                r.args[6], r.args[7]});
        return r.ecx;
    });
    StandInThis(s, {0x00498FB0}, 0);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("newflat", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0xF1A0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007D69E8) = 0x1D8E0000;
            Lcg random(0xF1A00 + round);
            SetCamera(random, round);
            g_room = round % 4 == 1 ? 4 + 4 * random.Below(4) : 400 + random.Below(200);
            auto* system = reinterpret_cast<Nx::CPcNewFlatParticles*>(Bump(sizeof(Nx::CPcNewFlatParticles)));
            g_failAllocations = {round == 6}; // (the material's)
            Fn<void*(__thiscall*)(void*, uint32_t, int, uint32_t, uint32_t, int, int, float)>(original, 0x004BD3A0)(
                system, 0xF1A7, 50 + round, 0x7E57 + round, 0xB1E, round * 9, 3, 1.25f);
            std::vector<uint32_t> made(reinterpret_cast<uint32_t*>(system), reinterpret_cast<uint32_t*>(system) + 4);
            if (round == 6) {
                system->material = reinterpret_cast<Nx::sSimpleMaterial*>(Bump(8));
                std::memset(system->material, 0x3C, 8);
            }
            int count = round == 3 ? 0 : round == 7 ? 1 : 1 + static_cast<int>(random.Below(round % 4 == 1 ? 12 : 60));
            RandomSystem(system, random, count, round);
            system->emitted = RandomFloat(random, 0, round % 5 == 0 ? 2.0f : 1.0f);
            int frames = 1 + static_cast<int>(random.Below(4));
            for (int frame = 0; frame < frames; ++frame)
                Fn<void(__thiscall*)(void*)>(original, 0x004BD680)(system);
            if (round == 5)
                system->material = nullptr;
            uint32_t result = round & 1 ? Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BDC40)(system, round & 2 ? 1 : 0)
                                        : Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BD390)(
                                              reinterpret_cast<uint8_t*>(system) + 0xC, round & 2 ? 1 : 0);
            std::vector<uint32_t> trace = g_trace;
            trace.insert(trace.end(), made.begin(), made.end());
            trace.push_back(ArenaRelocate(result));
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "flexible particles: newflat made, rendered, emitting, destroyed");
}

// Flat ribbons (0x3D56A7A6): one segment (no ribbon) to many (several draws, the 0x7FF-byte limit), the buffer's room
// small and large, colours per segment, the mid colour or none, emitter independent or not, both destroys.
void TestFlatRibbonParticles()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007D69E8, 4}, {0x007CA964, 4},
                       {0x007CA95C, 4}, {0x00786BD8, 0x24}, {0x00786EBC, 8}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInFlexibleCalls(s);
    s.At({0x004992C0}, 32, [](Regs& r) {
        Record({0x92C0, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5],
                r.args[6], r.args[7]});
        return r.ecx;
    });
    StandInThis(s, {0x00499200}, 0);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("flat ribbon", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0xF1B0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007D69E8) = 0x1D8E0000;
            Lcg random(0xF1B00 + round);
            SetCamera(random, round);
            g_room = round % 4 == 1 ? 8 + 4 * random.Below(6) : 900 + random.Below(400);
            auto* system = reinterpret_cast<Nx::CPcFlatRibbonParticles*>(Bump(sizeof(Nx::CPcFlatRibbonParticles)));
            int segments = round == 0 ? 1 : 2 + static_cast<int>(random.Below(round % 3 == 2 ? 40 : 6));
            g_failAllocations = {round == 6};
            Fn<void*(__thiscall*)(void*, uint32_t, int, uint32_t, uint32_t, int, int, float, int)>(original, 0x004BDC80)(
                system, 0xF1B7, 40 + round, 0x7E57 + round, 0xB1E, round * 9, segments, RandomFloat(random, 0.1f, 3),
                round + 2);
            std::vector<uint32_t> made(reinterpret_cast<uint32_t*>(system), reinterpret_cast<uint32_t*>(system) + 4);
            made.push_back(std::bit_cast<uint32_t>(system->width));
            if (round == 6) {
                system->material = reinterpret_cast<Nx::sSimpleMaterial*>(Bump(8));
                std::memset(system->material, 0x3C, 8);
            }
            int count = round == 3 ? 0 : round == 7 ? 1 : 1 + static_cast<int>(random.Below(round % 4 == 1 ? 8 : 30));
            RandomSystem(system, random, count, round);
            system->segments = segments;
            system->historyStride = count + static_cast<int>(random.Below(4));
            int places = system->historyStride * segments + count + 1;
            system->positions = reinterpret_cast<float*>(Bump(12 * places));
            for (int i = 0; i < 3 * places; ++i)
                system->positions[i] = RandomFloat(random, -50, 50);
            if (round == 9) // (two points the same: a segment with no direction)
                std::memcpy(system->positions + 3 * system->historyStride, system->positions, 12);
            for (uint32_t** colour : {&system->startColour, &system->midColour, &system->endColour}) {
                *colour = reinterpret_cast<uint32_t*>(Bump(4 * segments + 4));
                for (int k = 0; k < segments; ++k)
                    (*colour)[k] = random.Next() ^ random.Next() << 8;
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004BDDD0)(system);
            if (round == 5)
                system->material = nullptr;
            uint32_t result = round & 1 ? Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BECD0)(
                                              system, round & 2 ? 1 : 0)
                                        : Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BDC70)(
                                              reinterpret_cast<uint8_t*>(system) + 0xC, round & 2 ? 1 : 0);
            std::vector<uint32_t> trace = g_trace;
            trace.insert(trace.end(), made.begin(), made.end());
            trace.push_back(ArenaRelocate(result));
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "flexible particles: flat ribbons made, rendered (ribbons and heads), destroyed");
}

// Glows: one segment to many, several draws (the 0x7FF-word limit), rendering skipped, the mid colour or none,
// emitter independent or not, both destroys. The drawn vertices compared (all written: twelve per triangle counted).
void TestGlowParticles()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007CA95C, 4}, {0x00786BD8, 0x24},
                       {0x00786EBC, 8}, {0x007D1EC0, 1}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInFlexibleCalls(s);
    com.On(kDevice, kDrawPrimitiveUP, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), r.args[2] * 12 * r.args[4])});
        return 0u;
    });
    s.At({0x004995B0}, 32, [](Regs& r) {
        Record({0x95B0, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5],
                r.args[6], r.args[7]});
        return r.ecx;
    });
    StandInThis(s, {0x00499550}, 0);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("glow", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x6100 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint8_t>(0x007D1EC0) = round == 8;
            Lcg random(0x61000 + round);
            SetCamera(random, round);
            auto* system = reinterpret_cast<Nx::CPcGlowParticles*>(Bump(sizeof(Nx::CPcGlowParticles)));
            int segments = round == 0 ? 1 : 2 + static_cast<int>(random.Below(round % 3 == 2 ? 20 : 8));
            g_failAllocations = {round == 6};
            Fn<void*(__thiscall*)(void*, uint32_t, int, uint32_t, uint32_t, int, int, float)>(original, 0x004BED10)(
                system, 0x6107, 40 + round, 0x7E57 + round, 0xB1E, round * 9, segments, 2.5f);
            std::vector<uint32_t> made(reinterpret_cast<uint32_t*>(system), reinterpret_cast<uint32_t*>(system) + 4);
            if (round == 6) {
                system->material = reinterpret_cast<Nx::sSimpleMaterial*>(Bump(8));
                std::memset(system->material, 0x3C, 8);
            }
            int count = round == 3 ? 0 : round == 7 ? 1 : 1 + static_cast<int>(random.Below(25));
            RandomSystem(system, random, count, round);
            system->segments = segments;
            system->glowWidth = RandomFloat(random, 1, 4);
            for (uint32_t** colour : {&system->startColour, &system->midColour, &system->endColour}) {
                *colour = reinterpret_cast<uint32_t*>(Bump(16));
                for (int k = 0; k < 3; ++k)
                    (*colour)[k] = random.Next() ^ random.Next() << 8;
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004BEE50)(system);
            if (round == 5)
                system->material = nullptr;
            uint32_t result = round & 1 ? Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BF6E0)(
                                              system, round & 2 ? 1 : 0)
                                        : Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004BED00)(
                                              reinterpret_cast<uint8_t*>(system) + 0xC, round & 2 ? 1 : 0);
            std::vector<uint32_t> trace = g_trace;
            trace.insert(trace.end(), made.begin(), made.end());
            trace.push_back(ArenaRelocate(result));
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "flexible particles: glows made, rendered, destroyed");
}

// The other types, each through the same rounds: constructed (no memory for the material once), set up at random
// (counts, history, segments, widths, colour arrays, alignment - also exactly along y -, random UV quadrants, a
// textured material or not), rendered with the buffer's room small and large, destroyed both ways.
struct sFlexibleType {
    const char* name;
    uint32_t size, construct, constructArgs, engineConstruct, engineDestroy, render, destroy, destroySecondary;
    int segmentsAt; // (0: none)
    int widthAt;    // (0: none)
    int maxSegments;
    int materialAt;
    int historyAt = 0; // (0: none)
    int stepAt = 0;
};

void TestFlexibleType(const sFlexibleType& type)
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007D69E8, 4}, {0x007CA964, 4},
                       {0x007CA95C, 4}, {0x00786BD8, 0x24}, {0x00786EBC, 8}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInFlexibleCalls(s);
    uint32_t engineArgs = 8;
    s.At({type.engineConstruct}, static_cast<uint16_t>(4 * engineArgs), [engineArgs](Regs& r) {
        Record({0xE9C0, Relocate(r.ecx)});
        for (uint32_t i = 0; i < engineArgs; ++i)
            Record({r.args[i]});
        return r.ecx;
    });
    StandInThis(s, {type.engineDestroy}, 0);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns(type.name, reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x7700 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007D69E8) = 0x1D8E0000;
            Lcg random(0x77000 + round + type.render);
            SetCamera(random, round);
            g_room = round % 4 == 1 ? 8 + 4 * random.Below(8) : 2000 + random.Below(400);
            auto* system = reinterpret_cast<CPcFlexibleParticles*>(Bump(type.size));
            int segments = round == 0 ? 1 : 2 + static_cast<int>(random.Below(type.maxSegments - 1));
            g_failAllocations = {round == 6};
            uint32_t texture = 0x7E57 + round;
            uint32_t args[8] = {0xF1C0u + round, 30u + round, texture, 0xB1E, static_cast<uint32_t>(round * 9),
                                static_cast<uint32_t>(segments), std::bit_cast<uint32_t>(RandomFloat(random, 0.5f, 3)),
                                static_cast<uint32_t>(round + 2)};
            if (type.constructArgs == 5)
                Fn<void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(original,
                                                                                         type.construct)(
                    system, args[0], args[1], args[2], args[3], args[4]);
            else if (type.constructArgs == 7)
                Fn<void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
                    original, type.construct)(system, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
            else
                Fn<void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                      uint32_t)>(original, type.construct)(system, args[0], args[1], args[2], args[3],
                                                                           args[4], args[5], args[6], args[7]);
            std::vector<uint32_t> made(reinterpret_cast<uint32_t*>(system),
                                       reinterpret_cast<uint32_t*>(system) + type.size / 4);
            auto* bytes = reinterpret_cast<uint8_t*>(system);
            auto*& material = *reinterpret_cast<Nx::sSimpleMaterial**>(bytes + type.materialAt);
            if (round == 6 || !material) {
                material = reinterpret_cast<Nx::sSimpleMaterial*>(Bump(8));
                auto* own = reinterpret_cast<uint8_t*>(Bump(0x110));
                std::memset(own, 0, 0x110);
                material->material = own;
                material->shader = reinterpret_cast<void*>(0x5AD00000);
            }
            int count = round == 3 ? 0 : round == 7 ? 1 : 1 + static_cast<int>(random.Below(round % 4 == 1 ? 6 : round % 4 == 2 ? 80 : 20));
            RandomSystem(system, random, count, round);
            if (type.segmentsAt)
                *reinterpret_cast<int*>(bytes + type.segmentsAt) = segments;
            if (type.widthAt)
                *reinterpret_cast<float*>(bytes + type.widthAt) = RandomFloat(random, 0.5f, 4);
            int history = segments;
            if (type.historyAt) {
                history = round == 11 ? 1 : 1 + static_cast<int>(random.Below(10));
                *reinterpret_cast<int*>(bytes + type.historyAt) = history;
                segments = 1 + static_cast<int>(random.Below(8));
                *reinterpret_cast<int*>(bytes + type.segmentsAt) = segments;
            }
            if (type.stepAt)
                *reinterpret_cast<float*>(bytes + type.stepAt) = RandomFloat(random, 10, 90);
            system->historyStride = count + static_cast<int>(random.Below(4));
            int places = system->historyStride * ((history > segments ? history : segments) + 1) + count + 1;
            system->positions = reinterpret_cast<float*>(Bump(12 * places));
            for (int i = 0; i < 3 * places; ++i)
                system->positions[i] = RandomFloat(random, -50, 50);
            if (round == 9)
                std::memcpy(system->positions + 3 * system->historyStride, system->positions, 12);
            int entries = (segments > 4 ? segments : 4) + history + 4;
            for (uint32_t** colour : {&system->startColour, &system->midColour, &system->endColour}) {
                *colour = reinterpret_cast<uint32_t*>(Bump(4 * entries + 4));
                for (int k = 0; k < entries; ++k)
                    (*colour)[k] = random.Next() ^ random.Next() << 8;
            }
            system->flags = random.Below(2) ? 0x4000 : 0;
            system->aligned = round % 3 == 1;
            system->alignTo = round == 10 ? Mth::Vector{0, 1, 0, 0}
                                          : Mth::Vector{RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
                                                        RandomFloat(random, -1, 1), 0};
            Fn<void(__thiscall*)(void*)>(original, type.render)(system);
            if (round == 5)
                material = nullptr;
            uint32_t result = round & 1 ? Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, type.destroy)(
                                              system, round & 2 ? 1 : 0)
                                        : Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, type.destroySecondary)(
                                              bytes + 0xC, round & 2 ? 1 : 0);
            std::vector<uint32_t> trace = g_trace;
            trace.insert(trace.end(), made.begin(), made.end());
            trace.push_back(ArenaRelocate(result));
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, type.name);
}

void TestFlexibleTypes()
{
    static const sFlexibleType kTypes[] = {
        {"flexible particles: lines", 0x148, 0x004C09F0, 7, 0x00499B80, 0x00499B20, 0x004C0B30, 0x004C0E70, 0x004C09E0, 0,
         0, 6, 0x140},
        {"flexible particles: ribbons", 0x148, 0x004C0EB0, 8, 0x0049B0D0, 0x0049B070, 0x004C0FF0, 0x004C16E0, 0x004C0EA0,
         0x134, 0, 8, 0x140},
        {"flexible particles: ribbon trails", 0x148, 0x004C1720, 8, 0x0049B380, 0x0049B320, 0x004C1860, 0x004C2160,
         0x004C1710, 0x134, 0, 30, 0x140},
        {"flexible particles: shaded", 0x138, 0x004C21A0, 5, 0x0049B660, 0x0049B600, 0x004C22D0, 0x004C2AF0, 0x004C2190,
         0, 0, 6, 0x134},
        {"flexible particles: smooth", 0x148, 0x004C2B30, 7, 0x0049B910, 0x0049B8A0, 0x004C2C70, 0x004C3500, 0x004C2B20,
         0, 0, 6, 0x140},
        {"flexible particles: smooth ribbons", 0x140, 0x004C3550, 8, 0x0049BBD0, 0x0049BB50, 0x004C3690, 0x004C3D40,
         0x004C3540, 0x138, 0, 8, 0x13C},
        {"flexible particles: smooth stars", 0x150, 0x004C3D80, 7, 0x0049BE80, 0x0049BE20, 0x004C3EC0, 0x004C4880,
         0x004C3D70, 0x134, 0x138, 12, 0x148},
        {"flexible particles: stars", 0x150, 0x004C48C0, 7, 0x0049C140, 0x0049C0D0, 0x004C4A00, 0x004C5200, 0x004C48B0,
         0x134, 0x138, 12, 0x148},
        {"flexible particles: glow ribbon trails", 0x150, 0x004BF720, 8, 0x00499880, 0x00499800, 0x004BF7F0, 0x004C09B0,
         0x004BF710, 0x134, 0x138, 8, 0x148, 0x118, 0x14C},
    };
    for (const sFlexibleType& type : kTypes)
        TestFlexibleType(type);
    // The smooth ribbon's segment count getter.
    alignas(16) uint8_t system[0x140] = {};
    *reinterpret_cast<int*>(system + 0x138) = 0x1234;
    Check(Fn<int(__thiscall*)(void*)>(true, 0x004C3530)(system) == Fn<int(__thiscall*)(void*)>(false, 0x004C3530)(system),
          "flexible particles: smooth ribbons' segments");
}

} // namespace

SELFTEST(Early, TestFlexibleColourLerp, RemoveStandIns);
SELFTEST(Early, TestFlexibleFactory, RemoveStandIns);
SELFTEST(Early, TestNewFlatParticles, RemoveStandIns);
SELFTEST(Early, TestFlatRibbonParticles, RemoveStandIns);
SELFTEST(Early, TestGlowParticles, RemoveStandIns);
SELFTEST(Early, TestFlexibleTypes, RemoveStandIns);
