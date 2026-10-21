// Self-tests for the stats manager component (game/skater/stats_manager.cpp): the originals against ours over random
// components and runs of frames, every call recorded by stand-ins (hook/standins.h).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/objman.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/skater/stats_manager.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CStatsManagerComponent;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    float Range(float low, float high) { return low + (high - low) * static_cast<float>(Below(10001)) / 10000.0f; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

alignas(16) uint8_t g_component[sizeof(CStatsManagerComponent) + 0x40];
alignas(16) uint8_t g_object[0x200];   // the component's object
alignas(16) uint8_t g_skater[0x300];   // the skater found by id
alignas(16) uint8_t g_state[0x200];    // its SkaterState
alignas(16) uint8_t g_control[0x40];   // its physics control (+0x2C: g_controlState)
alignas(16) uint8_t g_controlState[0x200];
alignas(16) uint8_t g_score[0x80];     // +0x14 points, +0x60 multiplier
alignas(16) uint8_t g_profile[0x80];
alignas(16) uint8_t g_skate[0x400];    // the skate module (+0x378)
alignas(16) uint8_t g_structs[16][0x10];
alignas(16) uint8_t g_goalArray[0xC];
alignas(16) uint8_t g_goalSources[8][0x10];
alignas(16) uint8_t g_block[2][sizeof(CStatsManagerComponent) + 0x10];
void* g_fakeVtable[16];

int g_nextStruct, g_blockNext;
bool g_flag1DED, g_found[8], g_hasName, g_flowFlag, g_skaterFound;
uint32_t g_profileName;
int g_difficulty, g_globalInt, g_goalCount;
uint32_t g_counts[4];

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, size_t each, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        if (p >= b && p < b + size)
            return tag + static_cast<uint32_t>((p - b) / each) * 0x1000 + static_cast<uint32_t>((p - b) % each);
        return 0;
    };
    if (!p)
        return 0;
    for (auto r : {in(g_component, sizeof(g_component), sizeof(g_component), 0x71000000),
                   in(g_skater, sizeof(g_skater), sizeof(g_skater), 0x72000000),
                   in(g_structs, sizeof(g_structs), sizeof(g_structs[0]), 0x73000000),
                   in(g_score, sizeof(g_score), sizeof(g_score), 0x74000000),
                   in(g_profile, sizeof(g_profile), sizeof(g_profile), 0x75000000),
                   in(g_goalSources, sizeof(g_goalSources), sizeof(g_goalSources[0]), 0x76000000),
                   in(g_goalArray, sizeof(g_goalArray), sizeof(g_goalArray), 0x77000000),
                   in(g_block, sizeof(g_block), sizeof(g_block[0]), 0x78000000),
                   in(g_state, sizeof(g_state), sizeof(g_state), 0x79000000),
                   in(g_control, sizeof(g_control), sizeof(g_control), 0x7A000000)})
        if (r)
            return r;
    return 0xBAD00000;
}

const char* const kNames[] = {"Ollie King", "", "Manual Master", "Grind 10 seconds of joy"};

void StandIns_(StandIns& s)
{
    using Script::CStruct;
    auto newStruct = [](Regs&) {
        Record({0x4E5});
        return reinterpret_cast<uint32_t>(g_structs[g_nextStruct++ & 15]);
    };
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, newStruct);
    s.At({0x005BA3D0}, 0, newStruct);
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) { Record({0x1417, Id(r.ecx)}); return r.ecx; });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0xC1EA, Id(r.ecx)}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0xDE1, Id(r.args[0])}); return 0u; });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Record({0xADD2, Id(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0xADD1, Id(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Record({0x6E75, r.args[0]});
        return reinterpret_cast<uint32_t>(kNames[r.args[0] & 3]);
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x6E71, r.args[0]});
        return static_cast<uint32_t>(g_globalInt);
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x6EA4, r.args[0]});
        return reinterpret_cast<uint32_t>(g_goalArray);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0x6E1E, Id(r.ecx), r.args[0]});
        if (r.ecx == reinterpret_cast<uint32_t>(g_goalArray))
            return reinterpret_cast<uint32_t>(g_goalSources[r.args[0] & 7]);
        return r.args[0] * 3 + Id(r.ecx);
    });
    s.At({0x0046CB70, AddressOf(&Script::CArray::GetLocalString)}, 4, [](Regs& r) {
        Record({0x6E15, Id(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(kNames[(r.args[0] + 1) & 3]);
    });
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [](Regs& r) {
             bool has = g_found[((r.args[0] >> 5) ^ Id(r.ecx)) & 7];
             Record({0xC0A7, Id(r.ecx), r.args[0], has});
             return static_cast<uint32_t>(has);
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6FC5, Id(r.ecx), r.args[0], r.args[2]});
             if (r.ecx == reinterpret_cast<uint32_t>(g_profile + 0x5C)) {
                 if (g_hasName)
                     *reinterpret_cast<uint32_t*>(r.args[1]) = g_profileName;
                 return static_cast<uint32_t>(g_hasName);
             }
             static const uint32_t values[] = {0x39E7537B, 0x7E064AD0, 0x1DBBB148, 0x430B80C4, 0x509ACA95,
                                               0x4EC3CFB5, 0x5B6556A4, 0x364E8C16, 0x68703AE9, 0x69B0F836,
                                               0x8F6F9F05, 0xC3EC3EED, 0x9068B4E5, 0xC1336019, 0xD9FFC141,
                                               0xB67CFF14, 0x6DCB497C, 0x9B65D7B8, 0xF73A13E3, 0x12345678};
             *reinterpret_cast<uint32_t*>(r.args[1]) = values[(Id(r.ecx) / 0x1000 * 7 + r.args[0]) % 20];
             return 1u;
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [](Regs& r) {
             Record({0x6FA7, Id(r.ecx), r.args[0], r.args[2], *reinterpret_cast<uint32_t*>(r.args[1])});
             *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_structs[12]);
             return 1u;
         });
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             Record({0x6F57, Id(r.ecx), r.args[0], r.args[2], Hash(*reinterpret_cast<const char**>(r.args[1]))});
             if (g_found[3])
                 *reinterpret_cast<const char**>(r.args[1]) = kNames[Id(r.ecx) / 0x1000 & 3];
             return static_cast<uint32_t>(g_found[3]);
         });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Record({0x6F17, Id(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(g_counts[3] % 70);
             return 1u;
         });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5C41, r.args[0], Id(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0xDB6, r.args[0], r.args[0] == 0x0064C2FC ? 0u : r.args[1]}); // (that message has no argument)
        return 0u;
    });
}

void StandInGame(StandIns& s)
{
    s.At({0x00553220}, 4, [](Regs& r) { Record({0x5532, r.args[0]}); return static_cast<uint32_t>(g_flag1DED); });
    s.At({0x00564FC0}, 4, [](Regs& r) {
        Record({0x564F, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profile);
    });
    s.At({0x005591B0}, 0, [](Regs&) { Record({0x5591}); return 0x5F5F5F5Fu; });
    s.At({0x00559050}, 4, [](Regs& r) { Record({0x5590, r.ecx, r.args[0] & 0xFF}); return static_cast<uint32_t>(g_difficulty); });
    s.At({0x00557E30}, 0, [](Regs& r) { Record({0x557E, r.ecx}); return static_cast<uint32_t>(g_flowFlag); });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x4619, r.args[0]});
        return g_skaterFound ? reinterpret_cast<uint32_t>(g_skater) : 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x5804, Id(r.ecx)});
        return reinterpret_cast<uint32_t>(g_score);
    });
    s.At({0x00580BE0, AddressOf(&Obj::CSkater::SetStatsFromProfile)}, 4, [](Regs& r) {
        Record({0x580B, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x00585FB0, AddressOf(&Obj::CSkaterProfile::GetStat)}, 4, [](Regs& r) {
        Record({0x585F, Id(r.ecx), r.args[0]});
        return r.args[0] & 7;
    });
    s.At({0x00585FD0, AddressOf(&Obj::CSkaterProfile::SetStat)}, 8, [](Regs& r) {
        Record({0x585E, Id(r.ecx), r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x005624C0}, 0x10, [](Regs& r) {
        Record({0x5624, Id(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return g_counts[r.args[3] & 1] + (r.args[0] == 0x89B7DBE4 ? 3 : 0);
    });
    s.At({0x00562370}, 4, [](Regs& r) {
        Record({0x5623, Id(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_counts[2];
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA11, r.args[0]});
        return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) { Record({0xF5EE, Id(r.args[0])}); return 0u; });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD7, Id(r.ecx), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
        return 0u;
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0x4E6, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    g_fakeVtable[5] = s.Thunk(4, [](Regs& r) { Record({0x5E5, Id(r.ecx), Id(r.args[0])}); return 0u; });
}

void Build(Random& r)
{
    auto* c = reinterpret_cast<CStatsManagerComponent*>(g_component);
    for (size_t i = 0; i < sizeof(g_component); ++i)
        g_component[i] = static_cast<uint8_t>(r.Next());
    c->m_vtable = g_fakeVtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    c->m_skater = r.OneIn(10) ? nullptr : reinterpret_cast<Obj::CCompositeObject*>(g_skater);
    c->m_skaterState = reinterpret_cast<Obj::CBaseComponent*>(g_state);
    c->m_physicsControl = reinterpret_cast<Obj::CBaseComponent*>(g_control);
    c->m_activated = !r.OneIn(8);
    c->m_raiseStats = r.Bit();
    c->m_goalDone = r.Bit();
    c->m_firstUpdate = r.OneIn(6);
    c->m_airPhase = static_cast<int>(r.Below(3));
    c->m_jumpPhase = static_cast<int>(r.Below(3));
    static const uint32_t states[] = {0x26A7CADF, 0xB39B4F1B, 0x439F4704, 0};
    c->m_lastState = states[r.Below(4)];
    c->m_value58 = r.Range(0.0f, 40.0f);
    c->m_countA80 = static_cast<int>(r.Below(6));
    c->m_countA84 = static_cast<int>(r.Below(6));
    c->m_airBest = static_cast<int>(r.Below(5000));
    c->m_airHeight = r.Range(0.0f, 300.0f);
    c->m_airStartPoints = static_cast<int>(r.Below(3000));
    c->m_airStartMultiplier = r.Range(0.0f, 4.0f);
    for (Mth::Vector* v : {&c->m_airStart, &c->m_airEnd, &c->m_jumpStart, &c->m_jumpEnd})
        *v = {r.Range(-500.0f, 500.0f), r.Range(-200.0f, 200.0f), r.Range(-500.0f, 500.0f), 1.0f};
    bool whole = r.OneIn(3);
    if (whole) {
        // Whole feet: rises, drops, heights and distances that tie with whole-number targets.
        for (Mth::Vector* v : {&c->m_airStart, &c->m_airEnd, &c->m_jumpStart, &c->m_jumpEnd})
            *v = {12.0f * static_cast<float>(r.Below(8)), 12.0f * static_cast<float>(r.Below(8)), 24.0f, 1.0f};
        c->m_airHeight = 12.0f * static_cast<float>(r.Below(8));
        static const float starts[] = {0.0f, 1.0f, 1.5f, 6.0f};
        c->m_airStartMultiplier = starts[r.Below(4)];
        c->m_airBest = static_cast<int>(r.Below(3));
    }
    static const uint32_t kinds[] = {0x39E7537B, 0x7E064AD0, 0x1DBBB148, 0x430B80C4, 0x509ACA95, 0x4EC3CFB5,
                                     0x5B6556A4, 0x364E8C16, 0x68703AE9, 0x69B0F836, 0x8F6F9F05, 0xC3EC3EED,
                                     0x9068B4E5, 0xC1336019, 0xD9FFC141, 0xB67CFF14, 0x11111111};
    static const uint32_t stats[] = {0x6DCB497C, 0x439F4704, 0x9016B4E7, 0xAE798769, 0x9B65D7B8, 0xAF895B3F,
                                     0xEDF5DB70, 0xB1FC0722, 0xF0D90109, 0xF73A13E3, 0x22222222};
    for (auto& goal : c->m_goals) {
        goal.m_kind = kinds[r.Below(17)];
        goal.m_stat = stats[r.Below(11)];
        goal.m_target = static_cast<int>(r.Below(40));
        if (r.OneIn(3))
            goal.m_target *= 50;
        goal.m_met = r.OneIn(4);
        goal.m_status = r.OneIn(3) ? static_cast<int>(r.Below(4)) : 0;
        goal.m_name = kNames[r.Below(4)];
        goal.m_text = r.OneIn(3) ? nullptr : kNames[r.Below(4)];
        goal.m_trick = r.OneIn(3) ? 0xEF24413Bu : r.Next();
        goal.m_value20 = r.Below(3);
        if (whole)
            goal.m_target = static_cast<int>(r.Below(8));
    }
    if (whole) {
        c->m_jumpStart.y = 0.0f;
        c->m_airStart.y = 0.0f;
    }
    At<uint32_t>(g_object, 0x14) = 0x40 + r.Below(8);
    std::memset(g_skate, 0, sizeof(g_skate));
    At<uint32_t>(g_skate, 0x378) = r.OneIn(8) ? 0xE92ECAFE : 0;
    g_flag1DED = !r.OneIn(8);
    g_hasName = !r.OneIn(8);
    g_profileName = r.OneIn(8) ? 0x1234 : 0x0A7BE964;
    g_skaterFound = !r.OneIn(6);
    g_flowFlag = r.Bit();
    g_difficulty = static_cast<int>(r.Below(3));
    g_globalInt = static_cast<int>(r.Below(3));
    for (bool& f : g_found)
        f = !r.OneIn(3);
    for (uint32_t& n : g_counts)
        n = r.Below(30);
    std::memset(g_goalArray, 0, sizeof(g_goalArray));
    reinterpret_cast<Script::CArray*>(g_goalArray)->m_size = r.Below(9);
    // The skater's components.
    At<uint32_t>(g_state, 4) = 0x43686585;
    At<uint32_t>(g_control, 4) = 0xD9151D4D;
    At<uint8_t*>(g_state, 0x10) = g_control;
    At<uint8_t*>(g_control, 0x10) = nullptr;
    At<uint8_t*>(g_skater, 0x100) = r.OneIn(8) ? nullptr : g_state;
    At<uint8_t*>(g_control, 0x2C) = g_controlState;
    g_nextStruct = 0;
}

// One frame's skater: place, physics state, flags, score.
void Frame(Random& r)
{
    auto* c = reinterpret_cast<CStatsManagerComponent*>(g_component);
    float pos[4] = {r.Range(-500.0f, 500.0f), r.Range(-200.0f, 200.0f), r.Range(-500.0f, 500.0f), 1.0f};
    if (r.OneIn(3)) {
        pos[0] = 12.0f * static_cast<float>(r.Below(8));
        pos[1] = 12.0f * static_cast<float>(r.Below(8));
        pos[2] = 24.0f;
    } else if (r.OneIn(3)) {
        pos[1] = c->m_airStart.y + c->m_airHeight + r.Range(-0.5f, 1.5f); // (just above the highest so far)
    }
    std::memcpy(g_object + 0x4C, pos, 16);
    At<int>(g_state, 0x18) = static_cast<int>(r.Below(3));
    g_state[0x30] = r.OneIn(3);
    g_state[0x70] = r.OneIn(5);
    g_controlState[0x118] = r.OneIn(6);
    At<int>(g_controlState, 0x1C) = r.OneIn(6) ? 1 : 0;
    g_control[0x25] = r.Bit();
    At<int>(g_score, 0x14) = static_cast<int>(r.Below(5000));
    static const float multipliers[] = {0.0f, 1.0f, 2.5f, 7.0f, 1.25f};
    At<float>(g_score, 0x60) = multipliers[r.Below(5)];
    if (r.OneIn(3)) { // a point or two over the take-off's, at x1
        At<int>(g_score, 0x14) = c->m_airStartPoints + static_cast<int>(r.Below(3));
        static const float simple[] = {1.0f, 2.5f, 7.0f};
        At<float>(g_score, 0x60) = simple[r.Below(3)];
    }
}

void Trace()
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_component);
    for (size_t i = 0; i < sizeof(CStatsManagerComponent) / 4; ++i) {
        uint32_t v = w[i];
        uint32_t id = Id(v);
        g_trace.push_back(id == 0xBAD00000 ? v : id);
    }
}

void TestStatsManager()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x007CE470, 4, {}}, {0x007CE478, 4, {}}, {0x006F0FB8, 4, {}},
                                {0x006F3614, 4, {}}, {0x006F361C, 16, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int scripts = 0;
    {
        auto* s = new StandIns;
        StandIns_(*s);
        StandInGame(*s);
        GameVariable<void*>(0x007D1800) = g_block[1];
        GameVariable<void*>(0x007CE478) = g_skate;
        GameVariable<int>(0x006F3614) = 1;
        GameVariable<uint32_t>(0x006F361C + 4) = 0x12345678;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{4242424};
            for (int n = 0; n < 3000; ++n) {
                Build(r);
                auto* c = reinterpret_cast<CStatsManagerComponent*>(g_component);
                int which = static_cast<int>(r.Below(8));
                Record({0xCA5E, static_cast<uint32_t>(which)});
                size_t before = g_trace.size();
                switch (which) {
                case 0:
                case 1:
                case 2: {
                    // A run of frames.
                    int frames = 1 + static_cast<int>(r.Below(6));
                    for (int f = 0; f < frames; ++f) {
                        Frame(r);
                        if (original)
                            Original<void(__thiscall*)(void*)>(0x0051CF90)(c);
                        else
                            c->Update();
                        Trace();
                    }
                    break;
                }
                case 3: {
                    static const uint32_t tricks[] = {0x8D10119D, 0x255ED86F, 0x0AC90769, 0xEF24413B, 0xA549B57B, 0x1};
                    uint32_t trick = tricks[r.Below(6)];
                    float time = r.OneIn(4) ? static_cast<float>(r.Below(40)) : r.Range(0.0f, 40.0f);
                    if (original)
                        Original<void(__thiscall*)(void*, uint32_t, float, int)>(0x0051D6A0)(c, trick, time, 0);
                    else
                        c->CheckBalanceTime(trick, time, 0);
                    break;
                }
                case 4:
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051CBF0)(c);
                    else
                        c->AnnounceMetGoals();
                    Trace();
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x0051CA00)(c);
                    else
                        c->Reset();
                    break;
                case 5:
                    if (original)
                        Original<void(__thiscall*)(void*, void*)>(0x0051C670)(c, nullptr);
                    else
                        c->InitFromStructure(nullptr);
                    break;
                case 6: {
                    uint32_t name = r.OneIn(3) ? 0xB9AE26D2u : (r.OneIn(2) ? 0xBA15FC7Fu : 0x5u);
                    int a = static_cast<int>(r.Next()), b = r.Bit() ? c->m_value14 : 7;
                    float value = r.Range(0.0f, 50.0f);
                    int index = static_cast<int>(r.Below(70)), status = static_cast<int>(r.Below(4));
                    int got;
                    bool active;
                    if (original) {
                        Original<void(__thiscall*)(void*, uint32_t)>(0x0051CAE0)(c, name);
                        Original<void(__thiscall*)(void*, int, int, int)>(0x0051C9D0)(c, a, b, 0);
                        Original<void(__thiscall*)(void*, float)>(0x0051CAC0)(c, value);
                        Original<void(__thiscall*)(void*, int)>(0x0051CE80)(c, index);
                        Original<void(__thiscall*)(void*, int, int)>(0x0051C480)(c, index, status);
                        got = Original<int(__thiscall*)(void*, int)>(0x0051C470)(c, (index + 3) % 70);
                        active = Original<bool(__thiscall*)(void*)>(0x0051C950)(c);
                    } else {
                        c->Count(name);
                        c->Set10(a, b, 0);
                        c->SetValue58(value);
                        c->MeetGoal(index);
                        c->SetGoalStatus(index, status);
                        got = c->GetGoalStatus((index + 3) % 70);
                        active = c->IsActive();
                    }
                    Record({static_cast<uint32_t>(got), active});
                    break;
                }
                default: {
                    // The member functions, then create / destroy.
                    static const uint32_t members[] = {0x0051C4A0, 0x0051CB20, 0x0051C4C0, 0x0051C4F0, 0x0051CB30,
                                                       0x0051C520, 0x0051C550, 0x0051CB70, 0x0051C580};
                    alignas(16) uint8_t script[0x40] = {};
                    At<uint8_t*>(script, 0x14) = g_structs[13];
                    int member = static_cast<int>(r.Below(9));
                    bool result;
                    auto* params = reinterpret_cast<Script::CStruct*>(g_structs[14]);
                    auto* sc = reinterpret_cast<Script::CScript*>(script);
                    if (original) {
                        result = Original<bool(__thiscall*)(void*, void*, void*)>(members[member])(c, params, sc);
                    } else {
                        switch (member) {
                        case 0: result = c->ReInit(params, sc); break;
                        case 1: result = c->ResetGoals(params, sc); break;
                        case 2: result = c->ActivateGoals(params, sc); break;
                        case 3: result = c->DeactivateGoals(params, sc); break;
                        case 4: result = c->GetGoalStatusMember(params, sc); break;
                        case 5: result = c->UnlockAmGoals(params, sc); break;
                        case 6: result = c->UnlockProGoals(params, sc); break;
                        case 7: result = c->GetLastAirComboScore(params, sc); break;
                        default: result = c->ClearLastAirComboScore(params, sc); break;
                        }
                    }
                    Record({result});
                    std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                    g_blockNext = r.OneIn(4) ? -1 : 0;
                    void* made = original ? Original<void*(__cdecl*)()>(0x0051CB90)() : CStatsManagerComponent::Create();
                    Record({Id(reinterpret_cast<uint32_t>(made))});
                    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_block[0]);
                    g_trace.insert(g_trace.end(), w, w + sizeof(CStatsManagerComponent) / 4);
                    if (made) {
                        unsigned flags = r.Below(2);
                        void* result2 = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x0051C640)(made, flags)
                                                 : static_cast<CStatsManagerComponent*>(made)->Destroy(flags);
                        Record({Id(reinterpret_cast<uint32_t>(result2)), At<uint32_t>(g_block[0], 0)});
                    }
                    if (original)
                        Original<void(__cdecl*)()>(0x0051C440)();
                    else
                        CStatsManagerComponent::RegisterMemberFunctions();
                    break;
                }
                }
                if (original)
                    for (size_t k = before; k < g_trace.size(); ++k)
                        scripts += g_trace[k] == 0x5C41;
                Trace();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Log("  stats manager: %d scripts run by the original", scripts);
    Check(scripts > 500, "stats manager: the cases reach the goals");
    Compare(traces[0], traces[1], "stats manager: frames (air, jumps, every goal kind), balance times, announcements, init");
}

} // namespace

SELFTEST(Early, TestStatsManager, RemoveStandIns);
