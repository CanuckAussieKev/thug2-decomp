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
#include "game/nx/feeler.h"
#include "game/obj/object.h"
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
uint32_t g_steps = 3;     // what the systems' GetNumVertexLists says
uint32_t g_colours = 2;   // and GetNumColours
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
            return slot == 2 ? g_steps : slot == 3 ? g_colours : r.ecx;
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
                switch (random.Below(17)) {
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
                case 15: { // a bare node out of a list of three, destroyed (freed or not)
                    auto* nodes = reinterpret_cast<Mem::ListNode*>(Bump(3 * sizeof(Mem::ListNode)));
                    for (int i = 0; i < 3; ++i) {
                        nodes[i].next = &nodes[(i + 1) % 3];
                        nodes[i].prev = &nodes[(i + 2) % 3];
                    }
                    Mem::ListNode* node = &nodes[random.Below(3)];
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x00496D10)(node);
                    else
                        trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(
                            original, 0x00496D30)(node, random.Below(2))));
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

// --- emission, the member functions, the update ---

uint32_t g_rand;
bool g_feelerHits;
float g_hitValue;
uint32_t g_objectsFound; // FindObject: an object for ids with this bit set

// A script parameter the stand-in getters find (or not), its value's words.
struct FakeParam {
    uint32_t name;
    bool present;
    uint32_t value[4];
};
std::vector<FakeParam> g_params;
const FakeParam* FindParam(uint32_t name)
{
    for (const FakeParam& p : g_params)
        if (p.name == name)
            return p.present ? &p : nullptr;
    return nullptr;
}
// The parameters whose integer defaults are left uninitialised (setColor's): their first value isn't recorded.
bool UninitialisedInteger(uint32_t name)
{
    static const uint32_t names[] = {0xFEC1C2FB, 0x18A762F2, 0x757A8619, 0x05107296, 0x9C19232C, 0xD07EE8FA,
                                     0xBDA30C11, 0xCDC9F89E, 0x54C0A924, 0x043FD725, 0x69E233CE, 0x1988C741,
                                     0x808196FB};
    for (uint32_t n : names)
        if (n == name)
            return true;
    return false;
}

void StandInUpdateCalls(StandIns& s)
{
    s.At({0x00623BA3}, 0, [](Regs&) {
        g_rand = g_rand * 1103515245u + 12345u;
        Record({0x3BA3});
        return (g_rand >> 16) & 0x7FFF;
    });
    s.At({0x004766D0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(
                          &Script::CStruct::GetFloat))},
         12, [](Regs& r) {
             Record({0x66D0, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
             const FakeParam* p = FindParam(r.args[0]);
             if (!p)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = p->value[0];
             return 1u;
         });
    s.At({0x00476630, 0x00476CB0,
          AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         12, [](Regs& r) {
             Record({0x6630, r.args[0], UninitialisedInteger(r.args[0]) ? 0u : *reinterpret_cast<uint32_t*>(r.args[1]),
                     r.args[2] & 0xFF});
             const FakeParam* p = FindParam(r.args[0]);
             if (!p)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = p->value[0];
             return 1u;
         });
    s.At({0x00476FB0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int)>(
                          &Script::CStruct::GetVector))},
         12, [](Regs& r) {
             Record({0x6FB0, r.args[0], r.args[2]});
             const FakeParam* p = FindParam(r.args[0]);
             if (!p)
                 return 0u;
             std::memcpy(reinterpret_cast<void*>(r.args[1]), p->value, 16);
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}, 4,
         [](Regs& r) {
             Record({0x6B40, r.args[0]});
             return FindParam(r.args[0]) ? 1u : 0u;
         });
    // the feeler's query: its line recorded, a hit or not (its other methods are the real ones)
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        const auto* line = reinterpret_cast<const uint32_t*>(r.ecx);
        Record({0x72F0, r.args[0] & 0xFF, r.args[1] & 0xFF});
        for (int i = 0; i < 8; ++i)
            Record({line[i]});
        return g_feelerHits ? 1u : 0u;
    });
    // the hit's vertex value (returned in ST0): FLD g_hitValue; RET
    for (uint32_t at : {0x004174A0u, AddressOf(&Nx::CFeeler::GetHitVertexValue)}) {
        std::vector<uint8_t> code = {0xD9, 0x05};
        std::vector<uint8_t> address = Dword(reinterpret_cast<uint32_t>(&g_hitValue));
        code.insert(code.end(), address.begin(), address.end());
        code.push_back(0xC3);
        s.Bytes(at, code);
    }
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return r.args[0] & g_objectsFound ? 0x0B1E0000u : 0u;
    });
    s.At({0x0048C490}, 0, [](Regs& r) {
        const auto* centre = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xC490, centre[0], centre[1], centre[2], centre[3], r.args[1]});
        return centre[0] & 0x10 ? 1u : 0u;
    });
}

// A system with particles, positions and colours (at random), `steps` history steps and `colours` colours.
CParticleSystem* MakeLiveSystem(Lcg& random, int steps, int colours)
{
    auto* system = reinterpret_cast<CParticleSystem*>(Bump(0x150));
    RandomSystem(system, random);
    auto* flags = reinterpret_cast<uint8_t*>(system) + 0x125;
    for (int i = 0; i < 0x133 - 0x125 + 1; ++i)
        flags[i] = static_cast<uint8_t>(random.Below(2));
    system->m_dying = random.Below(8) == 0;
    int maxParticles = 1 + static_cast<int>(random.Below(12));
    system->m_maxParticles = maxParticles;
    auto* block = reinterpret_cast<int*>(Bump(4 + maxParticles * 0x30));
    *block = maxParticles;
    system->mp_particles = reinterpret_cast<Nx::sFlexibleParticle*>(block + 1);
    for (int i = 0; i < maxParticles; ++i) {
        Nx::sFlexibleParticle& p = system->mp_particles[i];
        for (int k = 0; k < 3; ++k) {
            p.velocity[k] = RandomFloat(random, -5, 5);
            p.force[k] = RandomFloat(random, -1, 1);
        }
        for (int k = 0; k < 2; ++k) {
            p.startSize[k] = RandomFloat(random, 0.1f, 3);
            p.endSize[k] = RandomFloat(random, 0.1f, 3);
        }
        p.lifetime = RandomFloat(random, 0.5f, 3);
        p.age = p.lifetime + RandomFloat(random, -0.6f, 0.2f);
    }
    auto* positions = reinterpret_cast<float*>(Bump(static_cast<uint32_t>(maxParticles * (steps > 0 ? steps : 1) * 12)));
    for (int i = 0; i < maxParticles * (steps > 0 ? steps : 1) * 3; ++i)
        positions[i] = RandomFloat(random, -50, 50);
    system->mp_positions = positions;
    uint8_t** arrays[3] = {&system->mp_startColour, &system->mp_midColour, &system->mp_endColour};
    for (uint8_t** a : arrays) {
        *a = reinterpret_cast<uint8_t*>(Bump(static_cast<uint32_t>((colours > 0 ? colours : 1) * 4)));
        for (int i = 0; i < (colours > 0 ? colours : 1) * 4; ++i)
            (*a)[i] = static_cast<uint8_t>(random.Next());
    }
    system->m_unknown8C = static_cast<int>(random.Below(static_cast<uint32_t>(maxParticles + 1)));
    system->m_sizeScale = random.Below(3) ? 1.0f : RandomFloat(random, 0.2f, 3);
    system->m_lastPos = system->m_pos;
    if (random.Below(3))
        system->m_lastPos.x += RandomFloat(random, -20, 20);
    if (random.Below(2))
        system->m_lastPos.z += RandomFloat(random, -20000, 20000);
    system->m_angleSpread = RandomFloat(random, 0, 2);
    g_steps = static_cast<uint32_t>(steps);
    g_colours = static_cast<uint32_t>(colours);
    return system;
}

// Emission: moving particles or not; the random spread, circular, FillPath (along a way or none), independent of the
// emitter; following a matrix; sizes scaled or not; no room, dying, a negative count; history steps 0 - 4.
void TestParticleSystemEmit()
{
    GameMemory& memory = *NewSystemMemory();
    StandIns& s = *new StandIns();
    StandInSystemCalls(s);
    StandInUpdateCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("particle system emit", reported, [&](bool original) {
            ResetRun(memory, static_cast<uint32_t>(round));
            Lcg random(0x7C00 + round);
            g_rand = 0x1357 + round;
            CParticleSystem* system = MakeLiveSystem(random, static_cast<int>(random.Below(5)), 2);
            if (random.Below(4) == 0) {
                auto* matrix = reinterpret_cast<Mth::Matrix*>(Bump(sizeof(Mth::Matrix)));
                for (Mth::Vector& row : matrix->row)
                    row = {RandomFloat(random, -1, 1), RandomFloat(random, -1, 1), RandomFloat(random, -1, 1),
                           RandomFloat(random, -1, 1)};
                system->mp_matrix = matrix;
            }
            if (random.Below(8) == 0)
                system->m_lastPos.x = std::numeric_limits<float>::quiet_NaN();
            int count = static_cast<int>(random.Below(16)) - (random.Below(6) == 0 ? 20 : 0);
            Fn<void(__thiscall*)(void*, int)>(original, 0x00496FE0)(system, count);
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "particle system: emission (moving or not, random / circular spread, FillPath, independent, scaled, "
              "following a matrix, no room, dying)");
}

// Every member function, its parameters there or not (the vectors' or x / y / z), and a name it doesn't know.
void TestParticleSystemMembers()
{
    GameMemory& memory = *NewSystemMemory();
    StandIns& s = *new StandIns();
    StandInSystemCalls(s);
    StandInUpdateCalls(s);
    g_relocate = ArenaRelocate;
    static const uint32_t names[] = {0x1CFBF078, 0x099CD17B, 0x2A479569, 0x2E66815E, 0x35B65BD4, 0x514B2584,
                                     0x482482C6, 0x63274395, 0x711B1A6A, 0x7911855B, 0x96896315, 0x8FFDEF38,
                                     0xA075EE45, 0xB373FB6E, 0xD3F1D333, 0xE3309AB5, 0xDB8621F7, 0xE985A9A6,
                                     0xFBB6720D, 0x12345678};
    static const uint32_t floats[] = {0x7323E97C, 0x0424D9EA, 0x9D2D8850, 0x745523AA, 0xF95D2E5F, 0x68CD967D,
                                      0xE5C59B88, 0x5B1D3822, 0x2013C0C6, 0x856BC5E2, 0x5E84E22F, 0x6289DD76,
                                      0x834A5F87, 0x73E5BAD0, 0x0AB21AF0, 0x055CE396, 0x4EEDFAE7, 0xD7E4AB5D};
    static const uint32_t integers[] = {0xFEC1C2FB, 0x18A762F2, 0x757A8619, 0x05107296, 0x9C19232C, 0xD07EE8FA,
                                        0xBDA30C11, 0xCDC9F89E, 0x54C0A924, 0x043FD725, 0x69E233CE, 0x1988C741,
                                        0x808196FB, 0x23BC5091, 0x00000000, 0xE9C4252A, 0x9D2E07B3, 0x6C4E18EB};
    static const uint32_t vectors[] = {0xFF7EBAF6, 0x7F261953, 0x68977CD7, 0xB990D003};
    bool ok = true, reported = false;
    for (int round = 0; round < 20 * 12; ++round) {
        ok &= CompareTwoRuns("particle system members", reported, [&](bool original) {
            ResetRun(memory, static_cast<uint32_t>(round));
            Lcg random(0x7D00 + round);
            g_rand = 0x2468 + round;
            CParticleSystem* system =
                MakeLiveSystem(random, static_cast<int>(random.Below(4)), static_cast<int>(random.Below(6)));
            g_params.clear();
            for (uint32_t name : floats) {
                FakeParam p{name, random.Below(3) != 0, {}};
                float f = random.Below(10) == 0 ? 0.0f : RandomFloat(random, -100, 100);
                if (name == 0x0424D9EA && random.Below(3) == 0)
                    f = 1.0f; // (setEmitTarget's default direction)
                std::memcpy(p.value, &f, 4);
                g_params.push_back(p);
            }
            for (uint32_t name : integers) {
                FakeParam p{name, random.Below(3) != 0, {}};
                p.value[0] = name == 0xFEC1C2FB ? random.Below(8) - 1 : random.Below(3) == 0 ? random.Below(3)
                                                                                              : random.Next();
                g_params.push_back(p);
            }
            for (uint32_t name : vectors) {
                FakeParam p{name, random.Below(2) != 0, {}};
                for (uint32_t& w : p.value) {
                    float f = random.Below(8) == 0 ? 0.0f : RandomFloat(random, -10, 10);
                    std::memcpy(&w, &f, 4);
                }
                if (name == 0xB990D003 && random.Below(3) == 0) { // (the default direction)
                    float up[3] = {0.0f, 1.0f, 0.0f};
                    std::memcpy(p.value, up, 12);
                }
                g_params.push_back(p);
            }
            g_params.push_back({0x652F5D69, random.Below(2) != 0, {}});
            g_feelerHits = random.Below(2) != 0;
            g_hitValue = RandomFloat(random, 0, 1);
            uint32_t name = names[round % 20];
            uint32_t result = Fn<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(original, 0x00497B00)(
                system, name, reinterpret_cast<void*>(0x5A5A0000), nullptr);
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(result & 0xFF);
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "particle system: every member function, its parameters there or not");
}

// Updates: dying (empty or not), following an object (there or gone), skipped, not shown; in view or not (emitting
// or not: ageing only); the rate's emission (none due; NaN); the scripts; moving particles or not, no time; the
// ground's brightness; expiry; the LOD count.
void TestParticleSystemUpdate()
{
    GameMemory& memory = *NewSystemMemory();
    GameMemory frame({{0x00700EF4, 4}});
    StandIns& s = *new StandIns();
    StandInSystemCalls(s);
    StandInUpdateCalls(s);
    s.At({0x004A09C0}, 0, [](Regs& r) {
        Record({0x4A09C0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_camera);
    });
    g_relocate = ArenaRelocate;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    bool ok = true, reported = false;
    for (int round = 0; round < 200; ++round) {
        ok &= CompareTwoRuns("particle system update", reported, [&](bool original) {
            ResetRun(memory, static_cast<uint32_t>(round));
            Lcg random(0x7E00 + round);
            g_rand = 0x3579 + round;
            CParticleSystem* system = MakeLiveSystem(random, static_cast<int>(random.Below(5)), 2);
            system->m_dying = random.Below(10) == 0;
            if (random.Below(10) == 0)
                system->m_unknown8C = 0;
            system->m_unknown78 = random.Below(4) == 0 ? random.Next() : 0;
            g_objectsFound = random.Below(2) ? 0xFFFFFFFF : 0;
            system->m_unknown12F = random.Below(8) != 0;
            system->m_unknown133 = random.Below(3) != 0;
            system->m_moving = random.Below(4) != 0;
            system->m_emitRate = random.Below(3) == 0 ? 0.0f : random.Below(20) == 0 ? nan : RandomFloat(random, 0, 90);
            system->m_emitOwed = RandomFloat(random, 0, 1);
            system->mp_emitScript = random.Below(4) == 0 ? reinterpret_cast<Script::CScript*>(Bump(0x10)) : nullptr;
            system->mp_updateScript = random.Below(3) == 0 ? reinterpret_cast<Script::CScript*>(Bump(0x10)) : nullptr;
            g_scriptResult = random.Below(2);
            g_feelerHits = random.Below(2) != 0;
            g_hitValue = RandomFloat(random, 0, 1);
            GameVariable<int>(0x00701378) = static_cast<int>(random.Below(3));
            g_camera = random.Below(5) ? reinterpret_cast<Gfx::Camera*>(Bump(0x200)) : nullptr;
            if (g_camera)
                *g_camera->GetPos() = {system->m_pos.x + RandomFloat(random, -300, 300), system->m_pos.y,
                                       system->m_pos.z + RandomFloat(random, -300, 300), 1.0f};
            system->m_lodNear = RandomFloat(random, 0, 1000);
            system->m_lodFar = system->m_lodNear + RandomFloat(random, 1, 90000);
            system->m_lodMinParticles = static_cast<int>(random.Below(6));
            GameVariable<float>(0x00700EF4) = RandomFloat(random, 0.5f, 2);
            float time = random.Below(8) == 0 ? 0.0f : RandomFloat(random, 0.001f, 0.5f);
            if (random.Below(3) == 0) { // (a time too small to change the ages as floats: on their lifetimes)
                time = std::ldexp(1.0f, -30);
                for (int i = 0; i < system->m_maxParticles; ++i)
                    if (random.Below(2))
                        system->mp_particles[i].age = system->mp_particles[i].lifetime;
            }
            bool skip = random.Below(10) == 0;
            if (round % 5 == 0) { // (out of view, not emitting: the particles only age - on their lifetimes)
                GameVariable<int>(0x00701378) = 1;
                if (!g_camera)
                    g_camera = reinterpret_cast<Gfx::Camera*>(Bump(0x200));
                system->m_unknown133 = true;
                system->m_unknown12F = true;
                system->m_dying = false;
                system->m_unknown78 = 0;
                system->m_emitRate = 0.0f;
                system->m_pos.x = std::bit_cast<float>(std::bit_cast<uint32_t>(system->m_pos.x) | 0x10);
                skip = false;
                time = std::ldexp(1.0f, -30);
                for (int i = 0; i < system->m_maxParticles; ++i)
                    if (random.Below(2))
                        system->mp_particles[i].age = system->mp_particles[i].lifetime;
            }
            Fn<void(__thiscall*)(void*, float, int)>(original, 0x004987F0)(
                system, time, skip ? 0x101 : static_cast<int>(random.Below(2)) << 8);
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            frame.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "particle system: updates (dying, following an object, skipped, out of view, emitting, scripts, "
              "moving, brightness, expiry, LOD)");
}

// The model type's colours: between two colours, scaled (clamped either way; NaN).
void TestModelParticlesColour()
{
    bool ok = true, reported = false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int round = 0; round < 400; ++round) {
        ok &= CompareTwoRuns("model particles colour", reported, [&](bool original) {
            Lcg random(0x7F00 + round);
            alignas(16) uint8_t storage[0x150];
            auto* system = reinterpret_cast<Nx::CModelParticles*>(storage);
            std::memset(storage, 0, sizeof(storage));
            for (float& f : system->m_colourScale)
                f = RandomFloat(random, -0.5f, 3);
            float brightness = random.Below(20) == 0 ? nan : RandomFloat(random, -0.5f, 3);
            system->m_unknownEC = std::bit_cast<uint32_t>(brightness);
            uint8_t from[4], to[4];
            for (int k = 0; k < 4; ++k) {
                from[k] = static_cast<uint8_t>(random.Next());
                to[k] = static_cast<uint8_t>(random.Next());
            }
            float t = random.Below(20) == 0 ? nan : RandomFloat(random, -0.5f, 1.5f);
            if (round % 4 == 1) { // (a scale whose product with the brightness rounds across a whole byte value)
                for (int tries = 0; tries < 100000; ++tries) {
                    float scale = RandomFloat(random, 0.5f, 2), bright = RandomFloat(random, 0.5f, 2);
                    uint8_t byte = static_cast<uint8_t>(1 + random.Below(200));
                    double product = static_cast<double>(scale) * bright;
                    double rounded = static_cast<float>(product);
                    if (static_cast<int64_t>(product * byte) != static_cast<int64_t>(rounded * byte) &&
                        product * byte < 255.0) {
                        system->m_colourScale[1] = system->m_colourScale[2] = scale;
                        system->m_unknownEC = std::bit_cast<uint32_t>(bright);
                        from[1] = to[1] = from[2] = to[2] = byte;
                        break;
                    }
                }
            } else if (round % 4 == 3) { // (exactly -1: below the clamp's 0)
                system->m_colourScale[0] = -1.0f;
                system->m_unknownEC = std::bit_cast<uint32_t>(1.0f);
                from[0] = to[0] = 1;
            }
            uint32_t out = 0xDDDDDDDD;
            uint32_t result = Fn<uint32_t(__thiscall*)(void*, uint32_t*, const uint8_t*, const uint8_t*, float)>(
                original, 0x00499EE0)(system, &out, from, to, t);
            return std::vector<uint32_t>{out, result == reinterpret_cast<uint32_t>(&out)};
        });
    }
    Check(ok, "particle system: the model type's colours");
}

} // namespace

SELFTEST(Early, TestParticleSystemLife, RemoveStandIns);
SELFTEST(Early, TestParticleSystemSettings, RemoveStandIns);
SELFTEST(Early, TestParticleSystemEmit, RemoveStandIns);
SELFTEST(Early, TestParticleSystemMembers, RemoveStandIns);
SELFTEST(Early, TestParticleSystemUpdate, RemoveStandIns);
SELFTEST(Early, TestModelParticlesColour, RemoveStandIns);
