// Self-tests for the skater's state history component (game/skater/state_history.cpp): its small methods, the network
// collision check and the two hit message handlers - the originals against ours, every call they make recorded by
// stand-ins (hook/standins.h).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/skater/state_history.h"
#include "game/skater/walk_physics.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CSkaterStateHistoryComponent;

// A small deterministic generator (the same sequence for both runs).
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

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
uint32_t Bits(double d)
{
    float f = static_cast<float>(d);
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// --- the world the code sees ------------------------------------------------------------------------------------

constexpr int kSkaters = 6;
alignas(16) uint8_t g_skaters[kSkaters][0x300];  // composite objects (+0x14 id, +0x4C pos, +0xAC vel, +0x100 components)
alignas(16) uint8_t g_states[kSkaters][0x200];   // their SkaterState components
alignas(16) uint8_t g_histories[kSkaters][0x40]; // their state history components (+0xC the object)
alignas(16) uint8_t g_controls[kSkaters][0x40];  // physics control components (+0x25, +0x2C the state)
alignas(16) uint8_t g_cores[kSkaters][0x20];     // core physics
alignas(16) uint8_t g_walks[kSkaters][0x20];     // walk
alignas(16) uint8_t g_inputs[kSkaters][0x500];   // input components (+0x498)
alignas(16) uint8_t g_scoreHolders[kSkaters][0x20];
alignas(16) uint8_t g_scores[kSkaters][0x20];    // +0 the stand-in score
constexpr int kPlayers = kSkaters + 2;
alignas(16) uint8_t g_players[kPlayers][0x200];  // +0x14 object, +0x18 connection holder, +0x30 team, +0x1D0 flags
alignas(16) uint8_t g_connections[kPlayers][0x40];
alignas(16) uint8_t g_netManager[0x100];         // +0 flags, +4 the app, +0x3A, +0x6C the local skater
alignas(16) uint8_t g_app[0x40];
alignas(16) uint8_t g_gameMode[0x20];
alignas(16) uint8_t g_skaterManager[0x20];
alignas(16) uint8_t g_message[0x40];
alignas(16) uint8_t g_context[sizeof(Net::MsgHandlerContext)];

int g_slots[8];          // the skater in each of the skater manager's slots (-1: none)
bool g_observing[kPlayers];
bool g_mayCollide, g_slapGame, g_teamGame, g_tagOver;
uint32_t g_gameType;
int g_playerOfId[0x80];  // PlayerByObjectId: the player of an id (-1: none)
float g_floats[8];       // the script floats, by FloatIndex

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// A pointer as a small number the same in both runs.
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
    if (uint32_t r = in(g_skaters, sizeof(g_skaters), sizeof(g_skaters[0]), 0x10000000))
        return r;
    if (uint32_t r = in(g_players, sizeof(g_players), sizeof(g_players[0]), 0x20000000))
        return r;
    if (uint32_t r = in(g_cores, sizeof(g_cores), sizeof(g_cores[0]), 0x30000000))
        return r;
    if (uint32_t r = in(g_walks, sizeof(g_walks), sizeof(g_walks[0]), 0x40000000))
        return r;
    if (uint32_t r = in(g_scores, sizeof(g_scores), sizeof(g_scores[0]), 0x50000000))
        return r;
    if (uint32_t r = in(g_app, sizeof(g_app), sizeof(g_app), 0x60000000))
        return r;
    if (uint32_t r = in(g_histories, sizeof(g_histories), sizeof(g_histories[0]), 0x70000000))
        return r;
    if (uint32_t r = in(g_netManager, sizeof(g_netManager), sizeof(g_netManager), 0x80000000))
        return r;
    if (uint32_t r = in(g_gameMode, sizeof(g_gameMode), sizeof(g_gameMode), 0x90000000))
        return r;
    return 0xBAD00000;
}

int FloatIndex(uint32_t name)
{
    switch (name) {
    case 0x25F78481: return 0;
    case 0x0D24273B: return 1;
    case 0x0A49FF23: return 2;
    case 0xC5819249: return 3;
    case 0xDBD6D58A: return 4;
    case 0xECB41860: return 5;
    case 0xF2E35FA3: return 6;
    case 0xEEF4AFB3: return 7;
    default: return -1;
    }
}
float __cdecl StandInGetFloat(uint32_t name)
{
    Record({0xF10A7, name});
    int index = FloatIndex(name);
    return index >= 0 ? g_floats[index] : -999.0f;
}
float __cdecl StandInGetFloatByName(const char* name)
{
    Record({0xF10A8, reinterpret_cast<uint32_t>(name)}); // (the exe's string, the same address in both runs)
    return 12345.0f;
}

// Every function the code calls, stood in for (the game's and our versions alike).
void StandInWorld(StandIns& s)
{
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInGetFloat)));
    for (uint32_t address : {0x00478F30u, AddressOf(static_cast<float (*)(const char*)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&StandInGetFloatByName)));
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0x53AE, Id(r.ecx), r.args[0] & 0xFFFF});
        int player = g_playerOfId[r.args[0] & 0x7F];
        return player >= 0 ? reinterpret_cast<uint32_t>(g_players[player]) : 0u;
    });
    s.At({0x00543910, AddressOf(&GameNet::PlayerInfo::IsInvulnerable)}, 0, [](Regs& r) {
        Record({0x5439, Id(r.ecx)});
        uint32_t index = (r.ecx - reinterpret_cast<uint32_t>(g_players)) / 0x200;
        return index < kPlayers ? static_cast<uint32_t>(g_observing[index]) : 0xDEADu;
    });
    s.At({0x00534340, AddressOf(&GameNet::Manager::PlayerCollisionEnabled)}, 0, [](Regs& r) { Record({0x5343, Id(r.ecx)}); return static_cast<uint32_t>(g_mayCollide) | 0x1200; });
    s.At({0x005B3470}, 0, [](Regs& r) { Record({0x5B34, r.args[0], r.args[1]}); return static_cast<uint32_t>(g_slapGame); });
    s.At({0x005532A0}, 0, [](Regs& r) { Record({0x5532, Id(r.ecx)}); return g_gameType; });
    s.At({0x005535F0}, 0, [](Regs& r) { Record({0x5535, Id(r.ecx)}); return static_cast<uint32_t>(g_teamGame); });
    s.At({0x00553910}, 0, [](Regs& r) { Record({0x5539, Id(r.ecx)}); return static_cast<uint32_t>(g_tagOver); });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x5665, r.args[0]});
        int slot = g_slots[r.args[0] & 7];
        return slot >= 0 ? reinterpret_cast<uint32_t>(g_skaters[slot]) : 0u;
    });
    s.At({0x00581580, AddressOf(&Obj::CSkater::GetDisplayName)}, 0, [](Regs& r) {
        Record({0x5815, Id(r.ecx)});
        static const char* names[] = {"Tony", "a name of exactly thirty-two chs", "Bob",
                                      "", "Elissa", "a name that runs well past thirty-two characters"};
        return reinterpret_cast<uint32_t>(names[((r.ecx - reinterpret_cast<uint32_t>(g_skaters)) / 0x300) % 6]);
    });
    s.At({0x00561B40}, 0, [](Regs& r) {
        Record({0x561B, Id(r.ecx)});
        return At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0);
    });
    s.At({0x00561A80}, 4, [](Regs& r) {
        Record({0x561A, Id(r.ecx), r.args[0]});
        At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0) = r.args[0];
        return 0u;
    });
    s.At({0x004FE1D0}, 4, [](Regs& r) { Record({0x4FE1, Id(r.ecx), Id(r.args[0])}); return 0u; });
    s.At({0x0052C2B0, AddressOf(&Obj::CWalkComponent::GoToAirFor)}, 4, [](Regs& r) { Record({0x52C2, Id(r.ecx), Id(r.args[0])}); return 0u; });
    s.At({0x00543350, AddressOf(&GameNet::PlayerInfo::MarkJustJoined)}, 0, [](Regs& r) { Record({0x5433, Id(r.ecx)}); return 0u; });
    s.At({0x00544340, AddressOf(&GameNet::PlayerInfo::MarkAsKing)}, 4, [](Regs& r) { Record({0x5443, Id(r.ecx), r.args[0] & 0xFF}); return 0u; });
    s.At({0x00544110, AddressOf(&GameNet::PlayerInfo::RetrievedFlag)}, 0, [](Regs& r) { Record({0x5441, Id(r.ecx)}); return 0u; });
    s.At({0x00543C70, AddressOf(&GameNet::PlayerInfo::StoleFlag)}, 4, [](Regs& r) { Record({0x543C, Id(r.ecx), r.args[0]}); return 0u; });
    // The player searches: the search's node counts the players handed out.
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x5372, Id(r.ecx), r.args[1], At<uint32_t>(reinterpret_cast<void*>(r.args[0]), 0)});
        At<uint32_t>(reinterpret_cast<void*>(r.args[0]), 4) = 1;
        return reinterpret_cast<uint32_t>(g_players[0]);
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        uint32_t& next = At<uint32_t>(reinterpret_cast<void*>(r.args[0]), 4);
        Record({0x5370, Id(r.ecx), r.args[1], next});
        return next < kPlayers ? reinterpret_cast<uint32_t>(g_players[next++]) : 0u;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* msg = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        Record({0x457E, Id(r.ecx), r.args[0], msg->m_id, msg->m_stream, msg->m_length, static_cast<uint32_t>(msg->m_priority), static_cast<uint32_t>(msg->m_queue),
                msg->m_groupId, msg->m_singular, static_cast<uint32_t>(msg->m_delay), msg->m_forcedSequenceId,
                msg->m_compression});
        const uint8_t* data = static_cast<const uint8_t*>(msg->m_data);
        if (msg->m_id == 0x43) { // (the id, three padding bytes, the push)
            Record({data[0]});
            const uint32_t* push = reinterpret_cast<const uint32_t*>(data + 4);
            Record({push[0], push[1], push[2], push[3]});
        } else {
            for (int i = 0; i < msg->m_length; ++i)
                Record({data[i]});
        }
        return 0u;
    });
    // The handlers' script structure and event.
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) { Record({0x1417}); return r.ecx; });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs&) { Record({0xC1EA}); return 0u; });
    s.At({0x004779D0, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, const char*)>(&Script::CStruct::AddString))},
         8, [](Regs& r) {
             // (a name filling all 32 bytes isn't terminated: the original reads on into its stack - only the 32 are
             // compared)
             char name[33] = {};
             std::memcpy(name, reinterpret_cast<const char*>(r.args[1]), strnlen(reinterpret_cast<const char*>(r.args[1]), 32));
             Record({0xADD2, r.args[0], Hash(name)});
             return 0u;
         });
    s.At({0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
         8, [](Regs& r) {
             Record({0xADDC, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477F70, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::VectorValue)>(&Script::CStruct::AddVector))},
         0x14, [](Regs& r) {
             Record({0xADD5, r.args[0], r.args[1], r.args[2], r.args[3]}); // (w isn't used)
             return 0u;
         });
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x5E1F, Id(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x004758F0, AddressOf(static_cast<void (*)(const char*, Script::CStruct*, Obj::CObject*, bool)>(&Script::RunScript))},
         0, [](Regs& r) {
             Record({0x7558, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
}

// The world: `count` skaters, their components, players, the managers.
void BuildWorld(Random& r)
{
    std::memset(g_skaters, 0, sizeof(g_skaters));
    std::memset(g_states, 0, sizeof(g_states));
    std::memset(g_histories, 0, sizeof(g_histories));
    std::memset(g_controls, 0, sizeof(g_controls));
    std::memset(g_inputs, 0, sizeof(g_inputs));
    std::memset(g_players, 0, sizeof(g_players));
    std::memset(g_connections, 0, sizeof(g_connections));
    std::memset(g_netManager, 0, sizeof(g_netManager));
    for (int i = 0; i < kSkaters; ++i) {
        uint8_t* skater = g_skaters[i];
        At<uint32_t>(skater, 0x14) = 0x20 + i * 3 + (r.OneIn(4) ? 0x40 : 0); // (bit 6 set now and then)
        float spread = r.OneIn(3) ? 15.0f : 60.0f;
        float pos[4] = {r.Range(-spread, spread), r.Range(-spread, spread) * 0.3f, r.Range(-spread, spread),
                        r.OneIn(2) ? 1.0f : r.Range(-2.0f, 2.0f)};
        if (r.OneIn(8)) // on top of skater 0
            std::memcpy(pos, g_skaters[0] + 0x4C, 16);
        std::memcpy(skater + 0x4C, pos, 16);
        float vel[4] = {r.Range(-900.0f, 900.0f), r.Range(-200.0f, 200.0f), r.Range(-900.0f, 900.0f), r.Range(-3.0f, 3.0f)};
        if (i > 0 && r.OneIn(4)) { // parallel to skater 0 (the segments' closest points fail: only the ends count)
            float scale = r.OneIn(2) ? 1.0f : r.Range(0.2f, 2.0f);
            for (int k = 0; k < 3; ++k)
                vel[k] = reinterpret_cast<const float*>(g_skaters[0] + 0xAC)[k] * scale;
        }
        if (r.OneIn(5))
            vel[0] = vel[1] = vel[2] = 0.0f;
        if (r.OneIn(6))
            vel[0] = 9000.0f; // too fast to be hit
        std::memcpy(skater + 0xAC, vel, 16);
        skater[0xBC] = r.OneIn(4);
        // Components: the state (and sometimes the input) on the object's list.
        uint8_t* state = g_states[i];
        At<uint32_t>(state, 4) = 0x43686585;
        state[0x70] = r.OneIn(5);
        state[0x118] = r.OneIn(3);
        At<int>(state, 0x1C) = r.OneIn(4) ? 1 : 0;
        uint8_t* control = g_controls[i];
        At<uint32_t>(control, 4) = 0xD9151D4D;
        At<uint8_t*>(control, 0x2C) = state;
        control[0x25] = r.OneIn(4);
        At<uint32_t>(g_cores[i], 4) = 0x5BD63B29;
        At<uint32_t>(g_walks[i], 4) = 0x726E85AA;
        At<uint32_t>(g_inputs[i], 4) = 0x27D7CD28;
        At<uint8_t*>(state, 0x10) = control;
        At<uint8_t*>(control, 0x10) = g_cores[i];
        At<uint8_t*>(g_cores[i], 0x10) = g_walks[i];
        At<uint8_t*>(g_walks[i], 0x10) = r.OneIn(3) ? nullptr : g_inputs[i];
        At<uint8_t*>(g_inputs[i], 0x10) = nullptr;
        At<uint8_t*>(skater, 0x100) = state;
        At<uint8_t*>(g_histories[i], 0x0C) = skater;
        At<uint8_t*>(skater, 0x27C) = g_histories[i];
        At<uint8_t*>(skater, 0x278) = g_scoreHolders[i];
        At<uint8_t*>(g_scoreHolders[i], 0x18) = g_scores[i];
        At<int>(g_scores[i], 0) = static_cast<int>(r.Below(3000));
    }
    for (int i = 0; i < kPlayers; ++i) {
        uint8_t* player = g_players[i];
        At<uint8_t*>(player, 0x14) = i < kSkaters ? g_skaters[i] : g_skaters[0];
        At<uint8_t*>(player, 0x18) = r.OneIn(6) ? nullptr : g_connections[i];
        At<uint32_t>(g_connections[i], 0x3C) = 0xC0 + i;
        At<int>(player, 0x30) = static_cast<int>(r.Below(3));
        uint32_t flags = r.OneIn(3) ? 1 : 0;
        if (r.OneIn(3))
            flags |= 0x80;
        static const uint32_t teams[] = {0, 0, 0x800, 0x1000, 0x2000, 0x4000};
        flags |= teams[r.Below(6)];
        At<uint32_t>(player, 0x1D0) = flags;
        g_observing[i] = r.OneIn(7);
    }
    for (int& p : g_playerOfId)
        p = -1;
    for (int i = 0; i < kSkaters; ++i)
        if (!r.OneIn(9))
            g_playerOfId[At<uint32_t>(g_skaters[i], 0x14) & 0x7F] = i;
    for (int& slot : g_slots)
        slot = r.OneIn(5) ? -1 : static_cast<int>(r.Below(kSkaters));
    At<uint32_t>(g_netManager, 0) = r.Below(8); // (bit 0 the server, bit 2 a flag)
    At<uint8_t*>(g_netManager, 4) = g_app;
    g_netManager[0x3A] = r.OneIn(4);
    At<uint8_t*>(g_netManager, 0x6C) = g_skaters[r.Below(kSkaters)];
    g_mayCollide = r.Bit();
    g_slapGame = r.OneIn(3);
    g_teamGame = r.OneIn(3);
    g_tagOver = r.OneIn(4);
    static const uint32_t types[] = {0x6EF8FDA0, 0x5D32129C, 0x6C5FF266, 0x12345678};
    g_gameType = types[r.Below(4)];
    for (float& f : g_floats)
        f = r.Range(0.0f, 30.0f);
    g_floats[0] = r.Range(5.0f, 60.0f);
    g_floats[1] = r.Range(5.0f, 60.0f);
    for (int i = 3; i < 7; ++i)
        g_floats[i] = r.Range(2.0f, 45.0f);
    g_floats[7] = r.Range(0.0f, 1.5f);
    GameVariable<float>(0x00684FD0) = 60.0f;
    static const double frames[] = {1.0, 2.0, 1.25, 0.5};
    GameVariable<double>(0x007D1E70) = frames[r.Below(4)];
}

void TraceWorld()
{
    for (int i = 0; i < kSkaters; ++i) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(g_skaters[i]);
        g_trace.insert(g_trace.end(), w + 0x4C / 4, w + 0xC0 / 4); // position, matrix, velocity, +0xBC
        g_trace.push_back(g_inputs[i][0x498]);
        g_trace.push_back(At<uint32_t>(g_scores[i], 0));
    }
    g_trace.insert(g_trace.end(), reinterpret_cast<const uint32_t*>(g_message),
                   reinterpret_cast<const uint32_t*>(g_message + sizeof(g_message)));
}

std::vector<Saved> SavedGlobals()
{
    return {{0x007CCE14, 4, {}}, {0x007CE470, 4, {}}, {0x007CE478, 4, {}}, {0x00684FD0, 4, {}}, {0x007D1E70, 8, {}}};
}
void PointGlobalsAtWorld()
{
    GameVariable<void*>(0x007CCE14) = g_netManager;
    GameVariable<void*>(0x007CE470) = g_gameMode;
    GameVariable<void*>(0x007CE478) = g_skaterManager;
}

// --- the small methods ------------------------------------------------------------------------------------------

alignas(16) uint8_t g_block[2][0x1108];
int g_blockNext;
uint32_t g_destroyed[4];

void TestStateHistoryMethods()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInWorld(*s);
        s->At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
            Record({0xCA11, r.args[0]});
            return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
        });
        s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
            Record({0xF5EE, r.args[0] == reinterpret_cast<uint32_t>(g_block[0])});
            return 0u;
        });
        s->At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
            Record({0xD7, At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
            return 0u;
        });
        PointGlobalsAtWorld();
        GameVariable<void*>(0x007D1800) = g_block[1]; // (the manager Free is called on: any non-null)
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{4242};
            // The message time stamp halves.
            for (int n = 0; n < 64; ++n) {
                alignas(4) uint8_t entry[0x54];
                for (uint8_t& b : entry)
                    b = static_cast<uint8_t>(r.Next());
                uint32_t time = r.Next() * 977u + r.Below(7);
                auto* m = reinterpret_cast<Obj::StateHistoryMessage*>(entry);
                if (original) {
                    Record({Original<uint32_t(__thiscall*)(void*)>(0x00519800)(entry)});
                    Original<void(__thiscall*)(void*, uint32_t)>(0x00519810)(entry, time);
                } else {
                    Record({m->GetTime()});
                    m->SetTime(time);
                }
                g_trace.insert(g_trace.end(), reinterpret_cast<uint32_t*>(entry), reinterpret_cast<uint32_t*>(entry + 0x54));
            }
            // The player search's destructors.
            for (unsigned flags : {0u, 1u, 3u}) {
                uint32_t search[3] = {0x11111111, 0x22222222, 0x33333333};
                std::memcpy(g_block[0], search, sizeof(search));
                void* result;
                if (original) {
                    Original<void(__thiscall*)(void*)>(0x00519830)(search);
                    result = Original<void*(__thiscall*)(void*, unsigned)>(0x0051A120)(g_block[0], flags);
                } else {
                    reinterpret_cast<Obj::PlayerSearchDestructors*>(search)->DestroyBody();
                    result = reinterpret_cast<Obj::PlayerSearchDestructors*>(g_block[0])->Destroy(flags);
                }
                Record({search[0], search[1], search[2], result == g_block[0], At<uint32_t>(g_block[0], 0),
                        At<uint32_t>(g_block[0], 4)});
            }
            // Create / Construct (over dirty memory) / Destroy.
            for (int next : {0, -1}) {
                std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                g_blockNext = next;
                void* made = original ? Original<void*(__cdecl*)()>(0x0051AD30)() : CSkaterStateHistoryComponent::Create();
                Record({made == g_block[0]});
                g_trace.insert(g_trace.end(), reinterpret_cast<uint32_t*>(g_block[0]),
                               reinterpret_cast<uint32_t*>(g_block[0] + sizeof(g_block[0])));
            }
            for (unsigned flags : {0u, 1u}) {
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00519840)(g_block[0], flags)
                                        : reinterpret_cast<CSkaterStateHistoryComponent*>(g_block[0])->Destroy(flags);
                Record({result == g_block[0], At<uint32_t>(g_block[0], 0)});
            }
            // The methods reading the object, over random worlds.
            for (int n = 0; n < 300; ++n) {
                BuildWorld(r);
                int i = static_cast<int>(r.Below(kSkaters));
                auto* history = reinterpret_cast<CSkaterStateHistoryComponent*>(g_histories[i]);
                Mth::Vector v[2];
                std::memset(v, 0x5A, sizeof(v));
                bool a = r.Bit(), b = r.Bit();
                float reach, radius;
                bool isNear;
                if (original) {
                    Original<void(__thiscall*)(void*, Mth::Vector*)>(0x00519FA0)(history, &v[0]);
                    Original<void(__thiscall*)(void*, Mth::Vector*)>(0x00519FF0)(history, &v[1]);
                    reach = static_cast<float>(Original<double(__thiscall*)(void*, bool)>(0x0051A020)(history, a));
                    radius = static_cast<float>(Original<double(__thiscall*)(void*, bool, bool)>(0x0051A080)(history, a, b));
                    isNear = Original<bool(__thiscall*)(void*)>(0x0051A150)(history);
                } else {
                    history->GetFrameMovement(&v[0]);
                    history->GetPosition(&v[1]);
                    reach = static_cast<float>(history->GetCollisionReach(a));
                    radius = static_cast<float>(history->GetCollisionRadius(a, b));
                    isNear = history->IsNearLocalSkater();
                }
                g_trace.insert(g_trace.end(), reinterpret_cast<uint32_t*>(v), reinterpret_cast<uint32_t*>(v + 2));
                Record({Bits(reach), Bits(radius), isNear});
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    GameVariable<void*>(0x007D1800) = nullptr;
    RestoreAll(saved);
    Compare(traces[0], traces[1], "skater state history: time stamps, search destructors, create / destroy, reach, radius");
}

// The unrounded results, compared bit for bit (the original leaves them in ST0).
void TestStateHistoryUnrounded()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInWorld(*s);
        PointGlobalsAtWorld();
        for (int run = 0; run < 2; ++run) {
            g_trace.clear();
            Random r{99};
            for (int n = 0; n < 300; ++n) {
                BuildWorld(r);
                auto* history = reinterpret_cast<CSkaterStateHistoryComponent*>(g_histories[r.Below(kSkaters)]);
                bool a = r.Bit(), b = r.Bit();
                double reach, radius;
                if (run == 0) {
                    reach = Original<double(__thiscall*)(void*, bool)>(0x0051A020)(history, a);
                    radius = Original<double(__thiscall*)(void*, bool, bool)>(0x0051A080)(history, a, b);
                } else {
                    reach = history->GetCollisionReach(a);
                    radius = history->GetCollisionRadius(a, b);
                }
                uint32_t w[4];
                std::memcpy(w, &reach, 8);
                std::memcpy(w + 2, &radius, 8);
                Record({w[0], w[1], w[2], w[3]});
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "skater state history: reach and radius unrounded");
}

// --- the collision check ---------------------------------------------------------------------------------------

void TestStateHistoryCollisions()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int hits = 0;
    {
        auto* s = new StandIns;
        StandInWorld(*s);
        // The segments' closest points: ours in both runs (it works in floats - tested on its own with a tolerance).
        s->Bytes(0x00402F00, Branch(0xE9, 0x00402F00, reinterpret_cast<void*>(&Mth::ClosestPointsBetweenLines)));
        PointGlobalsAtWorld();
        for (int run = 0; run < 2; ++run) {
            g_trace.clear();
            Random r{20261022};
            for (int n = 0; n < 4000; ++n) {
                BuildWorld(r);
                int me = static_cast<int>(r.Below(kSkaters));
                for (int i = 0; i < kSkaters; ++i) // (another skater without a player crashes the original too)
                    if (i != me)
                        g_playerOfId[At<uint32_t>(g_skaters[i], 0x14) & 0x7F] = i;
                if (r.OneIn(4)) {
                    // Along one axis (the segments exactly parallel: only their ends count), whole-number reaches
                    // and radii, the other skater ahead or behind.
                    float* myPos = reinterpret_cast<float*>(g_skaters[me] + 0x4C);
                    float* myVel = reinterpret_cast<float*>(g_skaters[me] + 0xAC);
                    myVel[0] = r.Range(60.0f, 3000.0f);
                    myVel[1] = myVel[2] = 0.0f;
                    for (int k : {0, 1, 3, 4, 5, 6})
                        g_floats[k] = static_cast<float>(5 + r.Below(36));
                    g_floats[2] = static_cast<float>(r.Below(3));
                    for (int i = 0; i < kSkaters; ++i) {
                        if (i == me || r.OneIn(3))
                            continue;
                        float* pos = reinterpret_cast<float*>(g_skaters[i] + 0x4C);
                        float* vel = reinterpret_cast<float*>(g_skaters[i] + 0xAC);
                        pos[0] = myPos[0] + static_cast<float>(static_cast<int>(r.Below(161)) - 80);
                        pos[1] = myPos[1];
                        pos[2] = myPos[2];
                        vel[0] = r.Range(60.0f, 3000.0f) * (r.OneIn(3) ? -1.0f : 1.0f);
                        vel[1] = vel[2] = 0.0f;
                    }
                }
                int first = r.OneIn(6) ? static_cast<int>(r.Below(10)) : 0;
                auto* history = reinterpret_cast<CSkaterStateHistoryComponent*>(g_histories[me]);
                size_t before = g_trace.size();
                if (run == 0)
                    Original<void(__thiscall*)(void*, int)>(0x0051A1C0)(history, first);
                else
                    history->CheckForCollisions(first);
                if (run == 0)
                    for (size_t k = before; k < g_trace.size(); ++k)
                        hits += g_trace[k] == 0x457E;
                TraceWorld();
                Record({0xE0Du});
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Log("  skater collisions: %d messages sent by the original", hits);
    Check(hits > 200, "skater state history: the collision cases reach the messages");
    Compare(traces[0], traces[1], "skater state history: network collisions (teams, tag games, slap)");
}

// --- the message handlers --------------------------------------------------------------------------------------

void TestStateHistoryHandlers()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInWorld(*s);
        PointGlobalsAtWorld();
        for (int run = 0; run < 2; ++run) {
            g_trace.clear();
            Random r{777};
            for (int n = 0; n < 3000; ++n) {
                BuildWorld(r);
                int me = static_cast<int>(r.Below(kSkaters));
                for (uint8_t& b : g_message)
                    b = static_cast<uint8_t>(r.Next());
                int other = static_cast<int>(r.Below(kSkaters));
                g_message[0] = static_cast<uint8_t>(At<uint32_t>(g_skaters[other], 0x14) | (r.Bit() ? 0x80 : 0));
                if (r.OneIn(8))
                    g_message[0] = static_cast<uint8_t>(r.Next()); // (an id nobody has, often)
                g_message[1] = r.OneIn(3) ? 0 : static_cast<uint8_t>(r.Next());
                At<int>(g_message, 4) = static_cast<int>(r.Below(4000)) - 500;
                if (r.OneIn(5)) // exactly the score
                    At<int>(g_message, 4) = At<int>(g_scores[me], 0);
                if (r.OneIn(6)) // the same position: the default push direction
                    std::memcpy(g_skaters[me] + 0x4C, g_skaters[other] + 0x4C, 16);
                static const uint8_t ids[] = {0x84, 0x85, 0x43, 0x44, 0x42, 0x86};
                auto* context = reinterpret_cast<Net::MsgHandlerContext*>(g_context);
                std::memset(g_context, 0, sizeof(g_context));
                context->m_Msg = reinterpret_cast<char*>(g_message);
                context->m_MsgId = ids[r.Below(6)];
                context->m_data = g_skaters[me];
                bool hits = context->m_MsgId == 0x84 || context->m_MsgId == 0x85 || r.Bit();
                int result;
                if (run == 0)
                    result = Original<int(__cdecl*)(Net::MsgHandlerContext*)>(hits ? 0x00519870 : 0x00519C10)(context);
                else
                    result = hits ? CSkaterStateHistoryComponent::HandleHitMessages(context)
                                  : CSkaterStateHistoryComponent::HandleCollisionMessages(context);
                Record({0x4E5u, static_cast<uint32_t>(result)});
                TraceWorld();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "skater state history: hit / collision message handlers");
}

} // namespace

SELFTEST(Early, TestStateHistoryMethods, RemoveStandIns);
SELFTEST(Early, TestStateHistoryUnrounded, RemoveStandIns);
SELFTEST(Early, TestStateHistoryCollisions, RemoveStandIns);
SELFTEST(Early, TestStateHistoryHandlers, RemoveStandIns);
