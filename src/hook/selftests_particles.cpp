// Self-tests for the particle component's particle systems and their manager (src/game/gfx/particles.cpp): the
// original and ours over stand-in systems (a recording vtable), the memory manager, the script calls and the engine's
// own methods stood in; the systems', the manager's and the lists' memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/particles.h"
#include "game/obj/pager_message.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CParticle;
using Nx::CParticleManager;
using Nx::CParticleParams;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

// A recording vtable: each slot records {0xF00 + slot, this, its arguments} and returns `this`; slot `create` (if
// any) returns what `create` makes instead.
struct FakeVtable {
    void* slots[8];
};
FakeVtable* g_particleVtable;
FakeVtable* g_managerVtable;
bool g_createFails;

void RandomParams(CParticleParams& p, Lcg& random)
{
    auto* bytes = reinterpret_cast<uint8_t*>(&p);
    for (size_t i = 0; i < sizeof(p); ++i)
        bytes[i] = static_cast<uint8_t>(random.Next());
    for (float& f : p.m_size)
        f = RandomFloat(random, 0.1f, 4);
    for (float& f : p.m_colourScale)
        f = RandomFloat(random, 0, 1);
    for (Mth::Vector& v : p.m_pos)
        v = {RandomFloat(random, -40, 40), RandomFloat(random, -40, 40), RandomFloat(random, -40, 40),
             RandomFloat(random, 0, 2)};
    for (Mth::Vector& v : p.m_box)
        v = {RandomFloat(random, 0, 9), RandomFloat(random, 0, 9), RandomFloat(random, 0, 9), RandomFloat(random, -1, 1)};
    for (Mth::Vector& row : p.m_rotation.row)
        row = {RandomFloat(random, -1, 1), RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
               RandomFloat(random, -1, 1)};
    p.m_name = random.Below(4) + 0x100; // (names that repeat)
    p.m_flag158 = random.Below(2) != 0;
    p.m_emitScript = random.Below(3) ? 0 : random.Next();
}

// A new stand-in system in the arena (the size of the engine's), its vtable the recording one.
CParticle* MakeParticle(Lcg& random)
{
    auto* particle = reinterpret_cast<CParticle*>(Bump(sizeof(CParticle)));
    RandomParams(particle->m_params, random);
    particle->vtable = g_particleVtable->slots;
    return particle;
}
Lcg* g_random;

void StandInParticleCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00494E00}, 4, [](Regs& r) { // Render(advance): a bool (the original's SETZ leaves EAX's top bytes)
        Record({0x494E00, Relocate(r.ecx), r.args[0] & 0xFF});
        return r.ecx;
    });
    StandInThis(s, {0x00494E20}, 0); // Update
    StandInThis(s, {0x00494E40}, 0); // Release
    s.At({0x00495260, AddressOf(&CParticle::UpdateBounds)}, 0, [](Regs& r) { // (what it's given)
        Record({0x495260, Relocate(r.ecx), HashBytes(reinterpret_cast<const uint8_t*>(r.ecx + 4), 0x159)});
        return r.ecx;
    });
    StandInCdecl(s, {0x00474040, AddressOf(&Script::KillSpawnedScriptsWithId)}, 1);
    StandInCdecl(s,
                 {0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, Script::CStruct*, uint32_t,
                                                                          Script::CStruct*, int, uint32_t, bool, bool,
                                                                          bool, bool)>(&Script::SpawnScript))},
                 10, 0x5C000000);
    StandInThis(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0);
    StandInThis(s,
                {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(
                                 &Script::CStruct::AddChecksum))},
                2);
    StandInThis(s, {0x00477130, AddressOf(&Script::CStruct::Clear)}, 0);
    StandInCdecl(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1);

    static const uint16_t pops[8] = {4, 0, 0, 4, 4, 0, 0, 0};
    g_particleVtable = new FakeVtable();
    g_managerVtable = new FakeVtable();
    for (int slot = 0; slot < 8; ++slot) {
        g_particleVtable->slots[slot] = s.Thunk(pops[slot], [slot](Regs& r) {
            Record({0xF00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (pops[slot])
                Record({r.args[0]});
            return r.ecx;
        });
        g_managerVtable->slots[slot] = s.Thunk(slot == 0 ? 4 : 0, [slot](Regs& r) {
            Record({0xE00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot != 1)
                return r.ecx;
            return g_createFails ? 0u : reinterpret_cast<uint32_t>(MakeParticle(*g_random));
        });
    }
}

// The parameters' defaults and copies; a system constructed, made from parameters, its positions and boxes set and
// turned, its colour set (taking the manager's or not), its emit script set (an old one or not, the structure's
// memory there or not).
void TestParticleSystemBasics()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F3614, 16}});
    StandIns& s = *new StandIns();
    StandInParticleCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("particle systems", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x9A00 + round);
            g_random = &random;
            std::vector<uint32_t> trace;
            // the parameters
            auto* params = reinterpret_cast<CParticleParams*>(Bump(sizeof(CParticleParams)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00496240)(params)));
            for (uint32_t i = 0; i < sizeof(CParticleParams); i += 4)
                trace.push_back(*reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(params) + i));
            auto* copy = reinterpret_cast<CParticleParams*>(Bump(sizeof(CParticleParams)));
            RandomParams(*copy, random);
            trace.push_back(
                ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, const void*)>(original, 0x00495C80)(params, copy)));
            RandomParams(*copy, random);
            // the system
            auto* particle = reinterpret_cast<CParticle*>(Bump(sizeof(CParticle)));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00496360)(particle)));
            trace.push_back(reinterpret_cast<uint32_t>(particle->vtable));
            particle->vtable = g_particleVtable->slots;
            Fn<void(__thiscall*)(void*, const void*)>(original, 0x00496380)(particle, copy);
            for (int step = 0; step < 12; ++step) {
                int which = static_cast<int>(random.Below(3));
                switch (random.Below(7)) {
                case 0: {
                    Mth::Vector v = {RandomFloat(random, -50, 50), RandomFloat(random, -50, 50),
                                     RandomFloat(random, -50, 50), RandomFloat(random, -2, 2)};
                    Fn<void(__thiscall*)(void*, int, const void*)>(original, 0x00496030)(particle, which, &v);
                    break;
                }
                case 1: {
                    Mth::Vector v = {RandomFloat(random, 0, 9), RandomFloat(random, 0, 9), RandomFloat(random, 0, 9),
                                     RandomFloat(random, -2, 2)};
                    Fn<void(__thiscall*)(void*, int, const void*)>(original, 0x00496160)(particle, which, &v);
                    break;
                }
                case 2: {
                    Mth::Matrix m;
                    for (Mth::Vector& row : m.row)
                        row = {RandomFloat(random, -1, 1), RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
                               RandomFloat(random, -3, 3)};
                    Fn<void(__thiscall*)(void*, int, const void*)>(original, 0x00496060)(particle, which, &m);
                    break;
                }
                case 3: {
                    uint8_t rgba[4];
                    for (uint8_t& c : rgba)
                        c = static_cast<uint8_t>(random.Next());
                    particle->m_params.m_flag158 = random.Below(2) != 0;
                    Fn<void(__thiscall*)(void*, const void*)>(original, 0x00496190)(particle,
                                                                                    random.Below(4) ? rgba : nullptr);
                    break;
                }
                case 4: {
                    float rgba[4];
                    for (float& c : rgba)
                        c = RandomFloat(random, 0, 2);
                    particle->m_params.m_flag158 = random.Below(2) != 0;
                    Fn<void(__thiscall*)(void*, const void*)>(original, 0x00496210)(particle, rgba);
                    break;
                }
                case 5: {
                    if (random.Below(4) == 0)
                        g_failAllocations = {true};
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x004963B0)(
                        particle, random.Below(3) ? random.Next() : 0, random.Next());
                    g_failAllocations.clear();
                    break;
                }
                default:
                    particle->m_params.m_emitScript = random.Below(2) ? random.Next() : 0;
                    break;
                }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "particles: parameters' defaults and copies; systems made, positioned, boxed, turned, coloured, "
              "their emit scripts set");
}

// The manager constructed (its sentinel's memory there or not), systems made (or not: no memory; taking the
// manager's colour or not), rendered (paused or not; none), updated, coloured, destroyed one by one (by name: one
// there, one not, repeated names; an emit script or not), all destroyed, the manager destroyed (freed or not); the
// list's insert directly.
void TestParticleManager()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F3614, 16}, {0x006FA7D4, 1}});
    StandIns& s = *new StandIns();
    StandInParticleCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("particle manager", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x9B00 + round);
            g_random = &random;
            std::vector<uint32_t> trace;
            auto* manager = reinterpret_cast<CParticleManager*>(Bump(sizeof(CParticleManager)));
            if (round == 3)
                g_failAllocations = {true};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00496770)(manager)));
            g_failAllocations.clear();
            trace.push_back(reinterpret_cast<uint32_t>(manager->vtable));
            if (round != 3) {
                manager->vtable = g_managerVtable->slots;
                if (round % 5 == 1)
                    Fn<void(__thiscall*)(void*)>(original, 0x00496640)(manager); // (none)
                for (int step = 0; step < 14; ++step) {
                    switch (random.Below(8)) {
                    case 0:
                    case 1:
                    case 2: {
                        auto* params = reinterpret_cast<CParticleParams*>(Bump(sizeof(CParticleParams)));
                        RandomParams(*params, random);
                        g_createFails = random.Below(6) == 0; // (the list node's memory always there: without it
                                                              // the original crashes)
                        trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, const void*)>(
                            original, 0x00496510)(manager, params)));
                        break;
                    }
                    case 3:
                        GameVariable<uint8_t>(0x006FA7D4) = static_cast<uint8_t>(random.Below(2));
                        Fn<void(__thiscall*)(void*)>(original, 0x00496640)(manager);
                        break;
                    case 4:
                        if (random.Below(3) == 0) { // (the count out of step with the list: rendering checks it)
                            int size = manager->m_systems.size;
                            manager->m_systems.size = 0;
                            Fn<void(__thiscall*)(void*)>(original, 0x00496640)(manager);
                            manager->m_systems.size = size;
                        }
                        Fn<void(__thiscall*)(void*)>(original, 0x00496680)(manager);
                        break;
                    case 5: {
                        float rgba[4];
                        for (float& c : rgba)
                            c = RandomFloat(random, 0, 2);
                        Fn<void(__thiscall*)(void*, const void*)>(original, 0x004966B0)(manager, rgba);
                        break;
                    }
                    default: {
                        CParticle* other = MakeParticle(random);
                        Fn<void(__thiscall*)(void*, void*)>(original, 0x00496590)(manager, other);
                        break;
                    }
                    }
                }
                if (round & 1)
                    Fn<void(__thiscall*)(void*)>(original, 0x00496700)(manager);
                trace.push_back(static_cast<uint32_t>(manager->m_systems.size));
                trace.push_back(ArenaRelocate(
                    Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x00496860)(manager, round & 2 ? 1 : 0)));
            }
            // the list's insert: before the sentinel, before another node
            auto* list = reinterpret_cast<Obj::PointerList*>(Bump(sizeof(Obj::PointerList)));
            auto* head = reinterpret_cast<Obj::PointerListNode*>(Bump(sizeof(Obj::PointerListNode)));
            head->next = head->prev = head;
            list->head = head;
            list->size = static_cast<int>(random.Below(5));
            for (int i = 0; i < 4; ++i) {
                void* value = reinterpret_cast<void*>(random.Next());
                Obj::PointerListNode* position = i & 1 ? head->next : head;
                Fn<void(__thiscall*)(void*, void*, void* const*)>(original, 0x004964A0)(list, position, &value);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "particles: the manager made, systems made, rendered, updated, coloured, destroyed by name and all, "
              "the manager destroyed; the list's insert");
}

// The bounds: random positions, boxes (some negative), sizes and radii, through the mid or not; the engine's
// min / max (ECX, EDX, ESI) and curve extremes stood in (their inputs recorded; NaN and ties among them).
uint32_t FloatWord(float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}
void TestParticleBounds()
{
    StandIns& s = *new StandIns();
    for (uint32_t code : {0x00495130u, 0x004951B0u}) {
        bool max = code == 0x004951B0u;
        s.At({code}, 0, [max](Regs& r) {
            const auto* a = reinterpret_cast<const float*>(r.ecx);
            const auto* b = reinterpret_cast<const float*>(r.edx);
            auto* out = reinterpret_cast<float*>(r.esi);
            Record({max ? 0x51B0u : 0x5130u});
            float result[4];
            for (int i = 0; i < 4; ++i) {
                Record({FloatWord(a[i]), FloatWord(b[i])});
                result[i] = (max ? b[i] > a[i] : b[i] < a[i]) ? b[i] : a[i];
            }
            std::memcpy(out, result, 16);
            return r.esi;
        });
    }
    s.At({0x00494F10}, 0, [](Regs& r) {
        Record({0x4F10});
        uint32_t hash = 0;
        for (int i = 0; i < 14; ++i) {
            Record({r.args[i]});
            hash = hash * 31 + r.args[i];
        }
        auto* out = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 3; ++i)
            out[i] = static_cast<float>(static_cast<int>((hash >> (i * 8)) & 0xFF) - 128);
        out[3] = 0.0f;
        return r.ecx;
    });
    bool ok = true, reported = false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int round = 0; round < 300; ++round) {
        ok &= CompareTwoRuns("particle bounds", reported, [&](bool original) {
            g_trace.clear();
            Lcg random(0x9C00 + round);
            alignas(16) static uint8_t storage[sizeof(CParticle)];
            auto* particle = reinterpret_cast<CParticle*>(storage);
            std::memset(storage, 0xEE, sizeof(storage));
            CParticleParams& p = particle->m_params;
            for (int i = 0; i < 3; ++i) {
                // (large and small magnitudes together: the sums' rounding shows)
                float scale = random.Below(3) == 0 ? 100000.0f : 10.0f;
                p.m_pos[i] = {RandomFloat(random, -scale, scale), RandomFloat(random, -scale, scale),
                              RandomFloat(random, -scale, scale), RandomFloat(random, -1, 1)};
                p.m_box[i] = {RandomFloat(random, -3, 30), RandomFloat(random, -3, 30), RandomFloat(random, -3, 30),
                              RandomFloat(random, -1, 1)};
                p.m_size[i] = RandomFloat(random, -1, 9) * (random.Below(4) == 0 ? 0.0001f : 1.0f);
                p.m_radius[i] = RandomFloat(random, -1, 9);
            }
            if (round % 7 == 3)
                p.m_pos[2] = p.m_pos[0]; // (ties)
            if (round % 11 == 5)
                p.m_box[random.Below(3)].y = nan;
            p.m_unknown2C = RandomFloat(random, 0.1f, 6);
            p.m_unknown30 = RandomFloat(random, 1, 99);
            p.m_flag151 = random.Below(2) != 0;
            Fn<void(__thiscall*)(void*)>(original, 0x00495260)(particle);
            std::vector<uint32_t> trace = g_trace;
            for (uint32_t i = 0; i < sizeof(storage); i += 4)
                trace.push_back(OnStack(*reinterpret_cast<uint32_t*>(storage + i)) ? 0x57AC0000u
                                                                                    : *reinterpret_cast<uint32_t*>(storage + i));
            for (uint32_t& w : trace)
                if (OnStack(w))
                    w = 0x57AC0000u;
            return trace;
        });
    }
    delete &s;
    Check(ok, "particles: the bounds (box and sphere) from the paths, through the mid or not");
}

} // namespace

SELFTEST(Early, TestParticleSystemBasics, RemoveStandIns);
SELFTEST(Early, TestParticleManager, RemoveStandIns);
SELFTEST(Early, TestParticleBounds, RemoveStandIns);
