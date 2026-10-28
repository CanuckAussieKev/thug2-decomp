// Self-tests for the engine's flexible particle systems (src/game/gfx/particle_system.cpp,
// particle_system_update.cpp): the original and ours over stand-in systems in an arena, the memory manager, the
// script calls, the camera and the particle manager stood in; the systems' memory, the results and the calls compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/camera.h"
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
using Nx::CParticleSystem;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}
uint32_t FloatBits(float f) { return std::bit_cast<uint32_t>(f); }

// A recording vtable for the systems: slot `n` records {0xF00 + n, this, its argument if it takes one}.
struct FakeVtable {
    void* slots[4];
};
FakeVtable* g_systemVtable;
uint32_t g_scriptResult; // what the scripts' Update says
Gfx::Camera* g_camera;

void StandInSystemCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005BB000, AddressOf(&Mem::Manager::AllocateZeroed)}, 12, [](Regs& r) {
        Record({0xB000, r.args[0], r.args[1], r.args[2]});
        uint32_t at = r.args[0] >= 0x80000000u ? 0 : Bump(r.args[0]); // (a negative size: none)
        if (at)
            std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    StandInThis(s, {0x0049AD60}, 1); // the particle manager lets go of a system
    StandInCdecl(s, {0x0048C3C0}, 1); // a model deleted
    StandInThis(s, {0x00474420, AddressOf(&Script::CScript::Destroy)}, 0);
    StandInCdecl(s, {0x004499B0, AddressOf(static_cast<void (*)(void*)>(&Script::CScript::operator delete))}, 1);
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Record({0x4738E0, Relocate(r.ecx)});
        return r.ecx;
    });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Record({0x4750B0, Relocate(r.ecx)});
        return g_scriptResult;
    });
    StandInThis(s,
                {0x004747A0, AddressOf(static_cast<void (Script::CScript::*)(uint32_t, Script::CStruct*,
                                                                              Obj::CObject*)>(&Script::CScript::SetScript))},
                3);
    StandInThis(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0);
    StandInThis(s, {0x00478540, AddressOf(&Script::CStruct::AppendStructureIfAny)}, 1);
    StandInThis(s, {0x00477130, AddressOf(&Script::CStruct::Clear)}, 0);
    StandInCdecl(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x401C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xFFFFFFFFu;
    });
    StandInCdecl(s, {0x004A09C0}, 1, 0);
    g_systemVtable = new FakeVtable();
    for (int slot = 0; slot < 4; ++slot) {
        g_systemVtable->slots[slot] = s.Thunk(slot == 0 ? 4 : 0, [slot](Regs& r) {
            Record({0xF00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot == 0)
                Record({r.args[0]});
            return slot == 2 ? 3u : slot == 3 ? 2u : r.ecx;
        });
    }
}

// The memory a run uses: the arena, and the globals read.
GameMemory* NewSystemMemory()
{
    return new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F3614, 16}, {0x006F3548, 16}, {0x00658555, 1},
                           {0x006FA7C0, 4}, {0x00701378, 4}});
}
void ResetRun(GameMemory& memory, uint32_t seed)
{
    g_trace.clear();
    ResetArena(0xEE);
    memory.Zero();
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
    GameVariable<uint32_t>(0x006FA7C0) = 0x3A9B0000;
    GameVariable<uint8_t>(0x00658555) = static_cast<uint8_t>(seed & 1);
}

// A system's fields at random (sane floats where they're floats).
void RandomSystem(CParticleSystem* system, Lcg& random)
{
    auto* bytes = reinterpret_cast<uint8_t*>(system);
    for (size_t i = 0; i < sizeof(CParticleSystem); ++i)
        bytes[i] = static_cast<uint8_t>(random.Next());
    system->m_vtable = g_systemVtable->slots;
    system->m_pos = {RandomFloat(random, -500, 500), RandomFloat(random, -500, 500), RandomFloat(random, -500, 500),
                     RandomFloat(random, -1, 1)};
    for (float* f : {&system->m_force[0], &system->m_force[1], &system->m_force[2], &system->m_emitRange[0],
                     &system->m_emitRange[1], &system->m_emitRange[2], &system->m_life[0], &system->m_life[1],
                     &system->m_speed[0], &system->m_speed[1], &system->m_emitTarget[0], &system->m_emitTarget[1],
                     &system->m_emitTarget[2], &system->m_rotation[0], &system->m_rotation[1],
                     &system->m_rotation[2]})
        *f = RandomFloat(random, -3, 3);
    system->m_lodNear = RandomFloat(random, 0, 300000);
    system->m_lodFar = RandomFloat(random, 0, 600000);
    system->m_maxParticles = static_cast<int>(random.Below(200));
    system->m_lodMinParticles = static_cast<int>(random.Below(50));
    system->mp_matrix = nullptr;
    system->mp_emitScript = nullptr;
    system->mp_updateScript = nullptr;
    system->mp_scriptParams = nullptr;
}

// Every type constructed (with the memory there, or failing at each allocation in turn) and destroyed - by its body,
// by its scalar deleting destructor (freed or not) and through the node; with scripts and parameters or without.
void TestParticleSystemLife()
{
    GameMemory& memory = *NewSystemMemory();
    StandIns& s = *new StandIns();
    StandInSystemCalls(s);
    g_relocate = ArenaRelocate;
    struct Type {
        uint32_t construct, destroyBody, destroy, destroyFromNode;
    };
    const Type types[] = {
        {0x00496F10, 0x00498690, 0x00498F80, 0x00496F50}, // the base
        {0x00499010, 0x00498FB0, 0x00498FE0, 0x00498FD0}, // newflat
        {0x004992C0, 0x00499200, 0x00499290, 0x004991F0}, // flat ribbon
        {0x004995B0, 0x00499550, 0x00499580, 0x00499570}, // glow
        {0x00499880, 0x00499800, 0x00499850, 0x00499840}, // glow ribbon trail
        {0x00499B80, 0x00499B20, 0x00499B50, 0x00499B40}, // line
        {0, 0x00499E10, 0x0049A090, 0x00499E00},          // model (constructed elsewhere)
    };
    bool ok = true, reported = false;
    for (int round = 0; round < 7 * 16; ++round) {
        const Type& type = types[round % 7];
        ok &= CompareTwoRuns("particle system life", reported, [&](bool original) {
            ResetRun(memory, static_cast<uint32_t>(round));
            Lcg random(0x7A00 + round);
            std::vector<uint32_t> trace;
            auto* system = reinterpret_cast<CParticleSystem*>(Bump(0x150));
            int failAt = (round / 7) % 8; // (0: none; else the allocation that fails)
            if (failAt)
                for (int i = 1; i <= failAt; ++i)
                    g_failAllocations.push_back(i == failAt);
            int maxParticles = 1 + static_cast<int>(random.Below(9));
            int history = static_cast<int>(random.Below(6)) - 1; // (-1: no steps)
            if (round % 13 == 5)
                history = -4; // (a negative size: the positions' allocation fails)
            if (type.construct == 0x00496F10) {
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, type.construct)(system)));
            } else if (type.construct) {
                trace.push_back(ArenaRelocate(
                    Fn<uint32_t(__thiscall*)(void*, uint32_t, int, uint32_t, uint32_t, int, int, float, int)>(
                        original, type.construct)(system, random.Next(), maxParticles, random.Next(), random.Next(),
                                                  static_cast<int>(random.Below(9)), static_cast<int>(random.Below(9)),
                                                  RandomFloat(random, 0, 5), history)));
            } else { // the model type: its own fields
                RandomSystem(system, random);
                system->mp_smartPointers = nullptr;
                auto* model = reinterpret_cast<Nx::CModelParticles*>(system);
                model->m_modelCount = random.Below(4);
                model->mp_unknown13C = random.Below(3) ? reinterpret_cast<void*>(Bump(16)) : nullptr;
                model->mp_models = random.Below(4) ? reinterpret_cast<void**>(Bump(16)) : nullptr;
                if (model->mp_models)
                    for (unsigned int i = 0; i < 4; ++i)
                        model->mp_models[i] = reinterpret_cast<void*>(0x30DE0000 + i);
                system->mp_particles = nullptr;
                system->mp_startColour = system->mp_midColour = system->mp_endColour = nullptr;
                system->mp_positions = nullptr;
            }
            g_failAllocations.clear();
            // what the destructor frees: scripts and parameters (or not)
            if (random.Below(2))
                system->mp_emitScript = reinterpret_cast<Script::CScript*>(Bump(0x10));
            if (random.Below(2))
                system->mp_updateScript = reinterpret_cast<Script::CScript*>(Bump(0x10));
            if (random.Below(2))
                system->mp_scriptParams = reinterpret_cast<Script::CStruct*>(Bump(0x10));
            if (type.construct == 0x00496F10) { // (the base has nothing else of its own)
                system->mp_particles = nullptr;
                system->mp_startColour = system->mp_midColour = system->mp_endColour = nullptr;
                system->mp_positions = nullptr;
            }
            // linked into a list of two (the node unlinks itself)
            auto* other = reinterpret_cast<Mem::ListNode*>(Bump(sizeof(Mem::ListNode)));
            other->next = other->prev = &system->m_node;
            system->m_node.next = system->m_node.prev = other;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            g_trace.clear();
            unsigned int freeFlags = (round / 28) & 1 ? 0 : 1;
            switch ((round / 7) % 3) {
            case 0:
                Fn<void(__thiscall*)(void*)>(original, type.destroyBody)(system);
                break;
            case 1:
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, type.destroy)(
                    system, freeFlags)));
                break;
            default:
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(
                    original, type.destroyFromNode)(&system->m_node, freeFlags)));
                break;
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "particle system: every type constructed (memory failing at each step) and destroyed every way");
}

// The settings, positions, emit matrix (following a matrix or not; the target nearly up, along x, zero, NaN), LOD
// (near / far either way round, NaN, viewports, no camera, the camera near / between / far), bounds (NaN too), colour
// scale, scripts (emit / update: set, kept, deleted; run to their end or not), rendering when visible, the time flag,
// the rotation matrix, the stub counts.
void TestParticleSystemSettings()
{
    GameMemory& memory = *NewSystemMemory();
    StandIns& s = *new StandIns();
    StandInSystemCalls(s);
    s.At({0x004A09C0}, 0, [](Regs& r) {
        Record({0x4A09C0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_camera);
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("particle system settings", reported, [&](bool original) {
            ResetRun(memory, static_cast<uint32_t>(round));
            Lcg random(0x7B00 + round);
            std::vector<uint32_t> trace;
            auto* system = reinterpret_cast<CParticleSystem*>(Bump(sizeof(CParticleSystem)));
            RandomSystem(system, random);
            auto* positions = reinterpret_cast<float*>(Bump(4 * 3 * 8 * 8));
            for (int i = 0; i < 3 * 8 * 8; ++i)
                positions[i] = RandomFloat(random, -100, 100);
            system->mp_positions = positions;
            system->m_maxParticles = 1 + static_cast<int>(random.Below(8));
            g_camera = reinterpret_cast<Gfx::Camera*>(Bump(0x200));
            *g_camera->GetPos() = {RandomFloat(random, -500, 500), RandomFloat(random, -500, 500),
                                   RandomFloat(random, -500, 500), 1.0f};
            for (int step = 0; step < 16; ++step) {
                switch (random.Below(16)) {
                case 0: {
                    static const uint32_t setters[] = {0x004968B0, 0x004968E0, 0x00496910, 0x00496940};
                    Fn<void(__thiscall*)(void*, float, float, float)>(original, setters[random.Below(4)])(
                        system, RandomFloat(random, -9, 9), RandomFloat(random, -9, 9), RandomFloat(random, -9, 9));
                    break;
                }
                case 1: {
                    float xyz[3] = {1, 2, 3};
                    int particle = static_cast<int>(random.Below(8)), step2 = static_cast<int>(random.Below(8));
                    if (particle >= system->m_maxParticles)
                        particle = 0;
                    Fn<void(__thiscall*)(void*, int, int, float*, float*, float*)>(original, 0x00496970)(
                        system, particle, step2 % 7, &xyz[0], &xyz[1], &xyz[2]);
                    for (float f : xyz)
                        trace.push_back(FloatBits(f));
                    Fn<void(__thiscall*)(void*, int, int, float, float, float)>(original, 0x004969B0)(
                        system, particle, (step2 + 3) % 7, xyz[2], RandomFloat(random, -5, 5), xyz[0]);
                    break;
                }
                case 2: {
                    Mth::Matrix follow, out;
                    for (Mth::Vector& row : follow.row)
                        row = {RandomFloat(random, -1, 1), RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
                               RandomFloat(random, -1, 1)};
                    std::memset(&out, 0xAB, sizeof(out));
                    system->mp_matrix = random.Below(4) == 0 ? &follow : nullptr;
                    switch (random.Below(6)) {
                    case 0: // nearly up (or down)
                        system->m_emitTarget[0] = RandomFloat(random, -0.05f, 0.05f);
                        system->m_emitTarget[1] = random.Below(2) ? 3.0f : -3.0f;
                        system->m_emitTarget[2] = RandomFloat(random, -0.05f, 0.05f);
                        break;
                    case 1:
                        system->m_emitTarget[0] = system->m_emitTarget[1] = system->m_emitTarget[2] = 0.0f;
                        break;
                    case 2:
                        system->m_emitTarget[1] = nan;
                        break;
                    default:
                        break;
                    }
                    Fn<void(__thiscall*)(void*, Mth::Matrix*)>(original, 0x004969F0)(system, &out);
                    system->mp_matrix = nullptr;
                    for (const Mth::Vector& row : out.row)
                        for (float f : {row.x, row.y, row.z, row.w})
                            trace.push_back(FloatBits(f));
                    break;
                }
                case 3:
                case 4: {
                    GameVariable<int>(0x00701378) = static_cast<int>(random.Below(4)) - 1;
                    if (random.Below(6) == 0)
                        g_camera = nullptr;
                    else if (!g_camera)
                        g_camera = reinterpret_cast<Gfx::Camera*>(Bump(0x200));
                    if (g_camera) {
                        float d = RandomFloat(random, 0, 900);
                        *g_camera->GetPos() = {system->m_pos.x + d * 0.6f, system->m_pos.y - d * 0.8f,
                                               system->m_pos.z + RandomFloat(random, -1, 1), 1.0f};
                    }
                    switch (random.Below(5)) {
                    case 0:
                        system->m_lodNear = nan;
                        break;
                    case 1:
                        system->m_lodFar = system->m_lodNear;
                        break;
                    case 2:
                        system->m_lodNear = RandomFloat(random, 0, 100000);
                        system->m_lodFar = system->m_lodNear + RandomFloat(random, 1, 500000);
                        break;
                    default:
                        break;
                    }
                    if (g_camera && random.Below(8) == 0)
                        g_camera->GetPos()->x = nan;
                    int maxParticles = system->m_maxParticles;
                    int count = random.Below(4) == 0 ? static_cast<int>(random.Below(10))
                                                     : static_cast<int>(random.Below(300));
                    if (g_camera && random.Below(3) == 0) { // (a large count in range: the distance's rounding shows)
                        GameVariable<int>(0x00701378) = static_cast<int>(random.Below(2));
                        system->m_maxParticles = 0x40000000 + static_cast<int>(random.Below(1000));
                        system->m_lodNear = RandomFloat(random, 0, 1000);
                        system->m_lodFar = 1000000.0f;
                        count = 0x7FFFFFFF;
                    } else if (g_camera && random.Below(3) == 0) { // (the camera at the emitter: distance 0 on a bound)
                        GameVariable<int>(0x00701378) = static_cast<int>(random.Below(2));
                        *g_camera->GetPos() = system->m_pos;
                        system->m_lodMinParticles = 20 + static_cast<int>(random.Below(20));
                        count = static_cast<int>(random.Below(40));
                        if (random.Below(2)) {
                            system->m_lodNear = 0.0f;
                            system->m_lodFar = 50.0f;
                        } else {
                            system->m_lodNear = -5.0f;
                            system->m_lodFar = 0.0f;
                        }
                    }
                    trace.push_back(static_cast<uint32_t>(
                        Fn<int(__thiscall*)(void*, int)>(original, 0x00496B40)(system, count)));
                    system->m_maxParticles = maxParticles;
                    break;
                }
                case 5:
                    if (random.Below(6) == 0)
                        system->m_force[random.Below(3)] = nan;
                    if (random.Below(6) == 0)
                        system->m_emitRange[random.Below(3)] = nan;
                    Fn<void(__thiscall*)(void*)>(original, 0x00496C30)(system);
                    if (std::isnan(system->m_force[0]) || std::isnan(system->m_force[1]) ||
                        std::isnan(system->m_force[2]))
                        system->m_force[0] = system->m_force[1] = system->m_force[2] = 0.5f;
                    break;
                case 6: {
                    float rgba[4];
                    for (float& c : rgba)
                        c = RandomFloat(random, 0, 2);
                    system->m_flags ^= random.Below(2) ? 0x10000u : 0u;
                    Fn<void(__thiscall*)(void*, const float*)>(original, 0x00496CE0)(system, rgba);
                    break;
                }
                case 7:
                    system->mp_emitScript =
                        random.Below(3) ? reinterpret_cast<Script::CScript*>(Bump(0x10)) : nullptr;
                    g_scriptResult = random.Below(2);
                    Fn<void(__thiscall*)(void*)>(original, 0x00496F60)(system);
                    break;
                case 8:
                    system->m_unknown12F = random.Below(2) != 0;
                    system->m_unknown130 = random.Below(2) != 0;
                    system->m_flags |= random.Below(2);
                    Fn<void(__thiscall*)(void*, bool)>(original, 0x00496FA0)(system, random.Below(3) == 0);
                    break;
                case 9:
                case 10: {
                    uint32_t at = random.Below(2) ? 0x00497790 : 0x00497900;
                    if (random.Below(4) == 0)
                        g_failAllocations = {random.Below(2) != 0};
                    Fn<void(__thiscall*)(void*, uint32_t, void*, void*)>(original, at)(
                        system, random.Below(4) ? random.Next() : 0, reinterpret_cast<void*>(random.Next()),
                        reinterpret_cast<void*>(random.Next()));
                    g_failAllocations.clear();
                    break;
                }
                case 11:
                    Fn<void(__thiscall*)(void*, uint8_t)>(original, 0x00497A70)(
                        system, static_cast<uint8_t>(random.Below(3)));
                    break;
                case 12: {
                    Mth::Matrix out;
                    std::memset(&out, 0xAB, sizeof(out));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, Mth::Matrix*)>(original, 0x00497AB0)(system, &out) ==
                                    reinterpret_cast<uint32_t>(&out));
                    for (const Mth::Vector& row : out.row)
                        for (float f : {row.x, row.y, row.z, row.w})
                            trace.push_back(FloatBits(f));
                    break;
                }
                case 14: // (the defaults again, over whatever is there)
                    Fn<void(__thiscall*)(void*)>(original, 0x00496D70)(system);
                    break;
                case 13:
                    trace.push_back(static_cast<uint32_t>(
                        Fn<int(__cdecl*)()>(original, random.Below(2) ? 0x00496890 : 0x004968A0)()));
                    break;
                default: { // the types' counts
                    auto* typed = reinterpret_cast<int*>(system);
                    typed[0x134 / 4] = static_cast<int>(random.Below(20)) - 3;
                    static const uint32_t counts[] = {0x004991E0, 0x00499820, 0x00499830};
                    trace.push_back(static_cast<uint32_t>(
                        Fn<int(__thiscall*)(void*)>(original, counts[random.Below(3)])(system)));
                    break;
                }
                }
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "particle system: settings, positions, emit matrix, LOD, bounds, colour, scripts, rendering, the "
              "rotation matrix, the counts");
}

} // namespace

SELFTEST(Early, TestParticleSystemLife, RemoveStandIns);
SELFTEST(Early, TestParticleSystemSettings, RemoveStandIns);
