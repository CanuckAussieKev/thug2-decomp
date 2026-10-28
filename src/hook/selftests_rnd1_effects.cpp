// Self-tests for the engine's effects (src/game/gfx/engine_effects.cpp, RND1-3): the original and ours over stand-in
// tables (the hash table's iteration, flush and put stood in by a list of their own), the PC code they pass on to
// stood in and recorded, the frame time and rand() scripted - fog, the level colour, shatter settings, the vertex
// stack, the splat parameters, the engine's shatter, texture splats aged / lit / killed, flashes and shatters updated
// and drawn, the tables made and everything killed.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "game/gfx/engine_effects.h"
#include "game/gfx/flash.h"
#include "game/gfx/pc_fog.h"
#include "game/gfx/shatter.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/texture_splats.h"
#include "game/lst/hashtable.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/rnd1_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd1Tests;

void StandInEffects(StandIns& s)
{
    Rnd1Tests::StandInTables(s);
    // D3DX's matrix product (stdcall: out, a, b): out = a * b, recorded.
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        float product[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                product[i * 4 + j] = static_cast<float>(sum);
            }
        }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), product, sizeof(product));
        Record({0xFA8C, Relocate(r.args[0]), Relocate(r.args[1])});
        for (float f : product)
            Record({std::bit_cast<uint32_t>(f)});
        return r.args[0];
    });
    // The PC code passed on to (and the engine's other calls): recorded.
    struct Call {
        uint32_t game;
        uint32_t ours;
        uint16_t pop;
        int args;
    };
    for (Call c : {Call{0x004B8E40, AddressOf(&Nx::SetFogEnabled), 0, 1},
                   Call{0x004B8E80, AddressOf(&Nx::SetFogDistance), 0, 1},
                   Call{0x004B8EE0, AddressOf(&Nx::SetFogColor), 0, 1},
                   Call{0x004BA7A0, AddressOf(&Nx::ShatterGeom), 0, 2},
                   Call{0x004B87B0, AddressOf(&Nx::LightTextureSplats), 0, 1},
                   Call{0x004B9200, AddressOf(&Nx::KillAllTextureSplats), 0, 0},
                   Call{0x004B92A0, AddressOf(&Nx::KillTextureSplatsInBox), 0, 1},
                   Call{0x004B9550, AddressOf(&Nx::KillAllShatters), 0, 0},
                   Call{0x004B95E0, AddressOf(&Nx::UpdateShatter), 0, 2},
                   Call{0x004B80B0, AddressOf(&Nx::DrawShatter), 0, 1},
                   Call{0x004B9030, AddressOf(&Nx::DrawFlash), 0, 1}, Call{0x00491E10, AddressOf(&Nx::Engine::KillAllSpawnedPolys), 0, 0},
                   Call{0x0049F540, 0, 4, 1}}) {
        std::initializer_list<uint32_t> both = {c.game, c.ours};
        std::initializer_list<uint32_t> one = {c.game};
        s.At(c.ours ? both : one, c.pop, [c](Regs& r) {
            Record({c.game & 0xFFFFF, c.pop ? Relocate(r.ecx) : 0u});
            for (int i = 0; i < c.args; ++i) {
                uint32_t value = r.args[i];
                if (c.game == 0x004B8E40)
                    value &= 0xFF; // (a bool)
                Record({Relocate(value)});
            }
            return c.game == 0x0049F540 ? 0x7E000000u + r.args[0] : 0u;
        });
    }
}

// The game memory the effects use: fog, the level colour, the shatter settings, the fade settings, the tables, the
// vertex stack, the particles' dictionary.
std::initializer_list<std::pair<uint32_t, uint32_t>> kMemory = {
    {0x007CC979, 1},  {0x007D1800, 4},  {0x007004C8, 1},   {0x0067DDD8, 0x30}, {0x007004A8, 0x58},
    {0x006FA7D4, 1},  {0x00700F20, 4},  {0x006FC4A0, 0x4008}};

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

// A float from a set of awkward values or a random one.
float AwkwardFloat(Lcg& random, float low, float high)
{
    switch (random.Below(6)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return std::numeric_limits<float>::quiet_NaN();
    default:
        return RandomFloat(random, low, high);
    }
}

// Fog, the level colour, the shatter settings, the splat parameters, the vertex stack, a geom shattered, the tables
// made and everything killed.
void TestEngineEffectsSettings()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInEffects(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("engine settings", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5E7 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x5E70 + round);
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            At<void(__cdecl*)(uint32_t)>(original, 0x00490DE0)(random.Pick({0u, 1u, 0x100u, 0x201u}));
            At<void(__cdecl*)(float)>(original, 0x00490E00)(RandomFloat(random, 0, 900));
            At<void(__cdecl*)(float)>(original, 0x00490E10)(RandomFloat(random, 0, 9));
            At<void(__cdecl*)(uint32_t)>(original, 0x00490E20)(random.Next());
            float color[4] = {RandomFloat(random, 0, 1), RandomFloat(random, 0, 1), 3, -4};
            At<void(__cdecl*)(const float*)>(original, 0x00491290)(color);
            float velocity[4] = {RandomFloat(random, -5, 5), 1, 2, 3};
            float vector[4] = {4, 5, RandomFloat(random, -5, 5), 6};
            At<void(__cdecl*)(const float*, const float*, float, float, float, float, float, float, float, float, uint32_t,
                              uint32_t, int)>(original, 0x00491110)(
                velocity, vector, AwkwardFloat(random, -9, 9), AwkwardFloat(random, 100, 900),
                AwkwardFloat(random, 0, 9), AwkwardFloat(random, 0, 3), AwkwardFloat(random, 0, 9),
                AwkwardFloat(random, -99, 99), AwkwardFloat(random, 0, 1), AwkwardFloat(random, 0, 1), random.Next(),
                random.Next(), static_cast<int>(random.Next()));
            uint8_t* params = Block(0x44);
            result(At<uint32_t(__thiscall*)(void*)>(original, 0x00490E40)(params));
            // The vertex stack: a vertex popped.
            auto& stack = GameVariable<Nx::sVertexStack>(0x006FC4A0);
            stack.vertexSize = 4 + random.Below(0x30);
            stack.used = stack.vertexSize * (1 + random.Below(20));
            uint8_t popped[0x40];
            std::memset(popped, 0xAB, sizeof(popped));
            At<void(__thiscall*)(void*, uint8_t*)>(original, 0x00490D80)(&stack, popped);
            for (uint32_t i = 0; i < sizeof(popped); i += 4)
                result(*reinterpret_cast<uint32_t*>(popped + i));
            result(stack.used);
            for (uint32_t i = 0; i < 0x400; i += 4)
                result(*reinterpret_cast<uint32_t*>(stack.data + i));
            At<void(__cdecl*)(void*)>(original, 0x00490DC0)(Block(0x10));
            // The tables made (some already there), then everything killed.
            for (uint32_t address = 0x007004A8; address <= 0x007004B8; address += 4)
                GameVariable<HashTable*>(address) = random.Below(3) ? nullptr : NewTable();
            At<void(__cdecl*)()>(original, 0x00492150)();
            for (uint32_t address = 0x007004A8; address <= 0x007004B8; address += 4)
                result(GameVariable<uint32_t>(address));
            for (uint32_t address = 0x007004A8; address <= 0x007004B8; address += 4) {
                HashTable* table = NewTable();
                GameVariable<HashTable*>(address) = table;
                for (uint32_t i = 0, n = random.Below(4); i < n; ++i)
                    Put(table, Block(0x30));
            }
            At<void(__cdecl*)()>(original, 0x00492340)();
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine effects: fog, level colour, shatter settings, splat parameters, vertex stack, shattering, the "
              "tables made and killed");
}

// The engine's shatter: made, its triangles stepped (bouncing or not), destroyed.
void TestEngineShatter()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInEffects(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 30; ++round) {
        ok &= CompareTwoRuns("engine shatter", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5A7 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            g_rand = 0x1234 + round;
            Lcg random(0x5A70 + round);
            auto* shatter = reinterpret_cast<Nx::CShatter*>(Block(0x40));
            int count = 1 + static_cast<int>(random.Below(6));
            GameVariable<float>(0x0067DDFC) = RandomFloat(random, -5, 5);
            Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004912C0)(shatter, count))});
            for (int i = 0; i < count; ++i) {
                for (int k = 0; k < 4; ++k) {
                    (&shatter->centres[i].x)[k] = RandomFloat(random, -6, 6);
                    (&shatter->velocities[i].x)[k] = RandomFloat(random, -9, 9);
                }
                std::memset(&shatter->rotations[i], 0x11 * i, sizeof(Mth::Matrix));
            }
            for (int step = 0; step < 12; ++step) {
                int i = static_cast<int>(random.Below(count));
                At<void(__thiscall*)(void*, int, float)>(original, 0x004913D0)(shatter, i,
                                                                                 RandomFloat(random, 0.001f, 0.2f));
            }
            for (int i = 0; i < count; ++i)
                Record({std::bit_cast<uint32_t>(shatter->centres[i].y), std::bit_cast<uint32_t>(shatter->velocities[i].y)});
            if (random.Below(2))
                At<void(__thiscall*)(void*)>(original, 0x00490E80)(shatter);
            else
                Record({Relocate(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00491380)(shatter,
                                                                                                  random.Below(2)))});
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine effects: a shatter made, stepped (bounces, new turns, gravity) and destroyed");
}

// Calls the age adapter: the elapsed time in EAX, the set on the stack.
void CallAge(void* code, void* splats, int elapsed)
{
    __asm {
        mov eax, elapsed
        push splats
        call code
        add esp, 4
    }
}

// A splat set: random times (some negative, zero, equal), lifetimes and alphas.
Nx::sTextureSplats* MakeSplats(Lcg& random)
{
    auto* splats = reinterpret_cast<Nx::sTextureSplats*>(Block(sizeof(Nx::sTextureSplats)));
    uint32_t shared = 200 + random.Below(400);
    int sharedDone = 300 + static_cast<int>(random.Below(400));
    for (int i = 0; i < 0x100; ++i) {
        switch (random.Below(8)) {
        case 0:
            splats->alive[i] = static_cast<uint32_t>(-1 - static_cast<int>(random.Below(3)));
            break;
        case 1:
            splats->alive[i] = 0;
            break;
        case 2:
        case 3:
            splats->alive[i] = shared;
            break;
        default:
            splats->alive[i] = random.Below(900);
            break;
        }
        splats->correctionSteps[i] = random.Below(5) ? 1000 + static_cast<int>(random.Below(3)) * 50 : 0;
        if (random.Below(6) == 0) // (the same difference from another time and lifetime)
            splats->alive[i] = static_cast<uint32_t>(splats->correctionSteps[i] + 600 - sharedDone);
        if (random.Below(10) == 0) // (a lifetime shorter than the time left: a negative difference)
            splats->correctionSteps[i] = static_cast<int>(random.Below(100));
        for (int k = 0; k < 3; ++k) {
            splats->baseColors[i][k] = random.Next() & (random.Below(3) ? 0xFFFFFFFFu : 0x00FFFFFFu);
            splats->baseColors[i][k] |= random.Below(4) ? 0 : 0x03000000u;
            for (int b = 0; b < 0x18; ++b)
                splats->vertices[i][k][b] = static_cast<uint8_t>(random.Below(3) ? random.Next() : 0);
        }
    }
    switch (random.Below(4)) { // the first triangle's difference: the bar the others must reach
    case 0:
        splats->correctionSteps[0] = 5000;
        splats->alive[0] = 1;
        break;
    case 1:
        splats->correctionSteps[0] = 0;
        splats->alive[0] = 3000;
        break;
    default:
        break;
    }
    splats->texture = 0x7E000000 + random.Below(3);
    return splats;
}

// Texture splats aged (one set through the adapter, all of them), lit, killed by texture, in a box, all.
void TestEngineSplats()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInEffects(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 30; ++round) {
        ok &= CompareTwoRuns("engine splats", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5B7 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x5B70 + round);
            GameVariable<int>(0x0067DDE0) = random.Pick({0, 40, 120, 250, 250, 256});
            GameVariable<int>(0x0067DDE4) = static_cast<int>(1 + random.Below(90));
            GameVariable<bool>(0x006FA7D4) = random.Below(4) == 0;
            g_frameSeconds = random.Pick({0.0166, 0.25, 0.0, 1.5});
            // One set through the register adapter.
            Nx::sTextureSplats* splats = MakeSplats(random);
            int elapsed = random.Pick({600, 0, 5, 100, -20});
            if (round % 3 == 0) { // (exactly as many free slots as the fade starts at)
                int living = 0;
                for (uint32_t left : splats->alive)
                    living += static_cast<int>(left) >= 0 && static_cast<int>(left) - elapsed > 0;
                GameVariable<int>(0x0067DDE0) = 0x100 - living;
            }
            CallAge(original ? OriginalFunction(0x00490EC0) : reinterpret_cast<void*>(&Nx::Engine::AgeTextureSplatsStub),
                    splats, elapsed);
            // The tables: sets and recent positions.
            HashTable* sets = NewTable();
            GameVariable<HashTable*>(0x007004B0) = random.Below(8) ? sets : nullptr;
            for (uint32_t i = 0, n = random.Below(4); i < n; ++i)
                Put(sets, MakeSplats(random));
            float box[8] = {RandomFloat(random, -10, 0), RandomFloat(random, -10, 0), RandomFloat(random, -10, 0), 1,
                            RandomFloat(random, 0, 10),  RandomFloat(random, 0, 10),  RandomFloat(random, 0, 10),  1};
            HashTable* recent = NewTable();
            GameVariable<HashTable*>(0x007004B4) = recent;
            for (uint32_t i = 0, n = random.Below(6); i < n; ++i) {
                auto* position = reinterpret_cast<float*>(Block(0x18));
                for (int k = 0; k < 3; ++k) {
                    position[k] = random.Below(8) ? RandomFloat(random, -10, 10) : std::numeric_limits<float>::quiet_NaN();
                    if (random.Below(4) == 0)
                        position[k] = box[random.Below(2) * 4 + k]; // (on a face)
                    else if (random.Below(3) == 0)
                        position[k] = RandomFloat(random, -1, 1);
                }
                position[3] = 1.0f;
                position[4] = 2.0f;
                position[5] = 3.0f;
                Put(recent, position);
            }
            At<void(__cdecl*)()>(original, 0x00491F70)();
            if (GameVariable<HashTable*>(0x007004B0)) {
                At<void(__cdecl*)()>(original, 0x00491F10)();
                At<void(__cdecl*)(uint32_t)>(original, 0x004916F0)(0x7E000000 + random.Below(4));
            }
            At<void(__cdecl*)(const float*)>(original, 0x00491630)(box);
            if (random.Below(2))
                At<void(__cdecl*)()>(original, 0x004915A0)();
            std::vector<uint32_t> trace = g_trace;
            for (auto* table : {sets, recent}) {
                for (HashTable::Item& item : g_items[table])
                    trace.push_back(Relocate(item.key));
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine effects: texture splats aged (fading when full), lit, killed by texture / box / all");
}

// Flashes added, updated (paused or not, running out) and drawn; shatters updated (running out) and drawn.
void TestEngineFlashes()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInEffects(s);
    void* destroy = s.Thunk(4, [](Regs& r) { // a shatter's virtual destructor
        Record({0xDE57, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    static void* shatterVtable[1];
    shatterVtable[0] = destroy;
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("engine flashes", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5F7 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x5F70 + round);
            HashTable* flashes = NewTable();
            GameVariable<HashTable*>(0x007004A8) = flashes;
            for (uint32_t i = 0, n = 1 + random.Below(5); i < n; ++i) {
                At<void(__cdecl*)(int, uint32_t, uint32_t, float, float, uint32_t, const char*)>(original, 0x004923E0)(
                    static_cast<int>(random.Below(2)), random.Next() ^ (random.Next() << 8),
                    random.Next() ^ (random.Next() << 8), RandomFloat(random, 0.05f, 2), RandomFloat(random, 0, 1),
                    random.Next() & 0xF, "flash.png");
            }
            if (round % 5 == 0) {
                At<void(__cdecl*)(int, uint32_t, uint32_t, float, float, uint32_t, const char*)>(original, 0x004923E0)(
                    1, 0xFFFFFFFFu, 0, 1.0f, 0.5f, 1, nullptr);
                auto* edge = reinterpret_cast<Nx::Flash*>(g_items[flashes].back().value);
                edge->timeLeft = std::nextafter(static_cast<float>(128.0 / 255.0), 1.0f);
                g_frameSeconds = 1e-9;
                GameVariable<bool>(0x006FA7D4) = false;
                At<void(__cdecl*)()>(original, 0x00491790)();
                Record({*reinterpret_cast<uint32_t*>(&edge->red)});
            }
            if (random.Below(4) == 0) // (one already out of time)
                reinterpret_cast<Nx::Flash*>(g_items[flashes][0].value)->timeLeft = 0.0f;
            HashTable* shatters = NewTable();
            GameVariable<HashTable*>(0x007004B8) = random.Below(8) ? shatters : nullptr;
            for (uint32_t i = 0, n = random.Below(4); i < n; ++i) {
                auto* shatter = reinterpret_cast<Nx::CShatter*>(Block(0x40));
                shatter->vtable = shatterVtable;
                shatter->lifetime = random.Below(2) ? random.Pick({0.1f, 0.4f, 0.0f}) : RandomFloat(random, -0.1f, 0.6f);
                Put(shatters, shatter);
            }
            for (int frame = 0; frame < 6; ++frame) {
                GameVariable<bool>(0x006FA7D4) = random.Below(4) == 0;
                g_frameSeconds = random.Pick({0.0166, 0.1, 0.0, 0.4});
                At<void(__cdecl*)()>(original, 0x00491790)();
                At<void(__cdecl*)(int, uint32_t)>(original, 0x00491910)(static_cast<int>(random.Below(2)),
                                                                        random.Pick({0u, 1u, 2u, 3u}));
                At<void(__cdecl*)()>(original, 0x00492000)();
                At<void(__cdecl*)()>(original, 0x004920E0)();
            }
            if (random.Below(3) == 0) {
                GameVariable<HashTable*>(0x007004A8) = nullptr;
                At<void(__cdecl*)()>(original, 0x00491790)();
            }
            std::vector<uint32_t> trace = g_trace;
            for (auto* table : {flashes, shatters}) {
                for (HashTable::Item& item : g_items[table])
                    trace.push_back(Relocate(item.key));
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine effects: flashes added, faded, run out and drawn by pass; shatters stepped, run out and drawn");
}

} // namespace

SELFTEST(Early, TestEngineEffectsSettings, RemoveStandIns);
SELFTEST(Early, TestEngineShatter, RemoveStandIns);
SELFTEST(Early, TestEngineSplats, RemoveStandIns);
SELFTEST(Early, TestEngineFlashes, RemoveStandIns);
