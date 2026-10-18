// Self-tests for the skate module's network handlers (src/game/skate/skate_net.cpp): the original and ours handle the
// same messages on identical stand-in skaters, players, goals, histories and apps in an arena; what they call outside
// (the network manager, the game mode, the goals, the scores, the objects, the scripts and structures, memory, the
// handler lists) is stood in for by recording code written over the game's function and ours alike, and the calls and
// the whole arena are compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_net.h"
#include "game/util/bitstream.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Mdl::SkateNet;
using Script::CArray;
using Script::CStruct;

using MsgCode = int(__cdecl*)(Net::MsgHandlerContext*);

constexpr uint32_t kArenaSize = 0x20000;
ArenaPair<kArenaSize> g_sn_storage;
uint32_t g_sn_base;
uint32_t At(uint32_t offset) { return g_sn_base + offset; }
template <typename T>
T* Ptr(uint32_t offset)
{
    return reinterpret_cast<T*>(g_sn_base + offset);
}
template <typename T>
T& Word(uint32_t offset)
{
    return *reinterpret_cast<T*>(g_sn_base + offset);
}
bool InArena(uint32_t word) { return word - g_sn_base < kArenaSize; }
uint32_t Rel(uint32_t word) { return InArena(word) ? 0xA0000000u + (word - g_sn_base) : word; }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// Arena layout.
constexpr uint32_t kSkate = 0x0000;      // the skate module: +0x18 / +0x1C its tasks, +0x374 the next level
constexpr uint32_t kObjects = 0x0400;    // objects (skaters), 0x300 apart: +0x14 id, +0x20 score object, +0x4C
                                         // position, +0x6C matrix, +0xBC local, +0x27C state history, +0x2B8 client
constexpr uint32_t kHistories = 0x1C00;  // state histories, 0xB00 apart
constexpr uint32_t kScores = 0x7400;     // score objects, 0x10 apart: +0 the total
constexpr uint32_t kPlayers = 0x7500;    // players, 0x60 apart: +0x14 skater, +0x30 team, +0x34 objects, +0x38 name,
                                         // +0x50 local, +0x51 / +0x52 the 0x00543320 / 0x00543380 answers, +0x54 next
constexpr uint32_t kGoals = 0x7800;      // goals, 0x20 apart: vtable +0, active +4
constexpr uint32_t kFakes = 0x7900;      // stand-in structures and arrays, 0x20 apart
constexpr uint32_t kText = 0x7D00;       // strings
constexpr uint32_t kGameMode = 0x7E00;
constexpr uint32_t kNet = 0x7F00;
constexpr uint32_t kMemManager = 0x8000; // (0x5E8)
constexpr uint32_t kConn = 0x8800;
constexpr uint32_t kMisc = 0x8A00;
constexpr uint32_t kMsg = 0x9000;        // the message (0x800)
constexpr uint32_t kApp = 0xA000;        // a server / client app (0x2000)
constexpr uint32_t kHeap = 0xC000;

uint32_t g_sn_heapNext;
std::vector<Saved> g_sn_saved;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_sn_answers; // (structure, name) -> checksum
std::map<uint32_t, uint32_t> g_sn_flags;                       // the game mode's 0x00553220 answers by name
uint32_t g_sn_present;     // objects that exist (bits)
bool g_sn_teams, g_sn_autoServing, g_sn_serverMade, g_sn_netGame, g_sn_observing, g_sn_cheatNoted, g_sn_leaderShown,
    g_sn_otherModeFlag, g_sn_isServer;
int g_sn_teamCount, g_sn_teamScores[4], g_sn_playerScores[8], g_sn_checksumOfSkate, g_sn_lastTeam, g_sn_scavengerMode;
uint32_t g_sn_firstPlayer, g_sn_localPlayer, g_sn_lastPlayer, g_sn_mode, g_sn_heapOk;
int g_sn_proSetFlag;
void* g_sn_goalVtable[8];

bool __fastcall StandInGoalActive(uint8_t* goal)
{
    Rec(0x60A1, {reinterpret_cast<uint32_t>(goal)});
    return goal[4] != 0;
}
void __fastcall StandInGoalUpdate(uint8_t* goal) { Rec(0x60A2, {reinterpret_cast<uint32_t>(goal)}); }

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_sn_saved);
    g_sn_saved.clear();
}

uint32_t Element(uint32_t array, uint32_t index)
{
    auto* a = reinterpret_cast<CArray*>(array);
    return a->m_size == 1 ? a->m_single : a->m_elements[index];
}

void Install(StandIns& s)
{
    for (void*& slot : g_sn_goalVtable)
        slot = nullptr;
    g_sn_goalVtable[3] = reinterpret_cast<void*>(&StandInGoalActive);
    g_sn_goalVtable[5] = reinterpret_cast<void*>(&StandInGoalUpdate);
    auto simple = [&](uint32_t address, uint16_t pop, uint32_t tag, std::function<uint32_t(Regs&)> answer = nullptr) {
        s.At({address}, pop, [tag, pop, answer](Regs& r) {
            std::vector<uint32_t> values = {r.ecx};
            for (int i = 0; i < pop / 4; ++i)
                values.push_back(r.args[i]);
            g_trace.push_back(tag);
            for (uint32_t v : values)
                g_trace.push_back(Rel(v));
            return answer ? answer(r) : 0u;
        });
    };
    // The network manager.
    for (uint32_t a : {0x005331F0u, 0x00533060u, 0x00536B90u, 0x00533230u, 0x00532F90u, 0x00535D90u, 0x005331D0u,
                       0x005430D0u, 0x00533180u, 0x00543420u})
        simple(a, a == 0x00543420 ? 0 : 4, a & 0xFFFF);
    for (uint32_t a : {0x005462F0u, 0x0053C6E0u, 0x0053E910u, 0x005399F0u, 0x005332F0u, 0x0053A040u, 0x00534D70u})
        simple(a, 0, a & 0xFFFF);
    simple(0x00533F90, 0, 0x3F90, [](Regs&) { return g_sn_isServer ? 1u : 0u; });
    simple(0x0044E4F0, 0, 0xE4F0, [](Regs&) { return g_sn_netGame ? 1u : 0u; });
    simple(0x0044E4E0, 0, 0xE4E0, [](Regs&) { return g_sn_autoServing ? 1u : 0u; });
    simple(0x00532AC0, 0, 0x2AC0, [](Regs&) { return 0u; });
    simple(0x005332E0, 0, 0x32E0, [](Regs&) { return g_sn_cheatNoted ? 1u : 0u; });
    simple(0x005332B0, 0, 0x32B0, [](Regs&) { return g_sn_leaderShown ? 1u : 0u; });
    simple(0x005331A0, 0, 0x31A0, [](Regs&) { return static_cast<uint32_t>(g_sn_lastTeam); });
    simple(0x00533190, 0, 0x3190, [](Regs&) { return g_sn_lastPlayer ? At(g_sn_lastPlayer) : 0u; });
    simple(0x005435F0, 4, 0x35F0, [](Regs& r) { return static_cast<uint32_t>(g_sn_teamScores[r.args[0] & 3]); });
    simple(0x005436E0, 4, 0x36E0, [](Regs& r) { return static_cast<uint32_t>(g_sn_playerScores[r.args[0] & 7]); });
    simple(0x0053C380, 8, 0xC380, [](Regs&) { return g_sn_serverMade ? At(kApp) : 0u; });
    simple(0x00537320, 0, 0x7320, [](Regs&) { return g_sn_localPlayer ? At(g_sn_localPlayer) : 0u; });
    s.At({0x0053AEA0}, 4, [](Regs& r) {
        uint32_t id = r.args[0] & 0xFFFF; // (the original pushes a 16-bit value)
        Rec(0xAEA0, {r.ecx, id});
        return id < 8 && id != 3 ? At(kPlayers + 0x60 * id) : 0u;
    });
    simple(0x00543300, 0, 0x3300, [](Regs& r) { return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx + 0x50)); });
    simple(0x00543320, 0, 0x3320, [](Regs& r) { return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx + 0x51)); });
    simple(0x00543380, 0, 0x3380, [](Regs& r) { return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx + 0x52)); });
    auto nextPlayer = [](Regs& r) {
        auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
        if (!search[1])
            return 0u;
        uint32_t player = At(search[1]);
        search[1] = *reinterpret_cast<uint32_t*>(player + 0x54);
        return player;
    };
    s.At({0x005372C0}, 8, [nextPlayer](Regs& r) {
        Rec(0x72C0, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), *reinterpret_cast<uint32_t*>(r.args[0] + 4),
                     r.args[1]});
        reinterpret_cast<uint32_t*>(r.args[0])[1] = g_sn_firstPlayer;
        return nextPlayer(r);
    });
    s.At({0x00537010}, 8, [nextPlayer](Regs& r) {
        Rec(0x7010, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), r.args[1]});
        return nextPlayer(r);
    });
    // The game mode.
    for (uint32_t a : {0x00553400u, 0x005531B0u, 0x00553AD0u, 0x005535A0u, 0x005530A0u, 0x00553080u, 0x00553250u,
                       0x00553BE0u})
        simple(a, 4, a & 0xFFFF);
    simple(0x005535F0, 0, 0x35F1, [](Regs&) { return g_sn_teams ? 1u : 0u; });
    simple(0x00553130, 0, 0x3130, [](Regs&) { return static_cast<uint32_t>(g_sn_teamCount); });
    simple(0x005532A0, 0, 0x32A0, [](Regs&) { return g_sn_mode; });
    simple(0x005530F0, 0, 0x30F0, [](Regs&) { return g_sn_otherModeFlag ? 1u : 0u; });
    simple(0x00553220, 4, 0x3220, [](Regs& r) { return g_sn_flags.count(r.args[0]) ? g_sn_flags[r.args[0]] : 0u; });
    // The skate module, the skaters, their scores and the goals.
    for (uint32_t a : {0x00566A60u})
        simple(a, 0, a & 0xFFFF);
    simple(0x00564D50, 4, 0x4D50);
    simple(0x00564C60, 8, 0x4C60);
    simple(0x005695A0, 0, 0x95A0, [](Regs&) { return static_cast<uint32_t>(g_sn_checksumOfSkate); });
    simple(0x005665C0, 4, 0x65C0, [](Regs& r) { return r.args[0] < 8 && r.args[0] != 2 ? At(kObjects + 0x300 * r.args[0]) : 0u; });
    simple(0x0057FEC0, 0, 0xFEC0, [](Regs& r) { return *reinterpret_cast<uint32_t*>(r.ecx + 0x2B8); });
    simple(0x00580480, 0, 0x0480, [](Regs& r) { return *reinterpret_cast<uint32_t*>(r.ecx + 0x20); });
    simple(0x00561B40, 0, 0x1B40, [](Regs& r) { return *reinterpret_cast<uint32_t*>(r.ecx); });
    simple(0x00561A80, 4, 0x1A80);
    simple(0x00581580, 0, 0x1580, [](Regs& r) { return At(kText + 0x40 + 0x10 * ((r.ecx - At(kObjects)) / 0x300)); });
    s.At({0x005591B0}, 0, [](Regs&) {
        Rec(0x91B0, {});
        return At(kMisc);
    });
    s.At({0x00557F90}, 8, [](Regs& r) {
        Rec(0x7F90, {r.ecx, r.args[0], r.args[1]});
        uint32_t index = r.args[0] == 0x1F3430CE ? 0 : r.args[0] == 0x8A6B04AD ? 1 : 2;
        return At(kGoals + 0x20 * index);
    });
    simple(0x00588480, 4, 0x8480, [](Regs& r) { return 0xC0100000u + r.args[0]; });
    simple(0x00588150, 4, 0x8150);
    simple(0x005890B0, 0, 0x90B0);
    s.At({0x005A0D90}, 0, [](Regs& r) {
        Rec(0x0D90, {r.args[0], r.args[1]});
        return g_sn_observing ? 1u : 0u;
    });
    // Objects.
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return r.args[0] < 8 && ((g_sn_present >> r.args[0]) & 1) ? At(kObjects + 0x300 * r.args[0]) : 0u;
    });
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Rec(0x09F0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    // The app, the tasks, the main loop.
    simple(0x00455A40, 4, 0x5A40);
    simple(0x00455960, 4, 0x5960);
    simple(0x00455920, 4, 0x5920);
    s.At({0x0045A5C0, AddressOf(&Net::Dispatcher::AddHandler)}, 20, [](Regs& r) {
        // (ours registers our handlers: the game's addresses of those)
        static const std::pair<MsgCode, uint32_t> ours[] = {
            {SkateNet::HandleServerRemovedYou, 0x0056C920}, {SkateNet::HandleScavengerHunt, 0x0056CA70},
            {SkateNet::HandleScavengerKillObject, 0x0056CDF0}, {SkateNet::HandleScavengerDropObjects, 0x0056CEC0},
            {SkateNet::HandleGraffitiColors, 0x0056D030}, {SkateNet::HandleScores, 0x0056D250},
            {SkateNet::HandleStartInfo, 0x0056D810}, {SkateNet::HandleRunScript, 0x0056E380},
            {SkateNet::HandleObjectUpdates, 0x0056E470}};
        uint32_t code = r.args[1];
        for (const auto& [function, address] : ours) {
            if (Resolve(reinterpret_cast<const void*>(code)) == AddressOf(function))
                code = address;
        }
        Rec(0xA5C0, {r.ecx, r.args[0] & 0xFF, code, r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    simple(0x00406C90, 4, 0x6C90);
    s.At({0x00406890}, 4, [](Regs& r) {
        Rec(0x6890, {r.ecx, r.args[0]});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x11, 0x18);
        return r.ecx;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        if (!g_sn_heapOk)
            return 0u;
        --g_sn_heapOk;
        uint32_t block = At(g_sn_heapNext);
        g_sn_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
    // Scripts and structures.
    s.At({0x004753C0, AddressOf(&Script::DeleteSpawnedScripts)}, 0, [](Regs&) {
        Rec(0x53C0, {});
        return 0u;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x004758F0, AddressOf(static_cast<void (*)(const char*, CStruct*, Obj::CObject*, bool)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x58F0, {Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2]});
        return 0u;
    });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             // (with no local player the original passes 0x38 itself: recorded as it is)
             uint32_t text = r.args[1];
             Rec(0x79D0, {r.ecx, r.args[0], text < 0x10000 ? text : Hash(reinterpret_cast<const char*>(text))});
             return 0u;
         });
    s.At({0x00478390, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t, const char*)>(&CStruct::AddString))},
         12, [](Regs& r) {
             Rec(0x8390, {r.ecx, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Rec(0x7B80, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477F70, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::VectorValue)>(&CStruct::AddVector))}, 20,
         [](Regs& r) {
             Rec(0x7F70, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
             return 0u;
         });
    s.At({0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, CArray*)>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Rec(0x8B00, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478FF0, AddressOf(static_cast<const char* (*)(const char*)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FF0, {Hash(reinterpret_cast<const char*>(r.args[0]))});
        return At(kText + 0x100);
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x8F60, {r.args[0]});
        return static_cast<uint32_t>(g_sn_proSetFlag);
    });
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        switch (r.args[0]) {
        case 0x50E63314: return At(kFakes + 0x00); // targetScoreArray
        case 0x1A2C22F9: return At(kFakes + 0x20); // highestScoreArray
        case 0x2F94C542: return At(kFakes + 0x40); // net_game_type_info
        default: return At(kFakes + 0x60);         // mp_game_type_info
        }
    });
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        Rec(0x8A60, {r.args[0], r.args[1]});
        auto* dest = reinterpret_cast<CArray*>(r.args[0]);
        dest->m_size = 1;
        dest->m_single = At(kFakes + 0x200 + (r.args[1] - At(kFakes)));
        return 0u;
    });
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return Element(r.ecx, r.args[0]);
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    auto answer = [](uint32_t structure, uint32_t name, uint32_t out) {
        auto it = g_sn_answers.find({structure - g_sn_base, name});
        if (it == g_sn_answers.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(out) = it->second;
        return 1u;
    };
    s.At({0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [answer](Regs& r) {
             Rec(0x6EC0, {r.ecx, r.args[0], r.args[2] & 0xFF});
             return answer(r.ecx, r.args[0], r.args[1]);
         });
    s.At({0x00476EE0, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [answer](Regs& r) {
             uint32_t name = Hash(reinterpret_cast<const char*>(r.args[0]));
             Rec(0x6EE0, {r.ecx, name, r.args[2] & 0xFF});
             return answer(r.ecx, name, r.args[1]);
         });
    s.At({0x0047A0C0}, 0, [](Regs& r) {
        Rec(0xA0C0, {r.args[0], r.args[1]});
        return r.args[1];
    });
    // Debug output, the C runtime, memory.
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0x1C30, {Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    // The matrix functions the original calls: ours (the CRT's sin / cos then, as in ours).
    s.Bytes(0x004048D0, Branch(0xE9, 0x004048D0, reinterpret_cast<void*>(AddressOf(&Mth::Matrix::SetFromEulerAngles))));
    s.Bytes(0x00404AB0, Branch(0xE9, 0x00404AB0, reinterpret_cast<void*>(AddressOf(&Mth::Matrix::GetEulerAngles))));
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1]});
        uint32_t block = At(g_sn_heapNext);
        g_sn_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xB110, {r.ecx});
        return 0u;
    });
}

void SetUpArena(int run)
{
    uint8_t* arena = g_sn_storage.Get()[run];
    g_sn_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kHeap, 0xCD, kArenaSize - kHeap);
    g_sn_heapNext = kHeap;
    g_trace.clear();
    g_sn_answers.clear();
    g_sn_flags.clear();
    GameVariable<uint32_t>(0x007CE478) = At(kSkate);
    GameVariable<uint32_t>(0x007CE470) = At(kGameMode);
    GameVariable<uint32_t>(0x007CCE14) = At(kNet);
    GameVariable<uint32_t>(0x007D1800) = At(kMemManager);
    GameVariable<uint32_t>(0x006F0FB8) = At(kMisc + 0x100);
    GameVariable<uint32_t>(0x00696D68) = At(kMisc + 0x200);
    GameVariable<uint32_t>(0x007CE500) = At(kMisc + 0x300);
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<bool>(0x006FA7D4) = false;
    GameVariable<int>(0x00684FB4) = 60;
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
    Word<uint32_t>(kMemManager + 0x110) = 0x70D0110;  // the top-down heap
    Word<uint32_t>(kMemManager + 0x124) = 0x5C410124; // the script heap
    // Objects, scores, histories, players, goals, names.
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t object = kObjects + 0x300 * i;
        Word<uint32_t>(object + 0x14) = i;
        Word<uint32_t>(object + 0x20) = At(kScores + 0x10 * i);
        Word<int>(kScores + 0x10 * i) = static_cast<int>(i * 100);
        Word<uint32_t>(object + 0x27C) = At(kHistories + 0xB00 * i);
        Word<float>(object + 0x4C) = 10.5f + i;
        Word<float>(object + 0x50) = -3.25f * i;
        Word<float>(object + 0x54) = 100.0f - i;
        float c = std::cos(0.3f * i), s = std::sin(0.3f * i);
        float m[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
        std::memcpy(Ptr<float>(object + 0x6C), m, sizeof(m));
        Word<uint8_t>(object + 0xBC) = i % 3 == 0;
        Word<int>(object + 0x2B8) = static_cast<int>(i % 2);
        uint32_t player = kPlayers + 0x60 * i;
        Word<uint32_t>(player + 0x14) = At(object);
        Word<int>(player + 0x30) = static_cast<int>(i % 2);
        std::memcpy(Ptr<char>(player + 0x38), "Player", 7);
        Word<char>(player + 0x3E) = static_cast<char>('0' + i);
        Word<uint32_t>(player + 0x54) = i < 3 ? player + 0x60 : 0u;
        std::memcpy(Ptr<char>(kText + 0x40 + 0x10 * i), "Skater", 7);
        Word<char>(kText + 0x46 + 0x10 * i) = static_cast<char>('A' + i);
    }
    std::memcpy(Ptr<char>(kText + 0x100), "Team Text", 10);
    for (uint32_t g = 0; g < 3; ++g)
        Word<void**>(kGoals + 0x20 * g) = g_sn_goalVtable;
    // Fake arrays: target / highest score (one structure each), the game type infos (3 structures).
    for (uint32_t a = 0; a < 4; ++a) {
        auto* array = Ptr<CArray>(kFakes + 0x20 * a);
        array->m_size = a < 2 ? 1 : 3;
        if (a < 2)
            array->m_single = At(kFakes + 0x100 + 0x10 * a);
        else
            array->m_elements = Ptr<uint32_t>(kFakes + 0x140 + 0x10 * a);
        for (uint32_t e = 0; e < 3; ++e)
            Word<uint32_t>(kFakes + 0x140 + 0x10 * a + 4 * e) = At(kFakes + 0x180 + 0x30 * a + 0x10 * e);
    }
}

void SaveGlobals()
{
    g_sn_saved = {{0x007CE478, 4, {}}, {0x007CE470, 4, {}}, {0x007CCE14, 4, {}}, {0x007D1800, 4, {}},
                  {0x006F0FB8, 4, {}}, {0x00696D68, 4, {}}, {0x007CE500, 4, {}}, {0x007CC979, 1, {}},
                  {0x006FA7D4, 1, {}}, {0x00684FB4, 4, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}};
    SaveAll(g_sn_saved);
}

void ResetState()
{
    g_sn_present = 0xFF;
    g_sn_teams = g_sn_autoServing = g_sn_netGame = g_sn_observing = g_sn_cheatNoted = g_sn_leaderShown = false;
    g_sn_otherModeFlag = g_sn_isServer = false;
    g_sn_serverMade = true;
    g_sn_teamCount = 2;
    for (int& v : g_sn_teamScores)
        v = 0;
    for (int& v : g_sn_playerScores)
        v = 0;
    g_sn_checksumOfSkate = 0;
    g_sn_lastTeam = -1;
    g_sn_firstPlayer = kPlayers;
    g_sn_localPlayer = kPlayers;
    g_sn_lastPlayer = 0;
    g_sn_mode = 0;
    g_sn_heapOk = 100;
    g_sn_proSetFlag = 40;
}

int Handle(bool original, uint32_t address, MsgCode ours, uint8_t id, uint32_t packetFlags = 0)
{
    Net::MsgHandlerContext context;
    std::memset(&context, 0, sizeof(context));
    context.m_Msg = Ptr<char>(kMsg);
    context.m_MsgId = id;
    context.m_App = Ptr<Net::App>(kApp);
    context.m_Conn = Ptr<Net::Conn>(kConn);
    context.m_PacketFlags = packetFlags;
    context.m_data = Ptr<void>(kSkate);
    int result = original ? Original<MsgCode>(address)(&context) : ours(&context);
    Rec(0xEE00 | id, {static_cast<uint32_t>(result)});
    return result;
}
void Message(std::initializer_list<uint8_t> bytes, uint32_t at = 0)
{
    uint32_t i = at;
    for (uint8_t b : bytes)
        Word<uint8_t>(kMsg + i++) = b;
}

// The tasks, StartServer (auto-serving or not, local or not, no server) and AddClientHandlers (clients 0 and 1).
void TestSkateNetRegistration()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        ResetState();
        auto* skate = Ptr<SkateNet>(kSkate);
        for (uint32_t heap : {2u, 1u, 0u}) {
            g_sn_heapOk = heap;
            if (original)
                Original<void(__thiscall*)(SkateNet*)>(0x0056E2B0)(skate);
            else
                skate->CreateNetworkTasks();
            Rec(0x2B0, {Word<uint32_t>(kSkate + 0x18), Word<uint32_t>(kSkate + 0x1C)});
        }
        g_sn_heapOk = 100;
        auto* app = Ptr<Net::App>(kApp);
        for (int variant = 0; variant < 4; ++variant) {
            g_sn_autoServing = variant == 1;
            g_sn_serverMade = variant != 3;
            app->m_flags = variant == 2 ? Net::App::kLocal : 0u;
            if (original)
                Original<void(__thiscall*)(SkateNet*)>(0x0056DEE0)(skate);
            else
                skate->StartServer();
        }
        for (int index : {0, 1}) {
            if (original)
                Original<void(__thiscall*)(SkateNet*, Net::App*, int)>(0x0056EB50)(skate, app, index);
            else
                skate->AddClientHandlers(app, index);
        }
        traces[run] = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(g_sn_base);
        for (uint32_t i = 0; i < kArenaSize / 4; ++i)
            traces[run].push_back(Rel(words[i]));
    }
    Cleanup();
    Compare(traces[0], traces[1], "skate net registration");
}

// The handlers: removed / moved on (to the main menu or not); the scavenger hunt's messages (goals active and not,
// objects left by player and team, local or not, unknown players, counts other than 1 / 5); kill / drop objects;
// graffiti colours (and not a graffiti game); the scores (eliminations, local skaters, cheating, team and player
// leaders, ties, the end); the start info (every victory condition branch, goal scripts); scripts run.
void TestSkateNetHandlers()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        ResetState();
        // Removed / moved on.
        for (uint32_t level : {0xE92ECAFEu, 0x12345678u}) {
            Word<uint32_t>(kSkate + 0x374) = level;
            for (uint8_t id : {uint8_t('Q'), uint8_t('V'), uint8_t('X')})
                Handle(original, 0x0056C920, SkateNet::HandleServerRemovedYou, id);
        }
        // The scavenger hunt.
        for (int active = 0; active < 4; ++active) {
            Word<uint8_t>(kGoals + 4) = active & 1;
            Word<uint8_t>(kGoals + 0x24) = (active >> 1) & 1;
            Word<uint8_t>(kGoals + 0x44) = active == 3;
            for (uint8_t id = 0x90; id <= 0x94; ++id) {
                if (id != 0x90)
                    Handle(original, 0x0056CA70, SkateNet::HandleScavengerHunt, id);
            }
        }
        for (bool teams : {false, true}) {
            g_sn_teams = teams;
            for (int localVariant = 0; localVariant < 4; ++localVariant) {
                g_sn_localPlayer = localVariant == 3 ? 0u : kPlayers + 0x60 * localVariant;
                Word<uint8_t>(kPlayers + 0x51) = localVariant == 1;
                Word<uint8_t>(kPlayers + 0x52) = localVariant != 2;
                Word<uint8_t>(kPlayers + 0x60 + 0x52) = 1;
                Word<uint8_t>(kPlayers + 0x50) = 1;
                for (int8_t who : {int8_t(0), int8_t(1), int8_t(3), int8_t(-1)}) {
                    for (uint8_t count : {uint8_t(1), uint8_t(5), uint8_t(2)}) {
                        Message({static_cast<uint8_t>(who), count});
                        Handle(original, 0x0056CA70, SkateNet::HandleScavengerHunt, 0x95);
                    }
                }
            }
        }
        g_sn_teams = false;
        g_sn_localPlayer = kPlayers;
        Word<uint32_t>(kMsg) = 0x1234ABCD;
        Handle(original, 0x0056CDF0, SkateNet::HandleScavengerKillObject, 0x90);
        for (int8_t who : {int8_t(1), int8_t(3), int8_t(2)}) {
            Message({static_cast<uint8_t>(who)});
            Word<int>(kMsg + 4) = who == 2 ? 0 : 3;
            for (uint32_t i = 0; i < 12; ++i)
                Word<float>(kMsg + 8 + 4 * i) = 1.5f * i - 4.0f;
            Handle(original, 0x0056CEC0, SkateNet::HandleScavengerDropObjects, 0x8F);
        }
        // Graffiti colours.
        Message({2, 0, 3, 0, 0, 1, 0, 0, 5, 6, 7, 8, 9, 10});
        Handle(original, 0x0056D030, SkateNet::HandleGraffitiColors, 0x5C);
        g_sn_flags[0x11941568] = 1;
        Handle(original, 0x0056D030, SkateNet::HandleGraffitiColors, 0x5C);
        // The scores.
        auto scores = [&](std::vector<std::pair<int8_t, int>> entries, bool final, uint32_t checksum) {
            Word<uint32_t>(kMsg) = 0xAB;
            Word<uint32_t>(kMsg + 4) = checksum;
            Word<uint8_t>(kMsg + 8) = final;
            Word<int8_t>(kMsg + 9) = static_cast<int8_t>(entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                Word<int8_t>(kMsg + 10 + 5 * static_cast<uint32_t>(i)) = entries[i].first;
                std::memcpy(Ptr<int>(kMsg + 11 + 5 * static_cast<uint32_t>(i)), &entries[i].second, 4);
            }
            Handle(original, 0x0056D250, SkateNet::HandleScores, 0x40);
        };
        std::vector<std::pair<int8_t, int>> entries = {{0, 500}, {1, 0}, {2, 0}, {4, 30}, {-2, 7}, {5, 0}};
        g_sn_present = 0xF7;
        for (int variant = 0; variant < 4; ++variant) {
            g_sn_flags[0x11941568] = variant == 0;
            g_sn_otherModeFlag = variant == 1;
            g_sn_mode = variant >= 2 ? 0xBFF33600 : 0;
            g_sn_isServer = variant == 3;
            g_sn_cheatNoted = variant == 2;
            scores(entries, false, variant == 1 ? 0u : 9u);
        }
        g_sn_mode = 0;
        g_sn_flags[0x11941568] = 0;
        g_sn_otherModeFlag = false;
        // Team leaders.
        g_sn_teams = true;
        g_sn_flags[0xD63DFD0F] = 1;
        struct TeamCase {
            int a, b, last;
            bool final, shown;
            uint32_t local, mode;
        };
        for (TeamCase c : {TeamCase{10, 5, -1, false, false, kPlayers, 0}, TeamCase{10, 5, 0, false, false, kPlayers, 0},
                           TeamCase{3, 8, -1, false, false, kPlayers, 0}, TeamCase{3, 8, -1, false, true, kPlayers, 0},
                           TeamCase{4, 4, -1, false, false, kPlayers, 0}, TeamCase{0, 0, -1, false, false, kPlayers, 0},
                           TeamCase{10, 5, -1, true, false, kPlayers, 0}, TeamCase{3, 8, -1, true, false, kPlayers, 0},
                           TeamCase{3, 8, -1, true, false, kPlayers + 0x180, 0},
                           TeamCase{10, 5, -1, false, false, kPlayers + 0x60, 0x10F84276}}) {
            g_sn_teamScores[0] = c.a;
            g_sn_teamScores[1] = c.b;
            g_sn_lastTeam = c.last;
            g_sn_leaderShown = c.shown;
            g_sn_localPlayer = c.local;
            g_sn_mode = c.mode;
            if (c.local == kPlayers + 0x180)
                Word<uint32_t>(kPlayers + 0x180 + 0x14) = 0;
            else
                Word<uint32_t>(kPlayers + 0x180 + 0x14) = At(kObjects + 0xC00);
            scores({}, c.final, 0);
        }
        g_sn_teams = false;
        g_sn_mode = 0;
        g_sn_leaderShown = false;
        // Player leaders.
        struct PlayerCase {
            std::vector<int> scores;
            uint32_t last, local, first;
            bool final, shown, quiet;
        };
        Word<uint8_t>(kPlayers + 0x50) = 1;
        Word<uint8_t>(kPlayers + 0x60 + 0x50) = 0;
        for (PlayerCase c : {PlayerCase{{5, 9, 1, 0}, 0, kPlayers, kPlayers, false, true, false},
                             PlayerCase{{9, 5, 1, 0}, 0, kPlayers, kPlayers, false, true, false},
                             PlayerCase{{9, 5, 1, 0}, kPlayers, kPlayers, kPlayers, false, true, false},
                             PlayerCase{{5, 9, 1, 0}, 0, kPlayers, kPlayers + 0x120, false, true, false},
                             PlayerCase{{5, 9, 1, 4}, 0, kPlayers, kPlayers + 0x120, false, true, false},
                             PlayerCase{{5, 9, 1, 0}, 0, kPlayers, kPlayers, false, false, false},
                             PlayerCase{{5, 9, 1, 0}, 0, kPlayers, kPlayers, false, true, true},
                             PlayerCase{{9, 9, 1, 0}, 0, kPlayers, kPlayers, false, true, false},
                             PlayerCase{{0, 0, 0, 0}, 0, kPlayers, kPlayers, false, true, false},
                             PlayerCase{{9, 5, 1, 0}, 0, kPlayers, kPlayers, true, true, false},
                             PlayerCase{{5, 9, 1, 0}, 0, kPlayers, kPlayers, true, true, false},
                             PlayerCase{{5, 9, 1, 0}, 0, kPlayers + 0x180, kPlayers, true, true, false},
                             PlayerCase{{5, 9, 1, 0}, 0, kPlayers, 0, false, true, false}}) {
            for (int i = 0; i < 4; ++i)
                g_sn_playerScores[i] = c.scores[i];
            g_sn_lastPlayer = c.last;
            g_sn_localPlayer = c.local;
            g_sn_firstPlayer = c.first;
            g_sn_flags[0xD63DFD0F] = c.shown;
            GameVariable<bool>(0x006FA7D4) = c.quiet;
            Word<uint32_t>(kPlayers + 0x180 + 0x14) = c.local == kPlayers + 0x180 ? 0u : At(kObjects + 0xC00);
            scores({}, c.final, 0);
        }
        GameVariable<bool>(0x006FA7D4) = false;
        g_sn_firstPlayer = kPlayers;
        g_sn_localPlayer = kPlayers;
        // The start info.
        auto start = [&](uint32_t mode, uint32_t timeLimit, uint8_t variant, bool net, bool observing) {
            std::memset(Ptr<void>(kMsg), 0, 0x50);
            Message({7, static_cast<uint8_t>(mode & 1), 0x83, 0x55, 0xFE});
            Word<uint32_t>(kMsg + 8) = 0x77;
            Word<uint32_t>(kMsg + 0xC) = mode;
            Word<uint32_t>(kMsg + 0x10) = 0xABCD1234;
            Word<uint32_t>(kMsg + 0x14) = 1;
            Word<uint32_t>(kMsg + 0x18) = 2;
            Word<uint32_t>(kMsg + 0x1C) = 3;
            Word<uint32_t>(kMsg + 0x20) = timeLimit;
            Word<uint32_t>(kMsg + 0x24) = 2500;
            Word<uint32_t>(kMsg + 0x48) = 4;
            Word<uint8_t>(kMsg + 0x4C) = variant;
            g_sn_netGame = net;
            g_sn_observing = observing;
            Handle(original, 0x0056D810, SkateNet::HandleStartInfo, 0x33);
        };
        // The game type infos: checksum= for each structure; goal_script for some.
        uint32_t modes[] = {0x6C5FF266, 0x10F84276, 0xFC13B531};
        for (uint32_t a = 2; a < 4; ++a) {
            for (uint32_t e = 0; e < 3; ++e) {
                uint32_t structure = kFakes + 0x180 + 0x30 * a + 0x10 * e;
                g_sn_answers[{structure, 0x21902065}] = modes[(e + a) % 3];
                if (e != 1)
                    g_sn_answers[{structure, Hash("goal_script")}] = 0x5C000000 + structure;
            }
        }
        for (uint32_t mode : {0x1C471C60u, 0x6C5FF266u, 0xEC200EAAu, 0x3D6D444Fu, 0xBFF33600u, 0x10F84276u, 0xFC13B531u,
                              0x12345678u}) {
            for (uint32_t timeLimit : {0u, 120u}) {
                for (uint8_t variant : {uint8_t(0), uint8_t(1), uint8_t(2)})
                    start(mode, timeLimit, variant, variant == 1, variant == 2);
            }
        }
        // Scripts run.
        for (uint32_t object : {2u, 9u}) {
            Word<uint32_t>(kMsg) = 0x5C415C41;
            Word<uint32_t>(kMsg + 4) = object;
            Handle(original, 0x0056E380, SkateNet::HandleRunScript, 0x48);
        }
        traces[run] = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(g_sn_base);
        for (uint32_t i = 0; i < kHeap / 4; ++i)
            traces[run].push_back(Rel(words[i]));
    }
    Cleanup();
    Compare(traces[0], traces[1], "skate net handlers");
}

// The object updates: a run of messages for several skaters (first updates, every field and none, the time stamp
// going round, a missing object, packets flagged late / foreign), the state histories compared after each.
void TestSkateNetObjectUpdates()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        ResetState();
        g_sn_present = 0xF7; // (no object 3)
        uint32_t seed = 12345;
        auto random = [&seed](uint32_t bits) {
            seed = seed * 1103515245u + 12345u;
            return (seed >> 8) & (bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1));
        };
        for (int message = 0; message < 60; ++message) {
            std::memset(Ptr<void>(kMsg), 0, 0x400);
            Util::BitStream stream;
            stream.SetOutputData(Ptr<void>(kMsg), 0x400);
            uint32_t objects = message < 2 ? 0xFFu : random(8);
            stream.WriteBits(objects, 8);
            for (uint32_t i = 0; i < 8; ++i) {
                if (!(objects & (1u << i)))
                    continue;
                uint32_t time = message % 17 == 16 ? random(4) : (message * 2000u + i * 7u) & 0xFFFF;
                stream.WriteBits(time, 16);
                uint32_t fields = message == 0 ? (i % 2 ? 0x1FFu : 0u) : message == 1 ? 0u : random(9); // (first: half from the skater)
                stream.WriteBits(fields, 9);
                for (int k = 0; k < 6; ++k) {
                    if (fields & (1u << k))
                        stream.WriteBits(random(16), 16);
                }
                if (fields & 0x40) {
                    stream.WriteBits(random(4), 4);
                    stream.WriteBits(random(6), 6);
                    stream.WriteBits(random(1), 1);
                    stream.WriteBits(random(1), 1);
                }
                if (fields & 0x80) {
                    stream.WriteBits(random(5), 5);
                    stream.WriteBits(random(3), 3);
                }
                if (fields & 0x100)
                    stream.WriteBits(random(32), 32);
            }
            stream.Flush();
            uint32_t flags = message % 13 == 5 ? 1u : message % 13 == 6 ? 2u : message % 13 == 7 ? 4u : 0u;
            Handle(original, 0x0056E470, SkateNet::HandleObjectUpdates, 0x23, flags);
            g_trace.push_back(HashBytes(Ptr<uint8_t>(kHistories), 8 * 0xB00)); // (each message's result)
        }
        traces[run] = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(g_sn_base);
        for (uint32_t i = 0; i < kScores / 4; ++i)
            traces[run].push_back(Rel(words[i]));
    }
    Cleanup();
    Compare(traces[0], traces[1], "skate net object updates");
}

} // namespace

SELFTEST(Early, TestSkateNetRegistration, Cleanup);
SELFTEST(Early, TestSkateNetHandlers, Cleanup);
SELFTEST(Early, TestSkateNetObjectUpdates, Cleanup);
