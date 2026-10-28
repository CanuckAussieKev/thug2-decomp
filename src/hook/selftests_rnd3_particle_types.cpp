// Self-tests for the flexible particle types' engine classes (src/game/gfx/particle_types.cpp): the original and ours
// over stand-in systems in an arena, the memory manager and the base's calls stood in; the systems' memory, the
// results and the calls compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/particle_types.h"
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

void StandInTypeCalls(StandIns& s)
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
    StandInThis(s, {0x00474420, AddressOf(&Script::CScript::Destroy)}, 0);
    StandInCdecl(s, {0x004499B0, AddressOf(static_cast<void (*)(void*)>(&Script::CScript::operator delete))}, 1);
    StandInCdecl(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1);
    StandInThis(s, {0x00477130, AddressOf(&Script::CStruct::Clear)}, 0);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x401C30});
        (void)r;
        return 0u;
    });
}

GameMemory* NewTypeMemory()
{
    return new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00658555, 1}, {0x006FA7C0, 4}});
}
void ResetTypeRun(GameMemory& memory, uint32_t seed)
{
    g_trace.clear();
    ResetArena(0xEE);
    memory.Zero();
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
    GameVariable<uint32_t>(0x006FA7C0) = 0x3A9B0000;
    GameVariable<uint8_t>(0x00658555) = static_cast<uint8_t>(seed & 1);
}

// Every type constructed (with the memory there, or failing at each allocation in turn; odd counts and histories,
// a negative size) and destroyed - by its body, by its scalar deleting destructor (freed or not) and through the node.
void TestParticleTypesLife()
{
    GameMemory& memory = *NewTypeMemory();
    StandIns& s = *new StandIns();
    StandInTypeCalls(s);
    g_relocate = ArenaRelocate;
    struct Type {
        uint32_t construct, destroyBody, destroy, destroyFromNode;
    };
    const Type types[] = {
        {0x0049B0D0, 0x0049B070, 0x0049B0A0, 0x0049B090}, // ribbon
        {0x0049B380, 0x0049B320, 0x0049B350, 0x0049B340}, // ribbontrail
        {0x0049B660, 0x0049B600, 0x0049B630, 0x0049B620}, // shaded
        {0x0049B910, 0x0049B8A0, 0x0049B8E0, 0x0049B8D0}, // smooth
        {0x0049BBD0, 0x0049BB50, 0x0049BBA0, 0x0049BB90}, // smoothribbon
        {0x0049BE80, 0x0049BE20, 0x0049BE50, 0x0049BE40}, // smoothstar
        {0x0049C140, 0x0049C0D0, 0x0049C110, 0x0049C100}, // star
    };
    constexpr int kTypes = 7;
    bool ok = true, reported = false;
    for (int round = 0; round < kTypes * 24; ++round) {
        const Type& type = types[round % kTypes];
        ok &= CompareTwoRuns("particle types life", reported, [&](bool original) {
            ResetTypeRun(memory, static_cast<uint32_t>(round));
            Lcg random(0x3D00 + round);
            std::vector<uint32_t> trace;
            auto* system = reinterpret_cast<CParticleSystem*>(Bump(0x150));
            int failAt = (round / kTypes) % 8; // (0: none; else the allocation that fails)
            for (int i = 1; i <= failAt; ++i)
                g_failAllocations.push_back(i == failAt);
            int maxParticles = 1 + static_cast<int>(random.Below(9));
            int history = static_cast<int>(random.Below(6)) - 1; // (-1: no steps)
            if (round % 11 == 5)
                history = -4; // (a negative size: the positions' allocation fails when zeroed)
            float size = static_cast<float>(random.Below(1000)) / 7.0f;
            trace.push_back(ArenaRelocate(
                Fn<uint32_t(__thiscall*)(void*, uint32_t, int, uint32_t, uint32_t, int, int, float, int)>(
                    original, type.construct)(system, random.Next(), maxParticles, random.Next(), random.Next(),
                                              static_cast<int>(random.Below(9)), 3 + static_cast<int>(random.Below(9)),
                                              size, history)));
            g_failAllocations.clear();
            auto* other = reinterpret_cast<Mem::ListNode*>(Bump(sizeof(Mem::ListNode)));
            other->next = other->prev = &system->m_node;
            system->m_node.next = system->m_node.prev = other;
            system->mp_emitScript = nullptr;
            system->mp_updateScript = nullptr;
            system->mp_scriptParams = nullptr;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            g_trace.clear();
            unsigned int freeFlags = (round / 56) & 1 ? 0 : 1;
            switch ((round / kTypes) % 3) {
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
    // the folded counts
    ok &= CompareTwoRuns("particle type counts", reported, [&](bool original) {
        std::vector<uint32_t> trace;
        uint32_t fields[0x50];
        for (uint32_t& f : fields)
            f = 0;
        for (int value : {0, 7, -3, 0x12345}) {
            fields[0x134 / 4] = static_cast<uint32_t>(value);
            for (uint32_t address : {0x0049B8C0u, 0x0049BB70u, 0x0049C0F0u, 0x0049BB80u})
                trace.push_back(static_cast<uint32_t>(Fn<int(__thiscall*)(void*)>(original, address)(fields)));
        }
        return trace;
    });
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "particle types: ribbon ... star constructed (memory failing at each step), destroyed every way; counts");
}

} // namespace

SELFTEST(Early, TestParticleTypesLife, RemoveStandIns);
